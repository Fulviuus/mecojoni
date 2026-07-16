/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>

#include "meco_grammar.h"
#include "mecojoni.h"

namespace mecojoni {

/* Accumulates the modules of one multi-module Mecojoni package and compiles
 * them into a MecoGrammar. Godot performs no file I/O on the package's
 * behalf: every authored `imports:` path must be resolved explicitly with
 * resolve_import, mirroring the core's I/O-free compilation contract. */
class MecoPackageBuilder : public godot::RefCounted {
    GDCLASS(MecoPackageBuilder, godot::RefCounted)

    ::MecoPackageBuilder *builder = nullptr;
    godot::String error_code;
    godot::String error_message;

    void take_error(::MecoError *error);

protected:
    static void _bind_methods();

public:
    MecoPackageBuilder();
    ~MecoPackageBuilder() override;

    godot::Error add_module(const godot::String &canonical_id,
                            const godot::String &display_name,
                            const godot::String &source);
    /* Reads the module source from `path`; display name is the file name. */
    godot::Error add_module_file(const godot::String &canonical_id,
                                 const godot::String &path);
    godot::Error resolve_import(const godot::String &module_canonical_id,
                                const godot::String &authored_path,
                                const godot::String &target_canonical_id);

    /* Compiles the package rooted at `root_canonical_id` ("" selects the
     * first added module). Returns null on failure; see get_error_message().
     * The builder stays reusable. */
    godot::Ref<MecoGrammar> compile(const godot::String &root_canonical_id);

    godot::String get_error_code() const;
    godot::String get_error_message() const;
};

} // namespace mecojoni
