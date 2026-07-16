/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "mecojoni.h"

namespace mecojoni {

/* Godot-facing handle for one compiled Mecojoni grammar.
 *
 * Wraps the opaque ::MecoGrammar from the C ABI. Load it from `.meco` source
 * (compile_source/compile_file), from compiled `bytecode/1` artifacts
 * (load_artifact/load_artifact_file), or from MecoPackageBuilder for
 * multi-module packages. One instance must not be used from two threads at
 * once; distinct instances are independent. */
class MecoGrammar : public godot::RefCounted {
    GDCLASS(MecoGrammar, godot::RefCounted)

    ::MecoGrammar *grammar = nullptr;
    godot::String error_code;
    godot::String error_message;

    void reset(::MecoGrammar *next_grammar);
    /* Records and frees a C-ABI error; `error` may be null. */
    void take_error(const godot::String &fallback_code, ::MecoError *error);

protected:
    static void _bind_methods();

public:
    ~MecoGrammar() override;

    /* Build metadata: ffi_abi, core_api, and bytecode format versions. */
    static godot::Dictionary versions();

    godot::Error compile_source(const godot::String &name, const godot::String &source);
    godot::Error compile_file(const godot::String &path);
    godot::Error load_artifact(const godot::PackedByteArray &bytes);
    godot::Error load_artifact_file(const godot::String &path);
    /* Empty on failure; see get_error_message(). Profile: 0 full, 1 mapped,
     * 2 stripped. */
    godot::PackedByteArray encode_artifact(int64_t debug_profile) const;

    bool is_compiled() const;
    godot::String get_error_code() const;
    godot::String get_error_message() const;

    godot::PackedStringArray get_entries() const;
    godot::String get_default_entry() const;
    int64_t get_rule_count() const;
    int64_t get_production_count() const;
    /* One "CODE: message" line per compiler warning. */
    godot::PackedStringArray get_warnings() const;

    /* One deterministic weighted generation. `entry` "" selects the default
     * entry. `data` maps declared input names to values: String -> text,
     * StringName -> enum member, bool -> boolean, int -> number, and
     * [numerator, denominator] -> exact rational. Returns {"ok": true,
     * "text": ..., "entry": ..., "expansions": ..., "sampler_words": ...} or
     * {"ok": false, "error_code": ..., "error_message": ...}. */
    godot::Dictionary generate(uint64_t seed, const godot::String &entry,
                               const godot::Dictionary &data);

    /* Used by MecoPackageBuilder to hand over an owned C grammar. */
    void adopt(::MecoGrammar *owned_grammar);
};

} // namespace mecojoni
