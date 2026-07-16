/* SPDX-License-Identifier: MPL-2.0 */
#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "meco_grammar.h"
#include "meco_package_builder.h"

using namespace godot;

void initialize_mecojoni_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    ClassDB::register_class<mecojoni::MecoGrammar>();
    ClassDB::register_class<mecojoni::MecoPackageBuilder>();
}

void uninitialize_mecojoni_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

extern "C" {
GDExtensionBool GDE_EXPORT mecojoni_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization) {
    GDExtensionBinding::InitObject init_object(p_get_proc_address, p_library,
                                               r_initialization);
    init_object.register_initializer(initialize_mecojoni_module);
    init_object.register_terminator(uninitialize_mecojoni_module);
    init_object.set_minimum_library_initialization_level(
            MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_object.init();
}
}
