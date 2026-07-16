/* SPDX-License-Identifier: MPL-2.0 */
#include "meco_package_builder.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "ffi_strings.h"

using namespace godot;

namespace mecojoni {

MecoPackageBuilder::MecoPackageBuilder() : builder(meco_package_builder_new()) {}

MecoPackageBuilder::~MecoPackageBuilder() {
    meco_package_builder_free(builder);
}

void MecoPackageBuilder::take_error(::MecoError *error) {
    if (error == nullptr) {
        error_code = String("E_INVALID_ARGUMENT");
        error_message = String("mecojoni call failed without diagnostics");
    } else {
        error_code = view_to_string(meco_error_code(error));
        error_message = view_to_string(meco_error_message(error));
        meco_error_free(error);
    }
    ERR_PRINT(vformat("Mecojoni: %s: %s", error_code, error_message));
}

Error MecoPackageBuilder::add_module(const String &canonical_id,
                                     const String &display_name,
                                     const String &source) {
    const CharString id_utf8 = canonical_id.utf8();
    const CharString name_utf8 = display_name.utf8();
    const CharString source_utf8 = source.utf8();
    ::MecoError *error = nullptr;
    const int32_t status = meco_package_builder_add_module(
            builder, id_utf8.get_data(), name_utf8.get_data(),
            reinterpret_cast<const uint8_t *>(source_utf8.get_data()),
            static_cast<size_t>(source_utf8.length()), &error);
    if (status != MECO_STATUS_OK) {
        take_error(error);
        return FAILED;
    }
    return OK;
}

Error MecoPackageBuilder::add_module_file(const String &canonical_id,
                                          const String &path) {
    const String source = FileAccess::get_file_as_string(path);
    const Error open_error = FileAccess::get_open_error();
    if (open_error != OK) {
        error_code = String("E_FILE");
        error_message = vformat("cannot read %s", path);
        return open_error;
    }
    return add_module(canonical_id, path.get_file(), source);
}

Error MecoPackageBuilder::resolve_import(const String &module_canonical_id,
                                         const String &authored_path,
                                         const String &target_canonical_id) {
    const CharString module_utf8 = module_canonical_id.utf8();
    const CharString path_utf8 = authored_path.utf8();
    const CharString target_utf8 = target_canonical_id.utf8();
    ::MecoError *error = nullptr;
    const int32_t status = meco_package_builder_resolve_import(
            builder, module_utf8.get_data(), path_utf8.get_data(),
            target_utf8.get_data(), &error);
    if (status != MECO_STATUS_OK) {
        take_error(error);
        return FAILED;
    }
    return OK;
}

Ref<MecoGrammar> MecoPackageBuilder::compile(const String &root_canonical_id) {
    const CharString root_utf8 = root_canonical_id.utf8();
    ::MecoGrammar *grammar = nullptr;
    ::MecoError *error = nullptr;
    const int32_t status = meco_package_builder_compile(
            builder, root_utf8.get_data(), &grammar, &error);
    if (status != MECO_STATUS_OK) {
        take_error(error);
        return Ref<MecoGrammar>();
    }
    Ref<MecoGrammar> wrapped;
    wrapped.instantiate();
    wrapped->adopt(grammar);
    error_code = String();
    error_message = String();
    return wrapped;
}

String MecoPackageBuilder::get_error_code() const {
    return error_code;
}

String MecoPackageBuilder::get_error_message() const {
    return error_message;
}

void MecoPackageBuilder::_bind_methods() {
    ClassDB::bind_method(
            D_METHOD("add_module", "canonical_id", "display_name", "source"),
            &MecoPackageBuilder::add_module);
    ClassDB::bind_method(D_METHOD("add_module_file", "canonical_id", "path"),
                         &MecoPackageBuilder::add_module_file);
    ClassDB::bind_method(D_METHOD("resolve_import", "module_canonical_id",
                                  "authored_path", "target_canonical_id"),
                         &MecoPackageBuilder::resolve_import);
    ClassDB::bind_method(D_METHOD("compile", "root_canonical_id"),
                         &MecoPackageBuilder::compile, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_error_code"),
                         &MecoPackageBuilder::get_error_code);
    ClassDB::bind_method(D_METHOD("get_error_message"),
                         &MecoPackageBuilder::get_error_message);
}

} // namespace mecojoni
