// Linux-only test interposer. Production code has no fault-injection switches.
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

namespace {
bool injected = false;

bool fail_once(const char* operation) {
    const auto* requested = std::getenv("NK_TEST_VULKAN_FAULT");
    if (!injected && requested && std::strcmp(requested, operation) == 0) {
        injected = true;
        return true;
    }
    return false;
}
} // namespace

extern "C" bool nk_vulkan_test_fault_injected() {
    return injected;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue,
                                                        uint32_t count,
                                                        const VkSubmitInfo* submissions,
                                                        VkFence fence) {
    if (fence != VK_NULL_HANDLE && count > 0 && submissions[0].commandBufferCount > 0 &&
        fail_once("submit")) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto next = reinterpret_cast<PFN_vkQueueSubmit>(dlsym(RTLD_NEXT, "vkQueueSubmit"));
    return next(queue, count, submissions, fence);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device,
                                                        uint32_t count,
                                                        const VkFence* fences) {
    if (fail_once("reset-fence")) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto next = reinterpret_cast<PFN_vkResetFences>(dlsym(RTLD_NEXT, "vkResetFences"));
    return next(device, count, fences);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer buffer) {
    if (fail_once("record")) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto next =
        reinterpret_cast<PFN_vkEndCommandBuffer>(dlsym(RTLD_NEXT, "vkEndCommandBuffer"));
    return next(buffer);
}
