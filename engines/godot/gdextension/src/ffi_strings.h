/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <godot_cpp/variant/string.hpp>

#include "mecojoni.h"

namespace mecojoni {

/* Copies a borrowed C-ABI string view into a Godot String; absent views
 * become the empty String. */
inline godot::String view_to_string(MecoStrView view) {
    if (view.data == nullptr) {
        return godot::String();
    }
    return godot::String::utf8(view.data, static_cast<int64_t>(view.len));
}

} // namespace mecojoni
