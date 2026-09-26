#pragma once

#include <string>

namespace nk {
class Widget;
} // namespace nk

namespace nk::detail {

// Demangled dynamic type of a widget, e.g. "nk::ComboBox". Diagnostics and
// native accessibility bridges use it; it is not a stable public identifier.
std::string widget_type_name(const Widget& widget);

} // namespace nk::detail
