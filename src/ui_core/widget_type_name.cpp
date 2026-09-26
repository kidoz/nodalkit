#include "widget_type_name.h"

#include <cstdlib>
#include <nk/ui_core/widget.h>
#include <typeinfo>

#if defined(__GNUG__)
#include <cxxabi.h>
#endif

namespace nk::detail {

std::string widget_type_name(const Widget& widget) {
#if defined(__GNUG__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(typeid(widget).name(), nullptr, nullptr, &status);
    std::string result = (status == 0 && demangled != nullptr) ? demangled : typeid(widget).name();
    std::free(demangled);
    return result;
#else
    return typeid(widget).name();
#endif
}

} // namespace nk::detail
