// SPDX-License-Identifier: MPL-2.0
//
// Defold native extension exposing the Mecojoni grammar VM to Lua.
//
// Architecture: mecojoni-core (no_std Rust) -> mecojoni-ffi (C ABI, see
// include/mecojoni.h) -> this bridge -> the Lua `mecojoni` module. The C ABI
// is engine-agnostic and shared with the Godot adapter; this file only
// translates between Lua values and the C contract.
//
// Handles (compiled grammars, package builders) are returned to Lua as full
// userdata carrying the opaque C pointer, with a __gc metamethod that frees
// it. One handle must not be used from two threads at once.

#include <dmsdk/sdk.h>

#include <stdint.h>
#include <string>
#include <vector>

#include "mecojoni.h"

#define EXTENSION_NAME Mecojoni
#define LIB_NAME "Mecojoni"
#define MODULE_NAME "mecojoni"

namespace {

const char* GRAMMAR_MT = "mecojoni.Grammar";
const char* BUILDER_MT = "mecojoni.PackageBuilder";

struct GrammarBox {
    MecoGrammar* grammar;
};

struct BuilderBox {
    MecoPackageBuilder* builder;
};

// --- small helpers ---------------------------------------------------------

void PushStrView(lua_State* L, MecoStrView view) {
    if (view.data == 0) {
        lua_pushstring(L, "");
    } else {
        lua_pushlstring(L, view.data, view.len);
    }
}

// Pushes two return values: nil and an {error_code, error_message} table.
// Consumes and frees `error` (may be null).
int PushError(lua_State* L, const char* fallback_code, MecoError* error) {
    lua_pushnil(L);
    lua_newtable(L);
    if (error != 0) {
        PushStrView(L, meco_error_code(error));
        lua_setfield(L, -2, "error_code");
        PushStrView(L, meco_error_message(error));
        lua_setfield(L, -2, "error_message");
        meco_error_free(error);
    } else {
        lua_pushstring(L, fallback_code);
        lua_setfield(L, -2, "error_code");
        lua_pushstring(L, "mecojoni call failed without diagnostics");
        lua_setfield(L, -2, "error_message");
    }
    return 2;
}

GrammarBox* CheckGrammar(lua_State* L, int index) {
    return (GrammarBox*)luaL_checkudata(L, index, GRAMMAR_MT);
}

BuilderBox* CheckBuilder(lua_State* L, int index) {
    return (BuilderBox*)luaL_checkudata(L, index, BUILDER_MT);
}

// Wraps an owned C grammar in a fresh userdata with the grammar metatable.
void PushGrammar(lua_State* L, MecoGrammar* grammar) {
    GrammarBox* box = (GrammarBox*)lua_newuserdata(L, sizeof(GrammarBox));
    box->grammar = grammar;
    luaL_getmetatable(L, GRAMMAR_MT);
    lua_setmetatable(L, -2);
}

// One collected host input, owning its string payloads so the C binding
// pointers stay valid for the duration of the generate call.
struct InputEntry {
    std::string name;
    std::string text;
    int64_t numerator;
    uint64_t denominator;
    uint32_t kind;
    uint8_t boolean;
};

// Reads the data table at `table_index` into `entries`. On a malformed entry
// pushes nothing and returns a Lua error string via `err` (caller reports it).
bool CollectBindings(lua_State* L, int table_index, std::vector<InputEntry>& entries,
                     std::string& err) {
    lua_pushnil(L);
    while (lua_next(L, table_index) != 0) {
        // key at -2, value at -1.
        if (lua_type(L, -2) != LUA_TSTRING) {
            err = "data keys must be strings (input names)";
            lua_pop(L, 2);
            return false;
        }
        InputEntry entry;
        entry.name = lua_tostring(L, -2);
        entry.numerator = 0;
        entry.denominator = 1;
        entry.kind = MECO_VALUE_TEXT;
        entry.boolean = 0;

        int value = lua_gettop(L); // absolute index of the value
        switch (lua_type(L, value)) {
            case LUA_TSTRING:
                entry.kind = MECO_VALUE_TEXT;
                entry.text = lua_tostring(L, value);
                break;
            case LUA_TBOOLEAN:
                entry.kind = MECO_VALUE_BOOLEAN;
                entry.boolean = lua_toboolean(L, value) ? 1 : 0;
                break;
            case LUA_TNUMBER: {
                double number = lua_tonumber(L, value);
                double whole = (double)(int64_t)number;
                if (number != whole) {
                    err = "input `" + entry.name +
                          "`: numbers must be integers; use {numerator, "
                          "denominator} for exact rationals";
                    lua_pop(L, 2);
                    return false;
                }
                entry.kind = MECO_VALUE_NUMBER;
                entry.numerator = (int64_t)number;
                entry.denominator = 1;
                break;
            }
            case LUA_TTABLE: {
                lua_getfield(L, value, "enum");
                if (lua_type(L, -1) == LUA_TSTRING) {
                    entry.kind = MECO_VALUE_ENUM;
                    entry.text = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    break;
                }
                lua_pop(L, 1);
                // Otherwise expect a [numerator, denominator] array.
                lua_rawgeti(L, value, 1);
                lua_rawgeti(L, value, 2);
                if (lua_type(L, -2) != LUA_TNUMBER || lua_type(L, -1) != LUA_TNUMBER) {
                    err = "input `" + entry.name +
                          "`: table values must be {enum=\"member\"} or "
                          "{numerator, denominator}";
                    lua_pop(L, 4);
                    return false;
                }
                double num = lua_tonumber(L, -2);
                double den = lua_tonumber(L, -1);
                lua_pop(L, 2);
                if (den != (double)(int64_t)den || den <= 0 ||
                    num != (double)(int64_t)num) {
                    err = "input `" + entry.name +
                          "`: a rational needs integer numerator and a "
                          "positive integer denominator";
                    lua_pop(L, 2);
                    return false;
                }
                entry.kind = MECO_VALUE_NUMBER;
                entry.numerator = (int64_t)num;
                entry.denominator = (uint64_t)(int64_t)den;
                break;
            }
            default:
                err = "input `" + entry.name +
                      "` has an unsupported value type; use string, boolean, "
                      "integer, {enum=..}, or {numerator, denominator}";
                lua_pop(L, 2);
                return false;
        }
        entries.push_back(entry);
        lua_pop(L, 1); // pop value, keep key for lua_next
    }
    return true;
}

// --- module functions ------------------------------------------------------

int Meco_version(lua_State* L) {
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)meco_ffi_abi_version());
    lua_setfield(L, -2, "ffi_abi");
    lua_pushinteger(L, (lua_Integer)meco_core_api_version());
    lua_setfield(L, -2, "core_api");
    PushStrView(L, meco_bytecode_version());
    lua_setfield(L, -2, "bytecode");
    return 1;
}

int Meco_compile(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    size_t len = 0;
    const char* source = luaL_checklstring(L, 2, &len);
    MecoGrammar* grammar = 0;
    MecoError* error = 0;
    int32_t status =
        meco_grammar_compile(name, (const uint8_t*)source, len, &grammar, &error);
    if (status != MECO_STATUS_OK) {
        return PushError(L, "E_INVALID_ARGUMENT", error);
    }
    PushGrammar(L, grammar);
    return 1;
}

int Meco_load_artifact(lua_State* L) {
    size_t len = 0;
    const char* bytes = luaL_checklstring(L, 1, &len);
    MecoGrammar* grammar = 0;
    MecoError* error = 0;
    int32_t status = meco_grammar_decode((const uint8_t*)bytes, len, &grammar, &error);
    if (status != MECO_STATUS_OK) {
        return PushError(L, "E_INVALID_ARGUMENT", error);
    }
    PushGrammar(L, grammar);
    return 1;
}

// Convenience so callers can write mecojoni.enum("member") in a data table.
int Meco_enum(lua_State* L) {
    const char* member = luaL_checkstring(L, 1);
    lua_newtable(L);
    lua_pushstring(L, member);
    lua_setfield(L, -2, "enum");
    return 1;
}

int Meco_package_builder(lua_State* L) {
    BuilderBox* box = (BuilderBox*)lua_newuserdata(L, sizeof(BuilderBox));
    box->builder = meco_package_builder_new();
    luaL_getmetatable(L, BUILDER_MT);
    lua_setmetatable(L, -2);
    return 1;
}

// --- grammar methods -------------------------------------------------------

int Grammar_generate(lua_State* L) {
    GrammarBox* box = CheckGrammar(L, 1);
    if (box->grammar == 0) {
        lua_newtable(L);
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "ok");
        lua_pushstring(L, "E_NO_GRAMMAR");
        lua_setfield(L, -2, "error_code");
        lua_pushstring(L, "grammar has been freed");
        lua_setfield(L, -2, "error_message");
        return 1;
    }
    uint64_t seed = (uint64_t)(int64_t)luaL_optnumber(L, 2, 0);
    const char* entry = luaL_optstring(L, 3, 0);
    if (entry != 0 && entry[0] == '\0') {
        entry = 0; // "" means the default entry
    }

    std::vector<InputEntry> entries;
    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);
        std::string collect_error;
        if (!CollectBindings(L, 4, entries, collect_error)) {
            lua_newtable(L);
            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "ok");
            lua_pushstring(L, "E_INPUT");
            lua_setfield(L, -2, "error_code");
            lua_pushstring(L, collect_error.c_str());
            lua_setfield(L, -2, "error_message");
            return 1;
        }
    }

    std::vector<MecoDataBinding> bindings(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        bindings[i].name = entries[i].name.c_str();
        bindings[i].text = entries[i].text.c_str();
        bindings[i].numerator = entries[i].numerator;
        bindings[i].denominator = entries[i].denominator;
        bindings[i].kind = entries[i].kind;
        bindings[i].boolean = entries[i].boolean;
    }

    MecoGenerateOptions options;
    options.entry = entry;
    options.data = bindings.empty() ? 0 : &bindings[0];
    options.data_count = bindings.size();
    options.seed = seed;
    options.max_depth = 0;
    options.max_expansions = 0;
    options.max_output_scalars = 0;
    options.max_output_bytes = 0;
    options.max_sampler_words = 0;

    MecoGenerated* generated = 0;
    MecoError* error = 0;
    int32_t status = meco_grammar_generate(box->grammar, &options, &generated, &error);

    lua_newtable(L);
    if (status != MECO_STATUS_OK) {
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "ok");
        if (error != 0) {
            PushStrView(L, meco_error_code(error));
            lua_setfield(L, -2, "error_code");
            PushStrView(L, meco_error_message(error));
            lua_setfield(L, -2, "error_message");
            meco_error_free(error);
        } else {
            lua_pushstring(L, "E_INVALID_ARGUMENT");
            lua_setfield(L, -2, "error_code");
            lua_pushstring(L, "invalid generation request");
            lua_setfield(L, -2, "error_message");
        }
        return 1;
    }
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "ok");
    PushStrView(L, meco_generated_text(generated));
    lua_setfield(L, -2, "text");
    PushStrView(L, meco_generated_entry(generated));
    lua_setfield(L, -2, "entry");
    lua_pushinteger(L, (lua_Integer)meco_generated_expansions(generated));
    lua_setfield(L, -2, "expansions");
    lua_pushinteger(L, (lua_Integer)meco_generated_sampler_words(generated));
    lua_setfield(L, -2, "sampler_words");
    meco_generated_free(generated);
    return 1;
}

int Grammar_entries(lua_State* L) {
    GrammarBox* box = CheckGrammar(L, 1);
    size_t count = meco_grammar_entry_count(box->grammar);
    lua_newtable(L);
    for (size_t i = 0; i < count; ++i) {
        PushStrView(L, meco_grammar_entry(box->grammar, i));
        lua_rawseti(L, -2, (int)(i + 1));
    }
    return 1;
}

int Grammar_default_entry(lua_State* L) {
    GrammarBox* box = CheckGrammar(L, 1);
    MecoStrView view = meco_grammar_default_entry(box->grammar);
    if (view.data == 0) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, view.data, view.len);
    }
    return 1;
}

int Grammar_rule_count(lua_State* L) {
    GrammarBox* box = CheckGrammar(L, 1);
    lua_pushinteger(L, (lua_Integer)meco_grammar_rule_count(box->grammar));
    return 1;
}

int Grammar_production_count(lua_State* L) {
    GrammarBox* box = CheckGrammar(L, 1);
    lua_pushinteger(L, (lua_Integer)meco_grammar_production_count(box->grammar));
    return 1;
}

int Grammar_warnings(lua_State* L) {
    GrammarBox* box = CheckGrammar(L, 1);
    size_t count = meco_grammar_warning_count(box->grammar);
    lua_newtable(L);
    for (size_t i = 0; i < count; ++i) {
        MecoStrView code = meco_grammar_warning_code(box->grammar, i);
        MecoStrView message = meco_grammar_warning_message(box->grammar, i);
        std::string line;
        if (code.data != 0) line.append(code.data, code.len);
        line.append(": ");
        if (message.data != 0) line.append(message.data, message.len);
        lua_pushlstring(L, line.data(), line.size());
        lua_rawseti(L, -2, (int)(i + 1));
    }
    return 1;
}

int Grammar_encode_artifact(lua_State* L) {
    GrammarBox* box = CheckGrammar(L, 1);
    uint32_t profile = (uint32_t)luaL_optinteger(L, 2, MECO_PROFILE_FULL);
    MecoArtifact* artifact = 0;
    MecoError* error = 0;
    int32_t status = meco_grammar_encode(box->grammar, profile, &artifact, &error);
    if (status != MECO_STATUS_OK) {
        return PushError(L, "E_INVALID_ARGUMENT", error);
    }
    lua_pushlstring(L, (const char*)meco_artifact_data(artifact),
                    meco_artifact_len(artifact));
    meco_artifact_free(artifact);
    return 1;
}

int Grammar_gc(lua_State* L) {
    GrammarBox* box = CheckGrammar(L, 1);
    if (box->grammar != 0) {
        meco_grammar_free(box->grammar);
        box->grammar = 0;
    }
    return 0;
}

int Grammar_tostring(lua_State* L) {
    GrammarBox* box = CheckGrammar(L, 1);
    lua_pushfstring(L, "mecojoni.Grammar(%p)", (void*)box->grammar);
    return 1;
}

// --- package builder methods -----------------------------------------------

int Builder_add_module(lua_State* L) {
    BuilderBox* box = CheckBuilder(L, 1);
    const char* canonical_id = luaL_checkstring(L, 2);
    const char* display_name = luaL_checkstring(L, 3);
    size_t len = 0;
    const char* source = luaL_checklstring(L, 4, &len);
    MecoError* error = 0;
    int32_t status = meco_package_builder_add_module(
        box->builder, canonical_id, display_name, (const uint8_t*)source, len, &error);
    if (status != MECO_STATUS_OK) {
        return PushError(L, "E_INVALID_ARGUMENT", error);
    }
    lua_pushboolean(L, 1);
    return 1;
}

int Builder_resolve_import(lua_State* L) {
    BuilderBox* box = CheckBuilder(L, 1);
    const char* module_id = luaL_checkstring(L, 2);
    const char* authored_path = luaL_checkstring(L, 3);
    const char* target_id = luaL_checkstring(L, 4);
    MecoError* error = 0;
    int32_t status = meco_package_builder_resolve_import(
        box->builder, module_id, authored_path, target_id, &error);
    if (status != MECO_STATUS_OK) {
        return PushError(L, "E_INVALID_ARGUMENT", error);
    }
    lua_pushboolean(L, 1);
    return 1;
}

int Builder_compile(lua_State* L) {
    BuilderBox* box = CheckBuilder(L, 1);
    const char* root_id = luaL_optstring(L, 2, 0);
    MecoGrammar* grammar = 0;
    MecoError* error = 0;
    int32_t status =
        meco_package_builder_compile(box->builder, root_id, &grammar, &error);
    if (status != MECO_STATUS_OK) {
        return PushError(L, "E_INVALID_ARGUMENT", error);
    }
    PushGrammar(L, grammar);
    return 1;
}

int Builder_gc(lua_State* L) {
    BuilderBox* box = CheckBuilder(L, 1);
    if (box->builder != 0) {
        meco_package_builder_free(box->builder);
        box->builder = 0;
    }
    return 0;
}

const luaL_reg Module_methods[] = {
    {"version", Meco_version},
    {"compile", Meco_compile},
    {"load_artifact", Meco_load_artifact},
    {"enum", Meco_enum},
    {"package_builder", Meco_package_builder},
    {0, 0}};

const luaL_reg Grammar_methods[] = {
    {"generate", Grammar_generate},
    {"entries", Grammar_entries},
    {"default_entry", Grammar_default_entry},
    {"rule_count", Grammar_rule_count},
    {"production_count", Grammar_production_count},
    {"warnings", Grammar_warnings},
    {"encode_artifact", Grammar_encode_artifact},
    {"__gc", Grammar_gc},
    {"__tostring", Grammar_tostring},
    {0, 0}};

const luaL_reg Builder_methods[] = {
    {"add_module", Builder_add_module},
    {"resolve_import", Builder_resolve_import},
    {"compile", Builder_compile},
    {"__gc", Builder_gc},
    {0, 0}};

void RegisterMetatable(lua_State* L, const char* name, const luaL_reg* methods) {
    luaL_newmetatable(L, name);      // mt
    lua_pushvalue(L, -1);            // mt, mt
    lua_setfield(L, -2, "__index");  // mt.__index = mt
    luaL_register(L, 0, methods);    // register methods into mt
    lua_pop(L, 1);
}

void LuaInit(lua_State* L) {
    int top = lua_gettop(L);
    RegisterMetatable(L, GRAMMAR_MT, Grammar_methods);
    RegisterMetatable(L, BUILDER_MT, Builder_methods);
    luaL_register(L, MODULE_NAME, Module_methods);
    lua_pop(L, 1); // pop the module table
    assert(top == lua_gettop(L));
}

dmExtension::Result AppInitializeMecojoni(dmExtension::AppParams* params) {
    return dmExtension::RESULT_OK;
}

dmExtension::Result InitializeMecojoni(dmExtension::Params* params) {
    LuaInit(params->m_L);
    return dmExtension::RESULT_OK;
}

dmExtension::Result AppFinalizeMecojoni(dmExtension::AppParams* params) {
    return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeMecojoni(dmExtension::Params* params) {
    return dmExtension::RESULT_OK;
}

} // namespace

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, AppInitializeMecojoni, AppFinalizeMecojoni,
                     InitializeMecojoni, 0, 0, FinalizeMecojoni)
