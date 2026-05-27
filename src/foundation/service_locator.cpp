#include <nk/foundation/service_locator.h>

namespace nk {

std::unordered_map<std::type_index, std::any>& ServiceLocator::services() {
    static std::unordered_map<std::type_index, std::any> instance;
    return instance;
}

std::mutex& ServiceLocator::mutex() {
    static std::mutex instance;
    return instance;
}

} // namespace nk