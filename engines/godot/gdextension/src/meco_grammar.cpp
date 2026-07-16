/* SPDX-License-Identifier: MPL-2.0 */
#include "meco_grammar.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>
#include <vector>

#include "ffi_strings.h"

using namespace godot;

namespace mecojoni {

namespace {

Dictionary error_dictionary(const String &code, const String &message) {
    Dictionary result;
    result["ok"] = false;
    result["error_code"] = code;
    result["error_message"] = message;
    return result;
}

/* Owned UTF-8 copies backing the MecoDataBinding pointer table. */
struct BindingStorage {
    CharString name;
    CharString text;
};

} // namespace

MecoGrammar::~MecoGrammar() {
    meco_grammar_free(grammar);
}

void MecoGrammar::reset(::MecoGrammar *next_grammar) {
    meco_grammar_free(grammar);
    grammar = next_grammar;
    error_code = String();
    error_message = String();
}

void MecoGrammar::take_error(const String &fallback_code, ::MecoError *error) {
    if (error == nullptr) {
        error_code = fallback_code;
        error_message = String("mecojoni call failed without diagnostics");
    } else {
        error_code = view_to_string(meco_error_code(error));
        error_message = view_to_string(meco_error_message(error));
        meco_error_free(error);
    }
    ERR_PRINT(vformat("Mecojoni: %s: %s", error_code, error_message));
}

Dictionary MecoGrammar::versions() {
    Dictionary result;
    result["ffi_abi"] = static_cast<int64_t>(meco_ffi_abi_version());
    result["core_api"] = static_cast<int64_t>(meco_core_api_version());
    result["bytecode"] = view_to_string(meco_bytecode_version());
    return result;
}

Error MecoGrammar::compile_source(const String &name, const String &source) {
    const CharString name_utf8 = name.utf8();
    const CharString source_utf8 = source.utf8();
    ::MecoGrammar *compiled = nullptr;
    ::MecoError *error = nullptr;
    const int32_t status = meco_grammar_compile(
            name_utf8.get_data(),
            reinterpret_cast<const uint8_t *>(source_utf8.get_data()),
            static_cast<size_t>(source_utf8.length()), &compiled, &error);
    if (status != MECO_STATUS_OK) {
        reset(nullptr);
        take_error("E_INVALID_ARGUMENT", error);
        return FAILED;
    }
    reset(compiled);
    return OK;
}

Error MecoGrammar::compile_file(const String &path) {
    const String source = FileAccess::get_file_as_string(path);
    const Error open_error = FileAccess::get_open_error();
    if (open_error != OK) {
        reset(nullptr);
        error_code = String("E_FILE");
        error_message = vformat("cannot read %s", path);
        return open_error;
    }
    return compile_source(path.get_file(), source);
}

Error MecoGrammar::load_artifact(const PackedByteArray &bytes) {
    ::MecoGrammar *decoded = nullptr;
    ::MecoError *error = nullptr;
    const int32_t status = meco_grammar_decode(
            bytes.ptr(), static_cast<size_t>(bytes.size()), &decoded, &error);
    if (status != MECO_STATUS_OK) {
        reset(nullptr);
        take_error("E_INVALID_ARGUMENT", error);
        return FAILED;
    }
    reset(decoded);
    return OK;
}

Error MecoGrammar::load_artifact_file(const String &path) {
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
    const Error open_error = FileAccess::get_open_error();
    if (open_error != OK) {
        reset(nullptr);
        error_code = String("E_FILE");
        error_message = vformat("cannot read %s", path);
        return open_error;
    }
    return load_artifact(bytes);
}

PackedByteArray MecoGrammar::encode_artifact(int64_t debug_profile) const {
    PackedByteArray result;
    ERR_FAIL_NULL_V_MSG(grammar, result, "no grammar is loaded");
    ::MecoArtifact *artifact = nullptr;
    ::MecoError *error = nullptr;
    const int32_t status = meco_grammar_encode(
            grammar, static_cast<uint32_t>(debug_profile), &artifact, &error);
    if (status != MECO_STATUS_OK) {
        if (error != nullptr) {
            ERR_PRINT(vformat("Mecojoni: %s: %s",
                              view_to_string(meco_error_code(error)),
                              view_to_string(meco_error_message(error))));
            meco_error_free(error);
        }
        return result;
    }
    const uint8_t *data = meco_artifact_data(artifact);
    const int64_t size = static_cast<int64_t>(meco_artifact_len(artifact));
    result.resize(size);
    memcpy(result.ptrw(), data, static_cast<size_t>(size));
    meco_artifact_free(artifact);
    return result;
}

bool MecoGrammar::is_compiled() const {
    return grammar != nullptr;
}

String MecoGrammar::get_error_code() const {
    return error_code;
}

String MecoGrammar::get_error_message() const {
    return error_message;
}

PackedStringArray MecoGrammar::get_entries() const {
    PackedStringArray entries;
    const size_t count = meco_grammar_entry_count(grammar);
    for (size_t index = 0; index < count; index += 1) {
        entries.push_back(view_to_string(meco_grammar_entry(grammar, index)));
    }
    return entries;
}

String MecoGrammar::get_default_entry() const {
    return view_to_string(meco_grammar_default_entry(grammar));
}

int64_t MecoGrammar::get_rule_count() const {
    return static_cast<int64_t>(meco_grammar_rule_count(grammar));
}

int64_t MecoGrammar::get_production_count() const {
    return static_cast<int64_t>(meco_grammar_production_count(grammar));
}

PackedStringArray MecoGrammar::get_warnings() const {
    PackedStringArray warnings;
    const size_t count = meco_grammar_warning_count(grammar);
    for (size_t index = 0; index < count; index += 1) {
        warnings.push_back(vformat(
                "%s: %s", view_to_string(meco_grammar_warning_code(grammar, index)),
                view_to_string(meco_grammar_warning_message(grammar, index))));
    }
    return warnings;
}

Dictionary MecoGrammar::generate(uint64_t seed, const String &entry,
                                 const Dictionary &data) {
    if (grammar == nullptr) {
        return error_dictionary("E_NO_GRAMMAR", "no grammar is loaded");
    }

    /* Convert the data Dictionary into the C binding table. The storage
     * vector owns every UTF-8 copy for the duration of the call. */
    const Array keys = data.keys();
    std::vector<BindingStorage> storage(static_cast<size_t>(keys.size()));
    std::vector<::MecoDataBinding> bindings(static_cast<size_t>(keys.size()));
    for (int64_t index = 0; index < keys.size(); index += 1) {
        const Variant &key = keys[index];
        if (key.get_type() != Variant::STRING &&
            key.get_type() != Variant::STRING_NAME) {
            return error_dictionary("E_INPUT", "data keys must be strings");
        }
        BindingStorage &slot = storage[static_cast<size_t>(index)];
        slot.name = String(key).utf8();

        ::MecoDataBinding &binding = bindings[static_cast<size_t>(index)];
        binding = ::MecoDataBinding{slot.name.get_data(), nullptr, 0, 1,
                                    MECO_VALUE_TEXT, 0};

        const Variant value = data[key];
        switch (value.get_type()) {
            case Variant::STRING: {
                slot.text = String(value).utf8();
                binding.kind = MECO_VALUE_TEXT;
                binding.text = slot.text.get_data();
            } break;
            case Variant::STRING_NAME: {
                slot.text = String(value).utf8();
                binding.kind = MECO_VALUE_ENUM;
                binding.text = slot.text.get_data();
            } break;
            case Variant::BOOL: {
                binding.kind = MECO_VALUE_BOOLEAN;
                binding.boolean = bool(value) ? 1 : 0;
            } break;
            case Variant::INT: {
                binding.kind = MECO_VALUE_NUMBER;
                binding.numerator = int64_t(value);
                binding.denominator = 1;
            } break;
            case Variant::ARRAY: {
                const Array pair = value;
                if (pair.size() != 2 ||
                    pair[0].get_type() != Variant::INT ||
                    pair[1].get_type() != Variant::INT ||
                    int64_t(pair[1]) <= 0) {
                    return error_dictionary(
                            "E_INPUT",
                            vformat("input `%s`: a rational must be "
                                    "[numerator, denominator] with int items "
                                    "and a positive denominator",
                                    String(key)));
                }
                binding.kind = MECO_VALUE_NUMBER;
                binding.numerator = int64_t(pair[0]);
                binding.denominator = static_cast<uint64_t>(int64_t(pair[1]));
            } break;
            default:
                return error_dictionary(
                        "E_INPUT",
                        vformat("input `%s` has unsupported type %s; use "
                                "String, StringName, bool, int, or "
                                "[numerator, denominator]",
                                String(key),
                                Variant::get_type_name(value.get_type())));
        }
    }

    const CharString entry_utf8 = entry.utf8();
    ::MecoGenerateOptions options = {};
    options.entry = entry.is_empty() ? nullptr : entry_utf8.get_data();
    options.data = bindings.empty() ? nullptr : bindings.data();
    options.data_count = bindings.size();
    options.seed = seed;

    ::MecoGenerated *generated = nullptr;
    ::MecoError *error = nullptr;
    const int32_t status =
            meco_grammar_generate(grammar, &options, &generated, &error);
    if (status != MECO_STATUS_OK) {
        String code = String("E_INVALID_ARGUMENT");
        String message = String("invalid generation request");
        if (error != nullptr) {
            code = view_to_string(meco_error_code(error));
            message = view_to_string(meco_error_message(error));
            meco_error_free(error);
        }
        return error_dictionary(code, message);
    }

    Dictionary result;
    result["ok"] = true;
    result["text"] = view_to_string(meco_generated_text(generated));
    result["entry"] = view_to_string(meco_generated_entry(generated));
    result["expansions"] =
            static_cast<int64_t>(meco_generated_expansions(generated));
    result["sampler_words"] =
            static_cast<int64_t>(meco_generated_sampler_words(generated));
    meco_generated_free(generated);
    return result;
}

void MecoGrammar::adopt(::MecoGrammar *owned_grammar) {
    reset(owned_grammar);
}

void MecoGrammar::_bind_methods() {
    ClassDB::bind_static_method("MecoGrammar", D_METHOD("versions"),
                                &MecoGrammar::versions);
    ClassDB::bind_method(D_METHOD("compile_source", "name", "source"),
                         &MecoGrammar::compile_source);
    ClassDB::bind_method(D_METHOD("compile_file", "path"),
                         &MecoGrammar::compile_file);
    ClassDB::bind_method(D_METHOD("load_artifact", "bytes"),
                         &MecoGrammar::load_artifact);
    ClassDB::bind_method(D_METHOD("load_artifact_file", "path"),
                         &MecoGrammar::load_artifact_file);
    ClassDB::bind_method(D_METHOD("encode_artifact", "debug_profile"),
                         &MecoGrammar::encode_artifact, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("is_compiled"), &MecoGrammar::is_compiled);
    ClassDB::bind_method(D_METHOD("get_error_code"),
                         &MecoGrammar::get_error_code);
    ClassDB::bind_method(D_METHOD("get_error_message"),
                         &MecoGrammar::get_error_message);
    ClassDB::bind_method(D_METHOD("get_entries"), &MecoGrammar::get_entries);
    ClassDB::bind_method(D_METHOD("get_default_entry"),
                         &MecoGrammar::get_default_entry);
    ClassDB::bind_method(D_METHOD("get_rule_count"),
                         &MecoGrammar::get_rule_count);
    ClassDB::bind_method(D_METHOD("get_production_count"),
                         &MecoGrammar::get_production_count);
    ClassDB::bind_method(D_METHOD("get_warnings"), &MecoGrammar::get_warnings);
    ClassDB::bind_method(D_METHOD("generate", "seed", "entry", "data"),
                         &MecoGrammar::generate, DEFVAL(String()),
                         DEFVAL(Dictionary()));
}

} // namespace mecojoni
