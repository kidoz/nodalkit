/// @file vulkan_renderer.cpp
/// @brief Experimental Vulkan renderer scaffold for Linux Wayland.

#include <nk/render/renderer.h>

#if defined(NK_HAVE_VULKAN) && defined(__linux__)

#define VK_USE_PLATFORM_WAYLAND_KHR

#include "vulkan_image_frag_spv.h"
#include "vulkan_image_vert_spv.h"
#include "vulkan_primitive_frag_spv.h"
#include "vulkan_primitive_vert_spv.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <nk/foundation/logging.h>
#include <nk/platform/platform_backend.h>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <nk/text/shaped_text.h>
#include <nk/text/text_shaper.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>
#include <wayland-client-core.h>

namespace nk {

namespace {

struct ClipRegion {
    Rect rect{};
    float radius = 0.0F;
};

struct PrimitiveCommand {
    Rect rect{};
    Color color{};
    float radius = 0.0F;
    float thickness = 0.0F;
    uint32_t kind = 0;
    std::array<ClipRegion, 2> clips{};
    uint32_t clip_count = 0;
};

struct ImageCommand {
    Rect rect{};
    const uint32_t* pixel_data = nullptr;
    int src_width = 0;
    int src_height = 0;
    ScaleMode scale_mode = ScaleMode::NearestNeighbor;
    std::array<ClipRegion, 2> clips{};
    uint32_t clip_count = 0;
};

struct TextCommand {
    Rect rect{};
    std::shared_ptr<ShapedText> shaped_text;
    std::array<ClipRegion, 2> clips{};
    uint32_t clip_count = 0;
};

enum class DrawCommandKind : uint8_t {
    Primitive,
    Image,
    Text,
};

struct DrawCommand {
    DrawCommandKind kind = DrawCommandKind::Primitive;
    std::size_t command_index = 0;
};

struct DrawPushConstants {
    float rect[4]{};
    float color[4]{};
    float clip_rects[8]{};
    float clip_radii[4]{};
    float params0[4]{};
    float viewport[4]{};
};

static_assert(sizeof(DrawPushConstants) == 112);

struct TextKey {
    std::string text;
    std::string family;
    float size = 0.0F;
    int weight = 0;
    int style = 0;
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 0.0F;
    float scale = 1.0F;

    bool operator==(const TextKey& other) const {
        return text == other.text && family == other.family && size == other.size &&
               weight == other.weight && style == other.style && r == other.r && g == other.g &&
               b == other.b && a == other.a && scale == other.scale;
    }
};

struct TextKeyHash {
    std::size_t operator()(const TextKey& key) const {
        std::size_t hash = std::hash<std::string>{}(key.text);
        hash ^= std::hash<std::string>{}(key.family) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.size) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.weight) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.style) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.r) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.g) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.b) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.a) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.scale) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

struct ImageTextureCacheKey {
    const uint32_t* pixel_data = nullptr;
    int width = 0;
    int height = 0;
    std::size_t content_hash = 0;

    bool operator==(const ImageTextureCacheKey& other) const {
        return pixel_data == other.pixel_data && width == other.width && height == other.height &&
               content_hash == other.content_hash;
    }
};

struct TextTextureCacheKey {
    const ShapedText* shaped_text = nullptr;
    const uint8_t* bitmap_data = nullptr;
    int width = 0;
    int height = 0;

    bool operator==(const TextTextureCacheKey& other) const {
        return shaped_text == other.shaped_text && bitmap_data == other.bitmap_data &&
               width == other.width && height == other.height;
    }
};

struct TextureCacheEntry {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkDescriptorSet nearest_descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet linear_descriptor_set = VK_NULL_HANDLE;
    uint64_t last_used_frame = 0;
};

struct ImageTextureCacheKeyHash {
    std::size_t operator()(const ImageTextureCacheKey& key) const {
        std::size_t hash = std::hash<const uint32_t*>{}(key.pixel_data);
        hash ^= std::hash<int>{}(key.width) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.height) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<std::size_t>{}(key.content_hash) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

struct TextTextureCacheKeyHash {
    std::size_t operator()(const TextTextureCacheKey& key) const {
        std::size_t hash = std::hash<const ShapedText*>{}(key.shaped_text);
        hash ^=
            std::hash<const uint8_t*>{}(key.bitmap_data) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.width) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.height) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

constexpr std::size_t kMaxClipDepth = 2;
constexpr uint64_t kTextureCacheMaxAgeFrames = 120;
constexpr std::size_t kImageTextureCacheMaxEntries = 128;
constexpr std::size_t kTextTextureCacheMaxEntries = 256;

float normalize_scale_factor(float scale_factor) {
    return std::isfinite(scale_factor) && scale_factor > 0.0F ? scale_factor : 1.0F;
}

Rect scale_rect(Rect rect, float scale_factor) {
    return {
        rect.x * scale_factor,
        rect.y * scale_factor,
        rect.width * scale_factor,
        rect.height * scale_factor,
    };
}

[[nodiscard]] bool rect_is_empty(Rect rect) {
    return rect.width <= 0.0F || rect.height <= 0.0F;
}

[[nodiscard]] bool rects_intersect(Rect lhs, Rect rhs) {
    return lhs.x < rhs.right() && lhs.right() > rhs.x && lhs.y < rhs.bottom() &&
           lhs.bottom() > rhs.y;
}

std::size_t scaled_pixel_area(Rect rect, float scale_factor) {
    if (rect.width <= 0.0F || rect.height <= 0.0F) {
        return 0;
    }
    const auto width = std::max(
        1, static_cast<int>(std::lround(rect.width * normalize_scale_factor(scale_factor))));
    const auto height = std::max(
        1, static_cast<int>(std::lround(rect.height * normalize_scale_factor(scale_factor))));
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

float effective_corner_radius(Rect rect, float requested_radius) {
    return std::clamp(requested_radius, 0.0F, std::min(rect.width, rect.height) * 0.5F);
}

std::size_t hash_bytes(const uint8_t* data, std::size_t size) {
    std::size_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::size_t>(data[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void populate_draw_push_constants(DrawPushConstants& push_constants,
                                  Rect rect,
                                  Color color,
                                  std::span<const ClipRegion> clips,
                                  uint32_t clip_count,
                                  float scale_factor,
                                  const VkExtent2D& viewport,
                                  float radius = 0.0F,
                                  float thickness = 0.0F,
                                  uint32_t kind = 0) {
    const Rect scaled_rect = scale_rect(rect, scale_factor);
    push_constants.rect[0] = scaled_rect.x;
    push_constants.rect[1] = scaled_rect.y;
    push_constants.rect[2] = scaled_rect.width;
    push_constants.rect[3] = scaled_rect.height;

    push_constants.color[0] = color.r;
    push_constants.color[1] = color.g;
    push_constants.color[2] = color.b;
    push_constants.color[3] = color.a;

    for (uint32_t index = 0; index < clip_count; ++index) {
        const Rect scaled_clip = scale_rect(clips[index].rect, scale_factor);
        push_constants.clip_rects[index * 4 + 0] = scaled_clip.x;
        push_constants.clip_rects[index * 4 + 1] = scaled_clip.y;
        push_constants.clip_rects[index * 4 + 2] = scaled_clip.width;
        push_constants.clip_rects[index * 4 + 3] = scaled_clip.height;
        push_constants.clip_radii[index] = clips[index].radius * scale_factor;
    }

    push_constants.params0[0] = radius * scale_factor;
    push_constants.params0[1] = thickness * scale_factor;
    push_constants.params0[2] = static_cast<float>(kind);
    push_constants.params0[3] = static_cast<float>(clip_count);
    push_constants.viewport[0] = static_cast<float>(viewport.width);
    push_constants.viewport[1] = static_cast<float>(viewport.height);
}

const char* vk_result_name(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    default:
        return "VK_ERROR_UNKNOWN";
    }
}

bool vk_ok(VkResult result, std::string_view step) {
    if (result == VK_SUCCESS) {
        return true;
    }

    std::string message = "Vulkan step failed: ";
    message += step;
    message += " (";
    message += vk_result_name(result);
    message += ")";
    NK_LOG_WARN("VulkanRenderer", message);
    return false;
}

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_UNORM ||
             format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM,
                                                VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                           : formats.front();
}

VkExtent2D choose_swapchain_extent(const VkSurfaceCapabilitiesKHR& capabilities,
                                   uint32_t desired_width,
                                   uint32_t desired_height) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    return {
        std::clamp(desired_width,
                   capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width),
        std::clamp(desired_height,
                   capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height),
    };
}

[[nodiscard]] bool has_extension(std::span<const VkExtensionProperties> extensions,
                                 const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [&](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

class VulkanRenderer final : public Renderer {
public:
    VulkanRenderer() : software_(std::make_unique<SoftwareRenderer>()) {}

    ~VulkanRenderer() override { destroy_context(); }

    [[nodiscard]] RendererBackend backend() const override { return RendererBackend::Vulkan; }

    bool attach_surface(NativeSurface& surface) override {
        (void)software_->attach_surface(surface);

        auto* display = static_cast<wl_display*>(surface.native_display_handle());
        auto* wl_surface = static_cast<struct wl_surface*>(surface.native_handle());
        if (display == nullptr || wl_surface == nullptr) {
            NK_LOG_WARN("VulkanRenderer",
                        "Wayland display or surface handle is missing; Vulkan cannot attach");
            destroy_context();
            return false;
        }

        if (ready_ && display == display_ && wl_surface == surface_) {
            return true;
        }

        destroy_context();

        constexpr const char* instance_extensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
        };

        const VkApplicationInfo application_info = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "NodalKit",
            .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
            .pEngineName = "NodalKit",
            .engineVersion = VK_MAKE_VERSION(0, 1, 0),
            .apiVersion = VK_API_VERSION_1_0,
        };

        const VkInstanceCreateInfo instance_info = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &application_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(std::size(instance_extensions)),
            .ppEnabledExtensionNames = instance_extensions,
        };

        if (!vk_ok(vkCreateInstance(&instance_info, nullptr, &instance_), "vkCreateInstance")) {
            destroy_context();
            return false;
        }

        const VkWaylandSurfaceCreateInfoKHR wayland_surface_info = {
            .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .display = display,
            .surface = wl_surface,
        };

        if (!vk_ok(vkCreateWaylandSurfaceKHR(instance_, &wayland_surface_info, nullptr, &vk_surface_),
                   "vkCreateWaylandSurfaceKHR")) {
            destroy_context();
            return false;
        }

        uint32_t physical_device_count = 0;
        if (!vk_ok(vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr),
                   "vkEnumeratePhysicalDevices(count)") ||
            physical_device_count == 0) {
            NK_LOG_WARN("VulkanRenderer", "No Vulkan physical devices were reported");
            destroy_context();
            return false;
        }

        std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
        if (!vk_ok(vkEnumeratePhysicalDevices(instance_, &physical_device_count, physical_devices.data()),
                   "vkEnumeratePhysicalDevices(list)")) {
            destroy_context();
            return false;
        }

        for (const auto candidate : physical_devices) {
            uint32_t queue_family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_family_count, nullptr);
            if (queue_family_count == 0) {
                continue;
            }

            std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate,
                                                     &queue_family_count,
                                                     queue_families.data());

            for (uint32_t queue_family_index = 0; queue_family_index < queue_family_count;
                 ++queue_family_index) {
                if ((queue_families[queue_family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                    continue;
                }

                VkBool32 supports_present = VK_FALSE;
                if (!vk_ok(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,
                                                               queue_family_index,
                                                               vk_surface_,
                                                               &supports_present),
                           "vkGetPhysicalDeviceSurfaceSupportKHR")) {
                    continue;
                }
                if (supports_present == VK_FALSE) {
                    continue;
                }

                physical_device_ = candidate;
                queue_family_index_ = queue_family_index;
                break;
            }

            if (physical_device_ != VK_NULL_HANDLE) {
                break;
            }
        }

        if (physical_device_ == VK_NULL_HANDLE) {
            NK_LOG_WARN("VulkanRenderer",
                        "No Vulkan device with graphics and Wayland present support was found");
            destroy_context();
            return false;
        }

        uint32_t device_extension_count = 0;
        if (!vk_ok(vkEnumerateDeviceExtensionProperties(physical_device_,
                                                        nullptr,
                                                        &device_extension_count,
                                                        nullptr),
                   "vkEnumerateDeviceExtensionProperties(count)")) {
            destroy_context();
            return false;
        }
        std::vector<VkExtensionProperties> available_device_extensions(device_extension_count);
        if (device_extension_count > 0 &&
            !vk_ok(vkEnumerateDeviceExtensionProperties(physical_device_,
                                                       nullptr,
                                                       &device_extension_count,
                                                       available_device_extensions.data()),
                   "vkEnumerateDeviceExtensionProperties(list)")) {
            destroy_context();
            return false;
        }

        const float queue_priority = 1.0F;
        const VkDeviceQueueCreateInfo queue_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = queue_family_index_,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        };

        std::vector<const char*> enabled_device_extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };
        incremental_present_supported_ =
            has_extension(available_device_extensions, VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME);
        if (incremental_present_supported_) {
            enabled_device_extensions.push_back(VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME);
        }

        const VkDeviceCreateInfo device_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queue_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(enabled_device_extensions.size()),
            .ppEnabledExtensionNames = enabled_device_extensions.data(),
            .pEnabledFeatures = nullptr,
        };

        if (!vk_ok(vkCreateDevice(physical_device_, &device_info, nullptr, &device_),
                   "vkCreateDevice")) {
            destroy_context();
            return false;
        }

        vkGetDeviceQueue(device_, queue_family_index_, 0, &graphics_queue_);
        if (graphics_queue_ == VK_NULL_HANDLE) {
            NK_LOG_WARN("VulkanRenderer", "Failed to acquire a Vulkan graphics queue");
            destroy_context();
            return false;
        }

        if (!create_command_resources() || !create_sync_resources() ||
            !create_descriptor_resources()) {
            destroy_context();
            return false;
        }

        display_ = display;
        surface_ = wl_surface;
        ready_ = true;
        NK_LOG_INFO("VulkanRenderer", "Wayland Vulkan instance/device attach succeeded");
        return true;
    }

    void set_text_shaper(TextShaper* shaper) override {
        text_shaper_ = shaper;
        software_->set_text_shaper(shaper);
    }

    void set_damage_regions(std::span<const Rect> regions) override {
        damage_regions_.clear();
        full_redraw_ = true;

        for (const auto& region : regions) {
            Rect scaled = scale_rect(region, scale_factor_);
            if (rect_is_empty(scaled)) {
                continue;
            }
            damage_regions_.push_back(scaled);
        }

        if (!damage_regions_.empty()) {
            full_redraw_ = false;
        }
        last_hotspot_counters_.damage_region_count = damage_regions_.size();
    }

    void begin_frame(Size viewport, float scale_factor) override {
        logical_viewport_ = viewport;
        scale_factor_ = normalize_scale_factor(scale_factor);
        ++frame_serial_;
        software_->begin_frame(viewport, scale_factor_);
        trim_texture_cache(image_texture_cache_, kImageTextureCacheMaxEntries);
        trim_texture_cache(text_texture_cache_, kTextTextureCacheMaxEntries);
        draw_commands_.clear();
        primitive_commands_.clear();
        image_commands_.clear();
        text_commands_.clear();
        draw_textures_.clear();
        draw_descriptor_sets_.clear();
        active_clips_.clear();
        damage_regions_.clear();
        present_regions_.clear();
        last_hotspot_counters_ = {};
        last_root_ = nullptr;
        use_gpu_path_ = false;
        software_frame_rendered_ = false;
        software_frame_finalized_ = false;
        full_redraw_ = true;
    }

    void render(const RenderNode& root) override {
        draw_commands_.clear();
        primitive_commands_.clear();
        image_commands_.clear();
        text_commands_.clear();
        draw_textures_.clear();
        draw_descriptor_sets_.clear();
        active_clips_.clear();
        last_root_ = &root;

        if (collect_draw_commands(root)) {
            use_gpu_path_ = true;
            last_hotspot_counters_.gpu_estimated_draw_pixel_count =
                estimate_full_redraw_draw_pixel_count();
            if (ready_ && !ensure_draw_textures_ready()) {
                use_gpu_path_ = false;
                draw_textures_.clear();
                draw_descriptor_sets_.clear();
                software_->render(root);
                software_frame_rendered_ = true;
                const auto preserved = last_hotspot_counters_;
                auto counters = software_->last_hotspot_counters();
                counters.damage_region_count = preserved.damage_region_count;
                counters.gpu_draw_call_count = preserved.gpu_draw_call_count;
                counters.gpu_present_region_count = preserved.gpu_present_region_count;
                counters.gpu_swapchain_copy_count = preserved.gpu_swapchain_copy_count;
                counters.gpu_estimated_draw_pixel_count =
                    preserved.gpu_estimated_draw_pixel_count;
                counters.gpu_present_path = preserved.gpu_present_path;
                last_hotspot_counters_ = counters;
            } else {
                last_hotspot_counters_.gpu_draw_call_count = estimate_gpu_draw_call_count();
            }
            return;
        }

        use_gpu_path_ = false;
        software_->render(root);
        software_frame_rendered_ = true;
        auto counters = software_->last_hotspot_counters();
        counters.damage_region_count = last_hotspot_counters_.damage_region_count;
        last_hotspot_counters_ = counters;
    }

    void end_frame() override {
        if (software_frame_rendered_ && !software_frame_finalized_) {
            software_->end_frame();
            software_frame_finalized_ = true;
        }
    }

    void present(NativeSurface& surface) override {
        if (!attach_surface(surface) || !ensure_swapchain()) {
            present_software_direct(surface);
            return;
        }

        if (!vk_ok(vkWaitForFences(device_, 1, &in_flight_fence_, VK_TRUE, UINT64_MAX),
                   "vkWaitForFences")) {
            present_software_direct(surface);
            return;
        }

        uint32_t image_index = 0;
        VkResult acquire = vkAcquireNextImageKHR(device_,
                                                 swapchain_,
                                                 UINT64_MAX,
                                                 image_available_semaphore_,
                                                 VK_NULL_HANDLE,
                                                 &image_index);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_swapchain_ = true;
            if (!ensure_swapchain()) {
                present_software_direct(surface);
                return;
            }
            acquire = vkAcquireNextImageKHR(device_,
                                            swapchain_,
                                            UINT64_MAX,
                                            image_available_semaphore_,
                                            VK_NULL_HANDLE,
                                            &image_index);
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            vk_ok(acquire, "vkAcquireNextImageKHR");
            present_software_direct(surface);
            return;
        }

        if (!vk_ok(vkResetFences(device_, 1, &in_flight_fence_), "vkResetFences")) {
            present_software_direct(surface);
            return;
        }

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        bool recorded = false;
        const bool full_redraw = full_redraw_ || !scene_initialized_ || damage_regions_.empty();
        const bool textures_ready =
            !use_gpu_path_ || draw_textures_.size() == draw_commands_.size() || ensure_draw_textures_ready();
        if (use_gpu_path_ && scene_framebuffer_ != VK_NULL_HANDLE &&
            primitive_pipeline_ != VK_NULL_HANDLE && image_pipeline_ != VK_NULL_HANDLE &&
            descriptor_pool_ != VK_NULL_HANDLE && descriptor_set_layout_ != VK_NULL_HANDLE &&
            textures_ready &&
            vk_ok(vkResetCommandPool(device_, command_pool_, 0), "vkResetCommandPool(gpu)")) {
            recorded = record_gpu_draw_commands(image_index);
            wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }

        if (!recorded) {
            if (!ensure_software_frame()) {
                return;
            }

            const auto* pixel_data = software_->pixel_data();
            const int pixel_width = software_->pixel_width();
            const int pixel_height = software_->pixel_height();
            if (pixel_data == nullptr || pixel_width <= 0 || pixel_height <= 0) {
                return;
            }

            if (!ensure_staging_buffer(static_cast<uint32_t>(pixel_width),
                                       static_cast<uint32_t>(pixel_height),
                                       swapchain_format_)) {
                present_software_direct(surface);
                return;
            }

            copy_pixels_to_staging(pixel_data,
                                   static_cast<uint32_t>(pixel_width),
                                   static_cast<uint32_t>(pixel_height));

            if (!vk_ok(vkResetCommandPool(device_, command_pool_, 0), "vkResetCommandPool(upload)") ||
                !record_software_upload_commands(image_index,
                                                 static_cast<uint32_t>(pixel_width),
                                                 static_cast<uint32_t>(pixel_height))) {
                present_software_direct(surface);
                return;
            }
            wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            last_hotspot_counters_.gpu_present_path = GpuPresentPath::SoftwareUpload;
        }

        present_regions_.clear();
        if (incremental_present_supported_ && recorded && !full_redraw) {
            present_regions_ = build_present_regions();
        }
        last_hotspot_counters_.gpu_present_region_count = present_regions_.size();

        VkPresentRegionKHR present_region = {
            .rectangleCount = static_cast<uint32_t>(present_regions_.size()),
            .pRectangles = present_regions_.data(),
        };
        VkPresentRegionsKHR present_regions_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR,
            .pNext = nullptr,
            .swapchainCount = 1,
            .pRegions = &present_region,
        };
        const void* present_pnext = present_regions_.empty() ? nullptr : &present_regions_info;

        const VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &image_available_semaphore_,
            .pWaitDstStageMask = &wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = &command_buffer_,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &render_finished_semaphore_,
        };

        if (!vk_ok(vkQueueSubmit(graphics_queue_, 1, &submit_info, in_flight_fence_),
                   "vkQueueSubmit")) {
            present_software_direct(surface);
            return;
        }

        const VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = present_pnext,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_finished_semaphore_,
            .swapchainCount = 1,
            .pSwapchains = &swapchain_,
            .pImageIndices = &image_index,
            .pResults = nullptr,
        };

        const VkResult present_result = vkQueuePresentKHR(graphics_queue_, &present_info);
        if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
            recreate_swapchain_ = true;
            return;
        }
        if (!vk_ok(present_result, "vkQueuePresentKHR")) {
            present_software_direct(surface);
        }
    }

    [[nodiscard]] RenderHotspotCounters last_hotspot_counters() const override {
        return last_hotspot_counters_;
    }

private:
    template <typename Map> void trim_texture_cache(Map& cache, std::size_t max_entries) {
        for (auto it = cache.begin(); it != cache.end();) {
            if ((frame_serial_ - it->second.last_used_frame) > kTextureCacheMaxAgeFrames) {
                destroy_texture_entry(it->second);
                it = cache.erase(it);
            } else {
                ++it;
            }
        }

        while (cache.size() > max_entries) {
            auto oldest = cache.begin();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->second.last_used_frame < oldest->second.last_used_frame) {
                    oldest = it;
                }
            }
            destroy_texture_entry(oldest->second);
            cache.erase(oldest);
        }
    }

    [[nodiscard]] Rect draw_command_bounds(const DrawCommand& draw_command) const {
        switch (draw_command.kind) {
        case DrawCommandKind::Primitive:
            return scale_rect(primitive_commands_[draw_command.command_index].rect, scale_factor_);
        case DrawCommandKind::Image:
            return scale_rect(image_commands_[draw_command.command_index].rect, scale_factor_);
        case DrawCommandKind::Text:
            return scale_rect(text_commands_[draw_command.command_index].rect, scale_factor_);
        }
        return {};
    }

    [[nodiscard]] std::size_t clipped_draw_pixel_area(Rect bounds) const {
        const uint32_t target_width =
            swapchain_extent_.width > 0 ? swapchain_extent_.width : desired_pixel_width();
        const uint32_t target_height =
            swapchain_extent_.height > 0 ? swapchain_extent_.height : desired_pixel_height();
        const Rect clipped = {
            std::max(0.0F, bounds.x),
            std::max(0.0F, bounds.y),
            std::min(bounds.right(), static_cast<float>(target_width)) -
                std::max(0.0F, bounds.x),
            std::min(bounds.bottom(), static_cast<float>(target_height)) -
                std::max(0.0F, bounds.y),
        };
        if (rect_is_empty(clipped)) {
            return 0;
        }
        return static_cast<std::size_t>(std::ceil(clipped.width)) *
               static_cast<std::size_t>(std::ceil(clipped.height));
    }

    [[nodiscard]] std::size_t estimate_full_redraw_draw_pixel_count() const {
        std::size_t pixel_count = 0;
        for (const auto& draw_command : draw_commands_) {
            pixel_count += clipped_draw_pixel_area(draw_command_bounds(draw_command));
        }
        return pixel_count;
    }

    [[nodiscard]] bool should_copy_back_full_redraw() const {
        const uint32_t target_width =
            swapchain_extent_.width > 0 ? swapchain_extent_.width : desired_pixel_width();
        const uint32_t target_height =
            swapchain_extent_.height > 0 ? swapchain_extent_.height : desired_pixel_height();
        const std::size_t viewport_pixels = static_cast<std::size_t>(target_width) *
                                            static_cast<std::size_t>(target_height);
        if (viewport_pixels == 0) {
            return false;
        }

        const std::size_t draw_pixels = estimate_full_redraw_draw_pixel_count();
        return draw_pixels >= viewport_pixels;
    }

    [[nodiscard]] std::size_t estimate_gpu_draw_call_count() const {
        const bool full_redraw = full_redraw_ || !scene_initialized_ || damage_regions_.empty();
        std::size_t draw_call_count = 0;
        for (const auto& draw_command : draw_commands_) {
            const Rect bounds = draw_command_bounds(draw_command);
            if (rect_is_empty(bounds)) {
                continue;
            }
            if (full_redraw) {
                ++draw_call_count;
                continue;
            }
            for (const auto& damage : damage_regions_) {
                if (rects_intersect(bounds, damage)) {
                    ++draw_call_count;
                }
            }
        }
        return draw_call_count;
    }

    [[nodiscard]] std::vector<VkRectLayerKHR> build_present_regions() const {
        std::vector<VkRectLayerKHR> regions;
        regions.reserve(damage_regions_.size());
        for (const auto& damage : damage_regions_) {
            const Rect clipped = {
                std::max(0.0F, damage.x),
                std::max(0.0F, damage.y),
                std::min(damage.right(), static_cast<float>(swapchain_extent_.width)) -
                    std::max(0.0F, damage.x),
                std::min(damage.bottom(), static_cast<float>(swapchain_extent_.height)) -
                    std::max(0.0F, damage.y),
            };
            if (rect_is_empty(clipped)) {
                continue;
            }
            regions.push_back({
                .offset = {static_cast<int32_t>(std::floor(clipped.x)),
                           static_cast<int32_t>(std::floor(clipped.y))},
                .extent = {static_cast<uint32_t>(std::ceil(clipped.width)),
                           static_cast<uint32_t>(std::ceil(clipped.height))},
                .layer = 0,
            });
        }
        return regions;
    }

    [[nodiscard]] std::size_t record_draw_commands_for_target(VkFramebuffer framebuffer,
                                                              bool full_redraw) {
        const VkClearColorValue clear_color = {.float32 = {1.0F, 1.0F, 1.0F, 1.0F}};
        const VkClearValue clear_value = {.color = clear_color};
        const VkRenderPassBeginInfo render_pass_begin_info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = render_pass_,
            .framebuffer = framebuffer,
            .renderArea =
                {
                    .offset = {0, 0},
                    .extent = swapchain_extent_,
                },
            .clearValueCount = 1,
            .pClearValues = &clear_value,
        };

        vkCmdBeginRenderPass(command_buffer_, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        const VkViewport viewport = {
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(swapchain_extent_.width),
            .height = static_cast<float>(swapchain_extent_.height),
            .minDepth = 0.0F,
            .maxDepth = 1.0F,
        };
        const VkRect2D scissor = {
            .offset = {0, 0},
            .extent = swapchain_extent_,
        };
        vkCmdSetViewport(command_buffer_, 0, 1, &viewport);
        vkCmdSetScissor(command_buffer_, 0, 1, &scissor);

        DrawCommandKind active_kind = DrawCommandKind::Primitive;
        bool pipeline_bound = false;
        std::size_t gpu_draw_call_count = 0;

        for (std::size_t draw_index = 0; draw_index < draw_commands_.size(); ++draw_index) {
            const auto& draw_command = draw_commands_[draw_index];
            Rect command_bounds{};

            if (draw_command.kind == DrawCommandKind::Primitive) {
                if (!pipeline_bound || active_kind != DrawCommandKind::Primitive) {
                    vkCmdBindPipeline(command_buffer_,
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      primitive_pipeline_);
                    active_kind = DrawCommandKind::Primitive;
                    pipeline_bound = true;
                }

                const auto& command = primitive_commands_[draw_command.command_index];
                command_bounds = scale_rect(command.rect, scale_factor_);
                DrawPushConstants push_constants{};
                populate_draw_push_constants(push_constants,
                                             command.rect,
                                             command.color,
                                             std::span<const ClipRegion>(command.clips.data(),
                                                                         command.clip_count),
                                             command.clip_count,
                                             scale_factor_,
                                             swapchain_extent_,
                                             command.radius,
                                             command.thickness,
                                             command.kind);
                if (full_redraw) {
                    vkCmdSetScissor(command_buffer_, 0, 1, &scissor);
                    vkCmdPushConstants(command_buffer_,
                                       pipeline_layout_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0,
                                       sizeof(DrawPushConstants),
                                       &push_constants);
                    vkCmdDraw(command_buffer_, 4, 1, 0, 0);
                    ++gpu_draw_call_count;
                } else {
                    for (const auto& damage : damage_regions_) {
                        if (!rects_intersect(command_bounds, damage)) {
                            continue;
                        }
                        const Rect clipped = {
                            std::max(0.0F, damage.x),
                            std::max(0.0F, damage.y),
                            std::min(damage.right(), static_cast<float>(swapchain_extent_.width)) -
                                std::max(0.0F, damage.x),
                            std::min(damage.bottom(), static_cast<float>(swapchain_extent_.height)) -
                                std::max(0.0F, damage.y),
                        };
                        if (rect_is_empty(clipped)) {
                            continue;
                        }
                        const VkRect2D damage_scissor = {
                            .offset = {static_cast<int32_t>(std::floor(clipped.x)),
                                       static_cast<int32_t>(std::floor(clipped.y))},
                            .extent = {static_cast<uint32_t>(std::ceil(clipped.width)),
                                       static_cast<uint32_t>(std::ceil(clipped.height))},
                        };
                        vkCmdSetScissor(command_buffer_, 0, 1, &damage_scissor);
                        vkCmdPushConstants(command_buffer_,
                                           pipeline_layout_,
                                           VK_SHADER_STAGE_VERTEX_BIT |
                                               VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0,
                                           sizeof(DrawPushConstants),
                                           &push_constants);
                        vkCmdDraw(command_buffer_, 4, 1, 0, 0);
                        ++gpu_draw_call_count;
                    }
                }
                continue;
            }

            if (!pipeline_bound || active_kind == DrawCommandKind::Primitive) {
                vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, image_pipeline_);
                active_kind = draw_command.kind;
                pipeline_bound = true;
            }

            const auto* texture_entry = draw_textures_[draw_index];
            if (texture_entry == nullptr || texture_entry->image_view == VK_NULL_HANDLE) {
                vkCmdEndRenderPass(command_buffer_);
                return 0;
            }

            const VkDescriptorSet descriptor_set = draw_descriptor_sets_[draw_index];
            if (descriptor_set == VK_NULL_HANDLE) {
                vkCmdEndRenderPass(command_buffer_);
                return 0;
            }
            vkCmdBindDescriptorSets(command_buffer_,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_,
                                    0,
                                    1,
                                    &descriptor_set,
                                    0,
                                    nullptr);

            DrawPushConstants push_constants{};
            if (draw_command.kind == DrawCommandKind::Image) {
                const auto& command = image_commands_[draw_command.command_index];
                command_bounds = scale_rect(command.rect, scale_factor_);
                populate_draw_push_constants(push_constants,
                                             command.rect,
                                             {},
                                             std::span<const ClipRegion>(command.clips.data(),
                                                                         command.clip_count),
                                             command.clip_count,
                                             scale_factor_,
                                             swapchain_extent_);
            } else {
                const auto& command = text_commands_[draw_command.command_index];
                command_bounds = scale_rect(command.rect, scale_factor_);
                populate_draw_push_constants(push_constants,
                                             command.rect,
                                             {},
                                             std::span<const ClipRegion>(command.clips.data(),
                                                                         command.clip_count),
                                             command.clip_count,
                                             scale_factor_,
                                             swapchain_extent_);
            }

            if (full_redraw) {
                vkCmdSetScissor(command_buffer_, 0, 1, &scissor);
                vkCmdPushConstants(command_buffer_,
                                   pipeline_layout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(DrawPushConstants),
                                   &push_constants);
                vkCmdDraw(command_buffer_, 4, 1, 0, 0);
                ++gpu_draw_call_count;
            } else {
                for (const auto& damage : damage_regions_) {
                    if (!rects_intersect(command_bounds, damage)) {
                        continue;
                    }
                    const Rect clipped = {
                        std::max(0.0F, damage.x),
                        std::max(0.0F, damage.y),
                        std::min(damage.right(), static_cast<float>(swapchain_extent_.width)) -
                            std::max(0.0F, damage.x),
                        std::min(damage.bottom(), static_cast<float>(swapchain_extent_.height)) -
                            std::max(0.0F, damage.y),
                    };
                    if (rect_is_empty(clipped)) {
                        continue;
                    }
                    const VkRect2D damage_scissor = {
                        .offset = {static_cast<int32_t>(std::floor(clipped.x)),
                                   static_cast<int32_t>(std::floor(clipped.y))},
                        .extent = {static_cast<uint32_t>(std::ceil(clipped.width)),
                                   static_cast<uint32_t>(std::ceil(clipped.height))},
                    };
                    vkCmdSetScissor(command_buffer_, 0, 1, &damage_scissor);
                    vkCmdPushConstants(command_buffer_,
                                       pipeline_layout_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0,
                                       sizeof(DrawPushConstants),
                                       &push_constants);
                    vkCmdDraw(command_buffer_, 4, 1, 0, 0);
                    ++gpu_draw_call_count;
                }
            }
        }

        vkCmdEndRenderPass(command_buffer_);
        return gpu_draw_call_count;
    }

    [[nodiscard]] bool create_command_resources() {
        const VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queue_family_index_,
        };
        if (!vk_ok(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_),
                   "vkCreateCommandPool")) {
            return false;
        }

        const VkCommandBufferAllocateInfo command_buffer_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = command_pool_,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        return vk_ok(vkAllocateCommandBuffers(device_, &command_buffer_info, &command_buffer_),
                     "vkAllocateCommandBuffers");
    }

    [[nodiscard]] bool create_sync_resources() {
        const VkSemaphoreCreateInfo semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        };
        if (!vk_ok(vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_semaphore_),
                   "vkCreateSemaphore(image_available)") ||
            !vk_ok(vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_semaphore_),
                   "vkCreateSemaphore(render_finished)")) {
            return false;
        }

        const VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        return vk_ok(vkCreateFence(device_, &fence_info, nullptr, &in_flight_fence_),
                     "vkCreateFence");
    }

    [[nodiscard]] bool create_descriptor_resources() {
        const VkDescriptorSetLayoutBinding sampler_binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };

        const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = 1,
            .pBindings = &sampler_binding,
        };
        if (!vk_ok(vkCreateDescriptorSetLayout(device_,
                                               &descriptor_set_layout_info,
                                               nullptr,
                                               &descriptor_set_layout_),
                   "vkCreateDescriptorSetLayout")) {
            return false;
        }

        const VkDescriptorPoolSize pool_size = {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1024,
        };
        const VkDescriptorPoolCreateInfo descriptor_pool_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 1024,
            .poolSizeCount = 1,
            .pPoolSizes = &pool_size,
        };
        if (!vk_ok(vkCreateDescriptorPool(device_, &descriptor_pool_info, nullptr, &descriptor_pool_),
                   "vkCreateDescriptorPool")) {
            return false;
        }

        const VkSamplerCreateInfo nearest_sampler_info = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0F,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0F,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0F,
            .maxLod = 0.0F,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };
        if (!vk_ok(vkCreateSampler(device_, &nearest_sampler_info, nullptr, &nearest_sampler_),
                   "vkCreateSampler(nearest)")) {
            return false;
        }

        VkSamplerCreateInfo linear_sampler_info = nearest_sampler_info;
        linear_sampler_info.magFilter = VK_FILTER_LINEAR;
        linear_sampler_info.minFilter = VK_FILTER_LINEAR;
        linear_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        return vk_ok(vkCreateSampler(device_, &linear_sampler_info, nullptr, &linear_sampler_),
                     "vkCreateSampler(linear)");
    }

    [[nodiscard]] uint32_t desired_pixel_width() const {
        return std::max(1U,
                        static_cast<uint32_t>(std::lround(logical_viewport_.width *
                                                          normalize_scale_factor(scale_factor_))));
    }

    [[nodiscard]] uint32_t desired_pixel_height() const {
        return std::max(
            1U,
            static_cast<uint32_t>(std::lround(logical_viewport_.height *
                                              normalize_scale_factor(scale_factor_))));
    }

    void destroy_texture_entry(TextureCacheEntry& entry) {
        if (device_ == VK_NULL_HANDLE) {
            entry = {};
            return;
        }

        const VkDescriptorSet nearest_descriptor_set = entry.nearest_descriptor_set;
        const VkDescriptorSet linear_descriptor_set = entry.linear_descriptor_set;
        std::array<VkDescriptorSet, 2> descriptor_sets{};
        uint32_t descriptor_set_count = 0;
        if (nearest_descriptor_set != VK_NULL_HANDLE) {
            descriptor_sets[descriptor_set_count++] = nearest_descriptor_set;
        }
        if (linear_descriptor_set != VK_NULL_HANDLE && linear_descriptor_set != nearest_descriptor_set) {
            descriptor_sets[descriptor_set_count++] = linear_descriptor_set;
        }
        if (descriptor_pool_ != VK_NULL_HANDLE && descriptor_set_count > 0) {
            (void)vkFreeDescriptorSets(device_,
                                       descriptor_pool_,
                                       descriptor_set_count,
                                       descriptor_sets.data());
        }
        entry.nearest_descriptor_set = VK_NULL_HANDLE;
        entry.linear_descriptor_set = VK_NULL_HANDLE;

        if (entry.image_view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, entry.image_view, nullptr);
            entry.image_view = VK_NULL_HANDLE;
        }
        if (entry.image != VK_NULL_HANDLE) {
            vkDestroyImage(device_, entry.image, nullptr);
            entry.image = VK_NULL_HANDLE;
        }
        if (entry.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, entry.memory, nullptr);
            entry.memory = VK_NULL_HANDLE;
        }
        entry.last_used_frame = 0;
    }

    void destroy_texture_caches() {
        for (auto& [key, entry] : image_texture_cache_) {
            (void)key;
            destroy_texture_entry(entry);
        }
        image_texture_cache_.clear();

        for (auto& [key, entry] : text_texture_cache_) {
            (void)key;
            destroy_texture_entry(entry);
        }
        text_texture_cache_.clear();
        shaped_text_cache_.clear();
        draw_textures_.clear();
        draw_descriptor_sets_.clear();
        image_upload_buffer_.clear();
    }

    void destroy_render_targets() {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }

        if (scene_framebuffer_ != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, scene_framebuffer_, nullptr);
            scene_framebuffer_ = VK_NULL_HANDLE;
        }
        if (scene_image_view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, scene_image_view_, nullptr);
            scene_image_view_ = VK_NULL_HANDLE;
        }
        if (scene_image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, scene_image_, nullptr);
            scene_image_ = VK_NULL_HANDLE;
        }
        if (scene_image_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, scene_image_memory_, nullptr);
            scene_image_memory_ = VK_NULL_HANDLE;
        }
        scene_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        scene_initialized_ = false;

        for (auto framebuffer : swapchain_framebuffers_) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device_, framebuffer, nullptr);
            }
        }
        swapchain_framebuffers_.clear();

        for (auto image_view : swapchain_image_views_) {
            if (image_view != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, image_view, nullptr);
            }
        }
        swapchain_image_views_.clear();

        if (image_pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, image_pipeline_, nullptr);
            image_pipeline_ = VK_NULL_HANDLE;
        }
        if (primitive_pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, primitive_pipeline_, nullptr);
            primitive_pipeline_ = VK_NULL_HANDLE;
        }
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
        }
        if (render_pass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, render_pass_, nullptr);
            render_pass_ = VK_NULL_HANDLE;
        }
    }

    void destroy_swapchain() {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }

        destroy_render_targets();

        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
        swapchain_images_.clear();
        swapchain_extent_ = {};
        swapchain_format_ = VK_FORMAT_UNDEFINED;
        recreate_swapchain_ = false;
    }

    void destroy_staging_buffer() {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }

        if (staging_buffer_memory_ != VK_NULL_HANDLE) {
            if (staging_mapped_ != nullptr) {
                vkUnmapMemory(device_, staging_buffer_memory_);
                staging_mapped_ = nullptr;
            }
            vkFreeMemory(device_, staging_buffer_memory_, nullptr);
            staging_buffer_memory_ = VK_NULL_HANDLE;
        }
        if (staging_buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, staging_buffer_, nullptr);
            staging_buffer_ = VK_NULL_HANDLE;
        }
        staging_buffer_size_ = 0;
        converted_pixels_.clear();
    }

    [[nodiscard]] bool create_shader_module(const uint32_t* code,
                                            std::size_t size,
                                            VkShaderModule& shader_module) {
        const VkShaderModuleCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = size,
            .pCode = code,
        };
        return vk_ok(vkCreateShaderModule(device_, &create_info, nullptr, &shader_module),
                     "vkCreateShaderModule");
    }

    [[nodiscard]] bool create_pipeline(const uint32_t* vertex_spv,
                                       std::size_t vertex_spv_size,
                                       const uint32_t* fragment_spv,
                                       std::size_t fragment_spv_size,
                                       VkPipeline& pipeline) {
        VkShaderModule vertex_shader = VK_NULL_HANDLE;
        VkShaderModule fragment_shader = VK_NULL_HANDLE;
        if (!create_shader_module(vertex_spv, vertex_spv_size, vertex_shader) ||
            !create_shader_module(fragment_spv, fragment_spv_size, fragment_shader)) {
            if (vertex_shader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device_, vertex_shader, nullptr);
            }
            if (fragment_shader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device_, fragment_shader, nullptr);
            }
            return false;
        }

        const VkPipelineShaderStageCreateInfo shader_stages[] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertex_shader,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragment_shader,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
        };

        const VkPipelineVertexInputStateCreateInfo vertex_input_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 0,
            .pVertexBindingDescriptions = nullptr,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions = nullptr,
        };

        const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            .primitiveRestartEnable = VK_FALSE,
        };

        const VkPipelineViewportStateCreateInfo viewport_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = nullptr,
            .scissorCount = 1,
            .pScissors = nullptr,
        };

        const VkPipelineRasterizationStateCreateInfo rasterization_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0F,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = 0.0F,
            .lineWidth = 1.0F,
        };

        const VkPipelineMultisampleStateCreateInfo multisample_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };

        const VkPipelineColorBlendAttachmentState color_blend_attachment = {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };

        const VkPipelineColorBlendStateCreateInfo color_blend_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 1,
            .pAttachments = &color_blend_attachment,
            .blendConstants = {0.0F, 0.0F, 0.0F, 0.0F},
        };

        constexpr VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        const VkPipelineDynamicStateCreateInfo dynamic_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = static_cast<uint32_t>(std::size(dynamic_states)),
            .pDynamicStates = dynamic_states,
        };

        const VkGraphicsPipelineCreateInfo pipeline_info = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stageCount = static_cast<uint32_t>(std::size(shader_stages)),
            .pStages = shader_stages,
            .pVertexInputState = &vertex_input_info,
            .pInputAssemblyState = &input_assembly,
            .pTessellationState = nullptr,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization_state,
            .pMultisampleState = &multisample_state,
            .pDepthStencilState = nullptr,
            .pColorBlendState = &color_blend_state,
            .pDynamicState = &dynamic_state,
            .layout = pipeline_layout_,
            .renderPass = render_pass_,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };

        const bool pipeline_ok =
            vk_ok(vkCreateGraphicsPipelines(device_,
                                            VK_NULL_HANDLE,
                                            1,
                                            &pipeline_info,
                                            nullptr,
                                            &pipeline),
                  "vkCreateGraphicsPipelines");
        vkDestroyShaderModule(device_, fragment_shader, nullptr);
        vkDestroyShaderModule(device_, vertex_shader, nullptr);
        return pipeline_ok;
    }

    [[nodiscard]] bool create_render_targets() {
        destroy_render_targets();

        const VkAttachmentDescription color_attachment = {
            .flags = 0,
            .format = swapchain_format_,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        const VkAttachmentReference color_attachment_reference = {
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        const VkSubpassDescription subpass = {
            .flags = 0,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = 0,
            .pInputAttachments = nullptr,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment_reference,
            .pResolveAttachments = nullptr,
            .pDepthStencilAttachment = nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = nullptr,
        };

        const VkSubpassDependency dependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0,
        };

        const VkRenderPassCreateInfo render_pass_info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .attachmentCount = 1,
            .pAttachments = &color_attachment,
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 1,
            .pDependencies = &dependency,
        };
        if (!vk_ok(vkCreateRenderPass(device_, &render_pass_info, nullptr, &render_pass_),
                   "vkCreateRenderPass")) {
            destroy_render_targets();
            return false;
        }

        const VkPushConstantRange push_constant_range = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(DrawPushConstants),
        };
        const VkPipelineLayoutCreateInfo pipeline_layout_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &descriptor_set_layout_,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range,
        };
        if (!vk_ok(vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout_),
                   "vkCreatePipelineLayout")) {
            destroy_render_targets();
            return false;
        }

        if (!create_pipeline(nk_vulkan_primitive_vert_spv,
                             sizeof(nk_vulkan_primitive_vert_spv),
                             nk_vulkan_primitive_frag_spv,
                             sizeof(nk_vulkan_primitive_frag_spv),
                             primitive_pipeline_) ||
            !create_pipeline(nk_vulkan_image_vert_spv,
                             sizeof(nk_vulkan_image_vert_spv),
                             nk_vulkan_image_frag_spv,
                             sizeof(nk_vulkan_image_frag_spv),
                             image_pipeline_)) {
            destroy_render_targets();
            return false;
        }

        swapchain_image_views_.reserve(swapchain_images_.size());
        swapchain_framebuffers_.reserve(swapchain_images_.size());
        for (auto image : swapchain_images_) {
            VkImageView image_view = VK_NULL_HANDLE;
            const VkImageViewCreateInfo image_view_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = swapchain_format_,
                .components =
                    {
                        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                        .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                    },
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            if (!vk_ok(vkCreateImageView(device_, &image_view_info, nullptr, &image_view),
                       "vkCreateImageView(swapchain)")) {
                destroy_render_targets();
                return false;
            }
            swapchain_image_views_.push_back(image_view);

            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            const VkFramebufferCreateInfo framebuffer_info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .renderPass = render_pass_,
                .attachmentCount = 1,
                .pAttachments = &swapchain_image_views_.back(),
                .width = swapchain_extent_.width,
                .height = swapchain_extent_.height,
                .layers = 1,
            };
            if (!vk_ok(vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &framebuffer),
                       "vkCreateFramebuffer(swapchain)")) {
                destroy_render_targets();
                return false;
            }
            swapchain_framebuffers_.push_back(framebuffer);
        }

        const VkImageCreateInfo scene_image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = swapchain_format_,
            .extent = {swapchain_extent_.width, swapchain_extent_.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        if (!vk_ok(vkCreateImage(device_, &scene_image_info, nullptr, &scene_image_),
                   "vkCreateImage(scene)")) {
            destroy_render_targets();
            return false;
        }

        VkMemoryRequirements scene_memory_requirements{};
        vkGetImageMemoryRequirements(device_, scene_image_, &scene_memory_requirements);
        const uint32_t scene_memory_type =
            find_memory_type(scene_memory_requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (scene_memory_type == std::numeric_limits<uint32_t>::max()) {
            NK_LOG_WARN("VulkanRenderer", "No device-local memory type available for scene image");
            destroy_render_targets();
            return false;
        }

        const VkMemoryAllocateInfo scene_allocate_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = scene_memory_requirements.size,
            .memoryTypeIndex = scene_memory_type,
        };
        if (!vk_ok(vkAllocateMemory(device_, &scene_allocate_info, nullptr, &scene_image_memory_),
                   "vkAllocateMemory(scene)")) {
            destroy_render_targets();
            return false;
        }
        if (!vk_ok(vkBindImageMemory(device_, scene_image_, scene_image_memory_, 0),
                   "vkBindImageMemory(scene)")) {
            destroy_render_targets();
            return false;
        }

        const VkImageViewCreateInfo scene_image_view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = scene_image_,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain_format_,
            .components =
                {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        if (!vk_ok(vkCreateImageView(device_, &scene_image_view_info, nullptr, &scene_image_view_),
                   "vkCreateImageView(scene)")) {
            destroy_render_targets();
            return false;
        }

        const VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderPass = render_pass_,
            .attachmentCount = 1,
            .pAttachments = &scene_image_view_,
            .width = swapchain_extent_.width,
            .height = swapchain_extent_.height,
            .layers = 1,
        };
        if (!vk_ok(vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &scene_framebuffer_),
                   "vkCreateFramebuffer(scene)")) {
            destroy_render_targets();
            return false;
        }

        return true;
    }

    [[nodiscard]] bool ensure_swapchain() {
        if (!ready_ || device_ == VK_NULL_HANDLE || vk_surface_ == VK_NULL_HANDLE) {
            return false;
        }

        const uint32_t desired_width = desired_pixel_width();
        const uint32_t desired_height = desired_pixel_height();
        if (!recreate_swapchain_ && swapchain_ != VK_NULL_HANDLE &&
            swapchain_extent_.width == desired_width && swapchain_extent_.height == desired_height) {
            return true;
        }

        if (!vk_ok(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(recreate_swapchain)")) {
            return false;
        }

        VkSurfaceCapabilitiesKHR capabilities{};
        if (!vk_ok(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_,
                                                             vk_surface_,
                                                             &capabilities),
                   "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
            return false;
        }

        uint32_t format_count = 0;
        if (!vk_ok(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_,
                                                        vk_surface_,
                                                        &format_count,
                                                        nullptr),
                   "vkGetPhysicalDeviceSurfaceFormatsKHR(count)") ||
            format_count == 0) {
            NK_LOG_WARN("VulkanRenderer", "Wayland surface reported no Vulkan swapchain formats");
            return false;
        }

        std::vector<VkSurfaceFormatKHR> formats(format_count);
        if (!vk_ok(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_,
                                                        vk_surface_,
                                                        &format_count,
                                                        formats.data()),
                   "vkGetPhysicalDeviceSurfaceFormatsKHR(list)")) {
            return false;
        }

        uint32_t present_mode_count = 0;
        if (!vk_ok(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_,
                                                             vk_surface_,
                                                             &present_mode_count,
                                                             nullptr),
                   "vkGetPhysicalDeviceSurfacePresentModesKHR(count)") ||
            present_mode_count == 0) {
            NK_LOG_WARN("VulkanRenderer", "Wayland surface reported no Vulkan present modes");
            return false;
        }

        std::vector<VkPresentModeKHR> present_modes(present_mode_count);
        if (!vk_ok(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_,
                                                             vk_surface_,
                                                             &present_mode_count,
                                                             present_modes.data()),
                   "vkGetPhysicalDeviceSurfacePresentModesKHR(list)")) {
            return false;
        }

        const auto chosen_format = choose_surface_format(formats);
        VkPresentModeKHR chosen_present_mode = VK_PRESENT_MODE_FIFO_KHR;
        if (std::find(present_modes.begin(), present_modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) !=
            present_modes.end()) {
            chosen_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        }
        const auto chosen_extent = choose_swapchain_extent(capabilities, desired_width, desired_height);

        uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0) {
            image_count = std::min(image_count, capabilities.maxImageCount);
        }

        destroy_swapchain();

        const VkSwapchainCreateInfoKHR create_info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = vk_surface_,
            .minImageCount = image_count,
            .imageFormat = chosen_format.format,
            .imageColorSpace = chosen_format.colorSpace,
            .imageExtent = chosen_extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = chosen_present_mode,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };

        if (!vk_ok(vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_),
                   "vkCreateSwapchainKHR")) {
            return false;
        }

        uint32_t swapchain_image_count = 0;
        if (!vk_ok(vkGetSwapchainImagesKHR(device_, swapchain_, &swapchain_image_count, nullptr),
                   "vkGetSwapchainImagesKHR(count)") ||
            swapchain_image_count == 0) {
            destroy_swapchain();
            return false;
        }

        swapchain_images_.resize(swapchain_image_count);
        if (!vk_ok(vkGetSwapchainImagesKHR(device_,
                                           swapchain_,
                                           &swapchain_image_count,
                                           swapchain_images_.data()),
                   "vkGetSwapchainImagesKHR(list)")) {
            destroy_swapchain();
            return false;
        }

        swapchain_format_ = chosen_format.format;
        swapchain_extent_ = chosen_extent;
        recreate_swapchain_ = false;
        if (!create_render_targets()) {
            destroy_swapchain();
            return false;
        }
        return true;
    }

    [[nodiscard]] uint32_t find_memory_type(uint32_t type_filter,
                                            VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memory_properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);
        for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool type_matches = (type_filter & (1U << index)) != 0;
            const bool property_matches =
                (memory_properties.memoryTypes[index].propertyFlags & properties) == properties;
            if (type_matches && property_matches) {
                return index;
            }
        }
        return std::numeric_limits<uint32_t>::max();
    }

    [[nodiscard]] bool ensure_staging_buffer(uint32_t width, uint32_t height, VkFormat format) {
        const VkDeviceSize required_size =
            static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
        const bool need_recreate = staging_buffer_ == VK_NULL_HANDLE ||
                                   staging_buffer_size_ != required_size ||
                                   staging_format_ != format;
        if (!need_recreate) {
            return true;
        }

        destroy_staging_buffer();

        const VkBufferCreateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = required_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };

        if (!vk_ok(vkCreateBuffer(device_, &buffer_info, nullptr, &staging_buffer_),
                   "vkCreateBuffer(staging)")) {
            return false;
        }

        VkMemoryRequirements memory_requirements{};
        vkGetBufferMemoryRequirements(device_, staging_buffer_, &memory_requirements);

        const uint32_t memory_type = find_memory_type(memory_requirements.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memory_type == std::numeric_limits<uint32_t>::max()) {
            NK_LOG_WARN("VulkanRenderer", "No suitable Vulkan memory type was found for staging");
            destroy_staging_buffer();
            return false;
        }

        const VkMemoryAllocateInfo allocate_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memory_requirements.size,
            .memoryTypeIndex = memory_type,
        };

        if (!vk_ok(vkAllocateMemory(device_, &allocate_info, nullptr, &staging_buffer_memory_),
                   "vkAllocateMemory(staging)")) {
            destroy_staging_buffer();
            return false;
        }

        if (!vk_ok(vkBindBufferMemory(device_, staging_buffer_, staging_buffer_memory_, 0),
                   "vkBindBufferMemory(staging)")) {
            destroy_staging_buffer();
            return false;
        }

        if (!vk_ok(vkMapMemory(device_,
                               staging_buffer_memory_,
                               0,
                               required_size,
                               0,
                               reinterpret_cast<void**>(&staging_mapped_)),
                   "vkMapMemory(staging)")) {
            destroy_staging_buffer();
            return false;
        }

        staging_buffer_size_ = required_size;
        staging_format_ = format;
        return true;
    }

    void copy_pixels_to_staging(const uint8_t* rgba, uint32_t width, uint32_t height) {
        const std::size_t pixel_bytes =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        if (staging_format_ == VK_FORMAT_B8G8R8A8_UNORM) {
            converted_pixels_.resize(pixel_bytes);
            for (std::size_t offset = 0; offset < pixel_bytes; offset += 4) {
                converted_pixels_[offset + 0] = rgba[offset + 2];
                converted_pixels_[offset + 1] = rgba[offset + 1];
                converted_pixels_[offset + 2] = rgba[offset + 0];
                converted_pixels_[offset + 3] = rgba[offset + 3];
            }
            std::memcpy(staging_mapped_, converted_pixels_.data(), pixel_bytes);
            return;
        }

        std::memcpy(staging_mapped_, rgba, pixel_bytes);
    }

    [[nodiscard]] bool create_upload_buffer(VkDeviceSize size,
                                            const uint8_t* data,
                                            VkBuffer& buffer,
                                            VkDeviceMemory& buffer_memory) {
        const VkBufferCreateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };
        if (!vk_ok(vkCreateBuffer(device_, &buffer_info, nullptr, &buffer),
                   "vkCreateBuffer(texture_upload)")) {
            return false;
        }

        VkMemoryRequirements memory_requirements{};
        vkGetBufferMemoryRequirements(device_, buffer, &memory_requirements);
        const uint32_t memory_type = find_memory_type(memory_requirements.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memory_type == std::numeric_limits<uint32_t>::max()) {
            NK_LOG_WARN("VulkanRenderer", "No upload memory type available for texture staging");
            vkDestroyBuffer(device_, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        const VkMemoryAllocateInfo allocate_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memory_requirements.size,
            .memoryTypeIndex = memory_type,
        };
        if (!vk_ok(vkAllocateMemory(device_, &allocate_info, nullptr, &buffer_memory),
                   "vkAllocateMemory(texture_upload)")) {
            vkDestroyBuffer(device_, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        if (!vk_ok(vkBindBufferMemory(device_, buffer, buffer_memory, 0),
                   "vkBindBufferMemory(texture_upload)")) {
            vkFreeMemory(device_, buffer_memory, nullptr);
            buffer_memory = VK_NULL_HANDLE;
            vkDestroyBuffer(device_, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        void* mapped = nullptr;
        if (!vk_ok(vkMapMemory(device_, buffer_memory, 0, size, 0, &mapped),
                   "vkMapMemory(texture_upload)")) {
            vkFreeMemory(device_, buffer_memory, nullptr);
            buffer_memory = VK_NULL_HANDLE;
            vkDestroyBuffer(device_, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        std::memcpy(mapped, data, static_cast<std::size_t>(size));
        vkUnmapMemory(device_, buffer_memory);
        return true;
    }

    [[nodiscard]] bool allocate_texture_image(uint32_t width,
                                              uint32_t height,
                                              TextureCacheEntry& entry) {
        const VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        if (!vk_ok(vkCreateImage(device_, &image_info, nullptr, &entry.image), "vkCreateImage")) {
            return false;
        }

        VkMemoryRequirements memory_requirements{};
        vkGetImageMemoryRequirements(device_, entry.image, &memory_requirements);
        const uint32_t memory_type =
            find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memory_type == std::numeric_limits<uint32_t>::max()) {
            NK_LOG_WARN("VulkanRenderer", "No device-local memory type available for texture image");
            destroy_texture_entry(entry);
            return false;
        }

        const VkMemoryAllocateInfo allocate_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memory_requirements.size,
            .memoryTypeIndex = memory_type,
        };
        if (!vk_ok(vkAllocateMemory(device_, &allocate_info, nullptr, &entry.memory),
                   "vkAllocateMemory(texture_image)")) {
            destroy_texture_entry(entry);
            return false;
        }

        if (!vk_ok(vkBindImageMemory(device_, entry.image, entry.memory, 0), "vkBindImageMemory")) {
            destroy_texture_entry(entry);
            return false;
        }

        const VkImageViewCreateInfo image_view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = entry.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .components =
                {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        if (!vk_ok(vkCreateImageView(device_, &image_view_info, nullptr, &entry.image_view),
                   "vkCreateImageView(texture)")) {
            destroy_texture_entry(entry);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool upload_texture_rgba(const uint8_t* rgba,
                                           uint32_t width,
                                           uint32_t height,
                                           TextureCacheEntry& entry) {
        const VkDeviceSize upload_size =
            static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
        VkBuffer upload_buffer = VK_NULL_HANDLE;
        VkDeviceMemory upload_memory = VK_NULL_HANDLE;
        if (!create_upload_buffer(upload_size, rgba, upload_buffer, upload_memory)) {
            return false;
        }

        if (!allocate_texture_image(width, height, entry)) {
            vkFreeMemory(device_, upload_memory, nullptr);
            vkDestroyBuffer(device_, upload_buffer, nullptr);
            return false;
        }

        VkCommandBuffer temp_command_buffer = VK_NULL_HANDLE;
        const VkCommandBufferAllocateInfo allocate_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = command_pool_,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (!vk_ok(vkAllocateCommandBuffers(device_, &allocate_info, &temp_command_buffer),
                   "vkAllocateCommandBuffers(texture_upload)")) {
            vkFreeMemory(device_, upload_memory, nullptr);
            vkDestroyBuffer(device_, upload_buffer, nullptr);
            destroy_texture_entry(entry);
            return false;
        }

        const VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };
        bool ok = vk_ok(vkBeginCommandBuffer(temp_command_buffer, &begin_info),
                        "vkBeginCommandBuffer(texture_upload)");
        if (ok) {
            const VkImageMemoryBarrier to_transfer = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = entry.image,
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(temp_command_buffer,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &to_transfer);

            const VkBufferImageCopy region = {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .imageOffset = {0, 0, 0},
                .imageExtent = {width, height, 1},
            };
            vkCmdCopyBufferToImage(temp_command_buffer,
                                   upload_buffer,
                                   entry.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1,
                                   &region);

            const VkImageMemoryBarrier to_shader_read = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = entry.image,
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(temp_command_buffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &to_shader_read);

            ok = vk_ok(vkEndCommandBuffer(temp_command_buffer), "vkEndCommandBuffer(texture_upload)");
        }

        if (ok) {
            const VkSubmitInfo submit_info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = nullptr,
                .waitSemaphoreCount = 0,
                .pWaitSemaphores = nullptr,
                .pWaitDstStageMask = nullptr,
                .commandBufferCount = 1,
                .pCommandBuffers = &temp_command_buffer,
                .signalSemaphoreCount = 0,
                .pSignalSemaphores = nullptr,
            };
            ok = vk_ok(vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE),
                       "vkQueueSubmit(texture_upload)") &&
                 vk_ok(vkQueueWaitIdle(graphics_queue_), "vkQueueWaitIdle(texture_upload)");
        }

        vkFreeCommandBuffers(device_, command_pool_, 1, &temp_command_buffer);
        vkFreeMemory(device_, upload_memory, nullptr);
        vkDestroyBuffer(device_, upload_buffer, nullptr);

        if (!ok) {
            destroy_texture_entry(entry);
        }
        return ok;
    }

    [[nodiscard]] TextureCacheEntry* upload_image_texture(const ImageCommand& command) {
        if (device_ == VK_NULL_HANDLE || command.pixel_data == nullptr || command.src_width <= 0 ||
            command.src_height <= 0) {
            return nullptr;
        }

        const std::size_t byte_count = static_cast<std::size_t>(command.src_width) *
                                       static_cast<std::size_t>(command.src_height) * 4;
        const ImageTextureCacheKey cache_key{
            .pixel_data = command.pixel_data,
            .width = command.src_width,
            .height = command.src_height,
            .content_hash =
                hash_bytes(reinterpret_cast<const uint8_t*>(command.pixel_data), byte_count),
        };
        if (auto it = image_texture_cache_.find(cache_key); it != image_texture_cache_.end()) {
            it->second.last_used_frame = frame_serial_;
            if (!ensure_texture_descriptor_sets(it->second, true)) {
                destroy_texture_entry(it->second);
                image_texture_cache_.erase(it);
                return nullptr;
            }
            return &it->second;
        }

        const std::size_t pixel_count = static_cast<std::size_t>(command.src_width) *
                                        static_cast<std::size_t>(command.src_height);
        image_upload_buffer_.resize(pixel_count * 4);
        for (std::size_t index = 0; index < pixel_count; ++index) {
            const uint32_t pixel = command.pixel_data[index];
            image_upload_buffer_[(index * 4) + 0] = static_cast<uint8_t>((pixel >> 16) & 0xFF);
            image_upload_buffer_[(index * 4) + 1] = static_cast<uint8_t>((pixel >> 8) & 0xFF);
            image_upload_buffer_[(index * 4) + 2] = static_cast<uint8_t>(pixel & 0xFF);
            image_upload_buffer_[(index * 4) + 3] = static_cast<uint8_t>((pixel >> 24) & 0xFF);
        }

        TextureCacheEntry entry{};
        if (!upload_texture_rgba(image_upload_buffer_.data(),
                                 static_cast<uint32_t>(command.src_width),
                                 static_cast<uint32_t>(command.src_height),
                                 entry)) {
            return nullptr;
        }

        ++last_hotspot_counters_.image_texture_upload_count;
        entry.last_used_frame = frame_serial_;
        if (!ensure_texture_descriptor_sets(entry, true)) {
            destroy_texture_entry(entry);
            return nullptr;
        }
        auto [it, inserted] = image_texture_cache_.emplace(cache_key, entry);
        if (!inserted) {
            destroy_texture_entry(entry);
            it->second.last_used_frame = frame_serial_;
        }
        return &it->second;
    }

    [[nodiscard]] TextureCacheEntry* upload_text_texture(const TextCommand& command) {
        if (device_ == VK_NULL_HANDLE || command.shaped_text == nullptr ||
            command.shaped_text->bitmap_data() == nullptr ||
            command.shaped_text->bitmap_width() <= 0 || command.shaped_text->bitmap_height() <= 0) {
            return nullptr;
        }

        const TextTextureCacheKey cache_key{
            .shaped_text = command.shaped_text.get(),
            .bitmap_data = command.shaped_text->bitmap_data(),
            .width = command.shaped_text->bitmap_width(),
            .height = command.shaped_text->bitmap_height(),
        };
        if (auto it = text_texture_cache_.find(cache_key); it != text_texture_cache_.end()) {
            it->second.last_used_frame = frame_serial_;
            if (!ensure_texture_descriptor_sets(it->second, false)) {
                destroy_texture_entry(it->second);
                text_texture_cache_.erase(it);
                return nullptr;
            }
            return &it->second;
        }

        TextureCacheEntry entry{};
        if (!upload_texture_rgba(command.shaped_text->bitmap_data(),
                                 static_cast<uint32_t>(command.shaped_text->bitmap_width()),
                                 static_cast<uint32_t>(command.shaped_text->bitmap_height()),
                                 entry)) {
            return nullptr;
        }

        ++last_hotspot_counters_.text_texture_upload_count;
        entry.last_used_frame = frame_serial_;
        if (!ensure_texture_descriptor_sets(entry, false)) {
            destroy_texture_entry(entry);
            return nullptr;
        }
        auto [it, inserted] = text_texture_cache_.emplace(cache_key, entry);
        if (!inserted) {
            destroy_texture_entry(entry);
            it->second.last_used_frame = frame_serial_;
        }
        return &it->second;
    }

    std::shared_ptr<ShapedText> shape_text_node(const TextNode& text_node) {
        if (text_shaper_ == nullptr || text_node.text().empty()) {
            return nullptr;
        }

        auto font = text_node.font();
        font.size *= scale_factor_;
        const auto color = text_node.text_color();
        TextKey key{
            .text = text_node.text(),
            .family = font.family,
            .size = font.size,
            .weight = static_cast<int>(font.weight),
            .style = static_cast<int>(font.style),
            .r = color.r,
            .g = color.g,
            .b = color.b,
            .a = color.a,
            .scale = scale_factor_,
        };

        if (auto it = shaped_text_cache_.find(key); it != shaped_text_cache_.end()) {
            ++last_hotspot_counters_.text_shape_cache_hit_count;
            return it->second;
        }

        if (shaped_text_cache_.size() >= 1024) {
            shaped_text_cache_.clear();
        }

        auto shaped =
            std::make_shared<ShapedText>(text_shaper_->shape(text_node.text(), font, color));
        shaped_text_cache_[key] = shaped;
        ++last_hotspot_counters_.text_shape_count;
        return shaped;
    }

    [[nodiscard]] bool ensure_draw_textures_ready() {
        draw_textures_.clear();
        draw_descriptor_sets_.clear();
        draw_textures_.reserve(draw_commands_.size());
        draw_descriptor_sets_.reserve(draw_commands_.size());

        for (const auto& draw_command : draw_commands_) {
            switch (draw_command.kind) {
            case DrawCommandKind::Primitive:
                draw_textures_.push_back(nullptr);
                draw_descriptor_sets_.push_back(VK_NULL_HANDLE);
                break;
            case DrawCommandKind::Image: {
                auto* texture = upload_image_texture(image_commands_[draw_command.command_index]);
                if (texture == nullptr) {
                    return false;
                }
                draw_textures_.push_back(texture);
                draw_descriptor_sets_.push_back(
                    image_commands_[draw_command.command_index].scale_mode == ScaleMode::Bilinear
                        ? texture->linear_descriptor_set
                        : texture->nearest_descriptor_set);
                break;
            }
            case DrawCommandKind::Text: {
                auto* texture = upload_text_texture(text_commands_[draw_command.command_index]);
                if (texture == nullptr) {
                    return false;
                }
                draw_textures_.push_back(texture);
                draw_descriptor_sets_.push_back(texture->nearest_descriptor_set);
                break;
            }
            }
        }
        return true;
    }

    [[nodiscard]] bool create_texture_descriptor_set(VkDescriptorSet& descriptor_set,
                                                     VkImageView image_view,
                                                     VkSampler sampler) {
        const VkDescriptorSetAllocateInfo allocate_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = descriptor_pool_,
            .descriptorSetCount = 1,
            .pSetLayouts = &descriptor_set_layout_,
        };
        if (!vk_ok(vkAllocateDescriptorSets(device_, &allocate_info, &descriptor_set),
                   "vkAllocateDescriptorSets")) {
            return false;
        }

        const VkDescriptorImageInfo image_info = {
            .sampler = sampler,
            .imageView = image_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet descriptor_write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = descriptor_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };
        vkUpdateDescriptorSets(device_, 1, &descriptor_write, 0, nullptr);
        return true;
    }

    void transition_scene_image(VkCommandBuffer command_buffer,
                                VkImageLayout old_layout,
                                VkImageLayout new_layout,
                                VkAccessFlags src_access,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags src_stage,
                                VkPipelineStageFlags dst_stage) {
        const VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = src_access,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = scene_image_,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        vkCmdPipelineBarrier(command_buffer,
                             src_stage,
                             dst_stage,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        scene_layout_ = new_layout;
    }

    [[nodiscard]] bool ensure_texture_descriptor_sets(TextureCacheEntry& entry, bool allow_linear) {
        if (entry.image_view == VK_NULL_HANDLE) {
            return false;
        }

        if (entry.nearest_descriptor_set == VK_NULL_HANDLE &&
            !create_texture_descriptor_set(entry.nearest_descriptor_set,
                                           entry.image_view,
                                           nearest_sampler_)) {
            return false;
        }

        if (allow_linear && entry.linear_descriptor_set == VK_NULL_HANDLE &&
            !create_texture_descriptor_set(entry.linear_descriptor_set,
                                           entry.image_view,
                                           linear_sampler_)) {
            return false;
        }

        return true;
    }

    [[nodiscard]] bool record_software_upload_commands(uint32_t image_index,
                                                       uint32_t width,
                                                       uint32_t height) {
        const VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };
        if (!vk_ok(vkBeginCommandBuffer(command_buffer_, &begin_info), "vkBeginCommandBuffer")) {
            return false;
        }

        const VkImageMemoryBarrier to_transfer = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain_images_[image_index],
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        vkCmdPipelineBarrier(command_buffer_,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &to_transfer);

        const VkBufferImageCopy region = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, 1},
        };
        vkCmdCopyBufferToImage(command_buffer_,
                               staging_buffer_,
                               swapchain_images_[image_index],
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &region);

        const VkImageMemoryBarrier to_present = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain_images_[image_index],
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        vkCmdPipelineBarrier(command_buffer_,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &to_present);

        return vk_ok(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer");
    }

    [[nodiscard]] bool record_gpu_draw_commands(uint32_t image_index) {
        if (image_index >= swapchain_images_.size() || image_index >= swapchain_framebuffers_.size()) {
            return false;
        }
        if (scene_framebuffer_ == VK_NULL_HANDLE || scene_image_ == VK_NULL_HANDLE) {
            return false;
        }

        const VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };
        if (!vk_ok(vkBeginCommandBuffer(command_buffer_, &begin_info), "vkBeginCommandBuffer")) {
            return false;
        }

        const bool full_redraw = full_redraw_ || !scene_initialized_ || damage_regions_.empty();
        const VkClearColorValue clear_color = {.float32 = {1.0F, 1.0F, 1.0F, 1.0F}};
        const VkImageSubresourceRange scene_range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        const bool copy_back_full_redraw = full_redraw && should_copy_back_full_redraw();
        std::size_t gpu_draw_call_count = 0;
        if (full_redraw && !copy_back_full_redraw) {
            last_hotspot_counters_.gpu_swapchain_copy_count = 0;
            last_hotspot_counters_.gpu_present_path = GpuPresentPath::FullRedrawDirect;
            if (scene_layout_ == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                transition_scene_image(command_buffer_,
                                       scene_layout_,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT);
            } else if (scene_layout_ == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
                transition_scene_image(command_buffer_,
                                       scene_layout_,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       VK_ACCESS_TRANSFER_READ_BIT,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT);
            } else if (scene_layout_ != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                transition_scene_image(command_buffer_,
                                       scene_layout_,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       0,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT);
            }

            vkCmdClearColorImage(command_buffer_,
                                 scene_image_,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &clear_color,
                                 1,
                                 &scene_range);
            transition_scene_image(command_buffer_,
                                   scene_layout_,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            gpu_draw_call_count += record_draw_commands_for_target(scene_framebuffer_, true);
            scene_initialized_ = true;

            const VkImageMemoryBarrier swapchain_to_transfer = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_images_[image_index],
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(command_buffer_,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &swapchain_to_transfer);
            vkCmdClearColorImage(command_buffer_,
                                 swapchain_images_[image_index],
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &clear_color,
                                 1,
                                 &scene_range);

            const VkImageMemoryBarrier swapchain_to_attachment = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_images_[image_index],
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(command_buffer_,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &swapchain_to_attachment);

            gpu_draw_call_count +=
                record_draw_commands_for_target(swapchain_framebuffers_[image_index], true);
            const VkImageMemoryBarrier swapchain_to_present = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = 0,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_images_[image_index],
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(command_buffer_,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &swapchain_to_present);
        } else if (full_redraw) {
            last_hotspot_counters_.gpu_swapchain_copy_count = 1;
            last_hotspot_counters_.gpu_present_path = GpuPresentPath::FullRedrawCopyBack;

            const VkImageMemoryBarrier swapchain_to_transfer = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_images_[image_index],
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(command_buffer_,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &swapchain_to_transfer);
            vkCmdClearColorImage(command_buffer_,
                                 swapchain_images_[image_index],
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &clear_color,
                                 1,
                                 &scene_range);

            const VkImageMemoryBarrier swapchain_to_attachment = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_images_[image_index],
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(command_buffer_,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &swapchain_to_attachment);
            gpu_draw_call_count = record_draw_commands_for_target(swapchain_framebuffers_[image_index],
                                                                  true);

            const VkImageMemoryBarrier swapchain_to_source = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_images_[image_index],
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(command_buffer_,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &swapchain_to_source);

            if (scene_layout_ == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                transition_scene_image(command_buffer_,
                                       scene_layout_,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT);
            } else if (scene_layout_ == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
                transition_scene_image(command_buffer_,
                                       scene_layout_,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       VK_ACCESS_TRANSFER_READ_BIT,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT);
            } else if (scene_layout_ != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                transition_scene_image(command_buffer_,
                                       scene_layout_,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       0,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT);
            }

            const VkImageCopy copy_region = {
                .srcSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .srcOffset = {0, 0, 0},
                .dstSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .dstOffset = {0, 0, 0},
                .extent = {swapchain_extent_.width, swapchain_extent_.height, 1},
            };
            vkCmdCopyImage(command_buffer_,
                           swapchain_images_[image_index],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           scene_image_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copy_region);
            scene_initialized_ = true;

            const VkImageMemoryBarrier swapchain_to_present = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .dstAccessMask = 0,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_images_[image_index],
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(command_buffer_,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &swapchain_to_present);
        } else {
            last_hotspot_counters_.gpu_swapchain_copy_count = 1;
            last_hotspot_counters_.gpu_present_path = GpuPresentPath::PartialRedrawCopy;
            if (scene_layout_ == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
                transition_scene_image(command_buffer_,
                                       scene_layout_,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       VK_ACCESS_TRANSFER_READ_BIT,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            } else if (scene_layout_ == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                transition_scene_image(command_buffer_,
                                       scene_layout_,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            } else if (scene_layout_ != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                transition_scene_image(command_buffer_,
                                       scene_layout_,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       0,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            }
            gpu_draw_call_count = record_draw_commands_for_target(scene_framebuffer_, false);
            scene_initialized_ = true;

            transition_scene_image(command_buffer_,
                                   scene_layout_,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_ACCESS_TRANSFER_READ_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT);

            const VkImageMemoryBarrier swapchain_to_transfer = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_images_[image_index],
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(command_buffer_,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &swapchain_to_transfer);

            const VkImageCopy copy_region = {
                .srcSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .srcOffset = {0, 0, 0},
                .dstSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .dstOffset = {0, 0, 0},
                .extent = {swapchain_extent_.width, swapchain_extent_.height, 1},
            };
            vkCmdCopyImage(command_buffer_,
                           scene_image_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           swapchain_images_[image_index],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copy_region);

            const VkImageMemoryBarrier swapchain_to_present = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = 0,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_images_[image_index],
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(command_buffer_,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &swapchain_to_present);
        }

        last_hotspot_counters_.gpu_draw_call_count = gpu_draw_call_count;

        return vk_ok(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer");
    }

    [[nodiscard]] bool ensure_software_frame() {
        if (!software_frame_rendered_) {
            if (last_root_ == nullptr) {
                return false;
            }
            software_->render(*last_root_);
            software_frame_rendered_ = true;
            const auto preserved = last_hotspot_counters_;
            auto counters = software_->last_hotspot_counters();
            counters.damage_region_count = preserved.damage_region_count;
            counters.gpu_draw_call_count = preserved.gpu_draw_call_count;
            counters.gpu_present_region_count = preserved.gpu_present_region_count;
            counters.gpu_swapchain_copy_count = preserved.gpu_swapchain_copy_count;
            counters.gpu_estimated_draw_pixel_count = preserved.gpu_estimated_draw_pixel_count;
            counters.gpu_present_path = preserved.gpu_present_path;
            last_hotspot_counters_ = counters;
        }

        if (!software_frame_finalized_) {
            software_->end_frame();
            software_frame_finalized_ = true;
        }

        return true;
    }

    void present_software_direct(NativeSurface& surface) {
        if (!ensure_software_frame()) {
            return;
        }
        last_hotspot_counters_.gpu_present_path = GpuPresentPath::SoftwareDirect;
        software_->present(surface);
    }

    bool collect_draw_commands(const RenderNode& node) {
        switch (node.kind()) {
        case RenderNodeKind::Container:
            for (const auto& child : node.children()) {
                if (child != nullptr && !collect_draw_commands(*child)) {
                    return false;
                }
            }
            return true;
        case RenderNodeKind::ColorRect: {
            const auto& color_node = static_cast<const ColorRectNode&>(node);
            PrimitiveCommand command{
                .rect = color_node.bounds(),
                .color = color_node.color(),
                .radius = 0.0F,
                .thickness = 0.0F,
                .kind = 0,
            };
            command.clip_count = static_cast<uint32_t>(active_clips_.size());
            std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
            primitive_commands_.push_back(command);
            draw_commands_.push_back({.kind = DrawCommandKind::Primitive,
                                      .command_index = primitive_commands_.size() - 1});
            return true;
        }
        case RenderNodeKind::RoundedRect: {
            const auto& rounded_node = static_cast<const RoundedRectNode&>(node);
            PrimitiveCommand command{
                .rect = rounded_node.bounds(),
                .color = rounded_node.color(),
                .radius =
                    effective_corner_radius(rounded_node.bounds(), rounded_node.corner_radius()),
                .thickness = 0.0F,
                .kind = 0,
            };
            command.clip_count = static_cast<uint32_t>(active_clips_.size());
            std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
            primitive_commands_.push_back(command);
            draw_commands_.push_back({.kind = DrawCommandKind::Primitive,
                                      .command_index = primitive_commands_.size() - 1});
            return true;
        }
        case RenderNodeKind::Border: {
            const auto& border_node = static_cast<const BorderNode&>(node);
            PrimitiveCommand command{
                .rect = border_node.bounds(),
                .color = border_node.color(),
                .radius =
                    effective_corner_radius(border_node.bounds(), border_node.corner_radius()),
                .thickness = std::max(0.0F, border_node.thickness()),
                .kind = 1,
            };
            command.clip_count = static_cast<uint32_t>(active_clips_.size());
            std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
            primitive_commands_.push_back(command);
            draw_commands_.push_back({.kind = DrawCommandKind::Primitive,
                                      .command_index = primitive_commands_.size() - 1});
            return true;
        }
        case RenderNodeKind::RoundedClip: {
            if (active_clips_.size() >= kMaxClipDepth) {
                return false;
            }
            const auto& clip_node = static_cast<const RoundedClipNode&>(node);
            active_clips_.push_back({
                .rect = clip_node.bounds(),
                .radius = effective_corner_radius(clip_node.bounds(), clip_node.corner_radius()),
            });
            for (const auto& child : node.children()) {
                if (child != nullptr && !collect_draw_commands(*child)) {
                    active_clips_.pop_back();
                    return false;
                }
            }
            active_clips_.pop_back();
            return true;
        }
        case RenderNodeKind::Image: {
            const auto& image_node = static_cast<const ImageNode&>(node);
            if (image_node.pixel_data() == nullptr || image_node.src_width() <= 0 ||
                image_node.src_height() <= 0) {
                return true;
            }

            ++last_hotspot_counters_.image_node_count;
            last_hotspot_counters_.image_source_pixel_count +=
                static_cast<std::size_t>(image_node.src_width()) *
                static_cast<std::size_t>(image_node.src_height());
            last_hotspot_counters_.image_dest_pixel_count +=
                scaled_pixel_area(image_node.bounds(), scale_factor_);

            ImageCommand command{
                .rect = image_node.bounds(),
                .pixel_data = image_node.pixel_data(),
                .src_width = image_node.src_width(),
                .src_height = image_node.src_height(),
                .scale_mode = image_node.scale_mode(),
            };
            command.clip_count = static_cast<uint32_t>(active_clips_.size());
            std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
            image_commands_.push_back(command);
            draw_commands_.push_back(
                {.kind = DrawCommandKind::Image, .command_index = image_commands_.size() - 1});
            return true;
        }
        case RenderNodeKind::Text: {
            const auto& text_node = static_cast<const TextNode&>(node);
            ++last_hotspot_counters_.text_node_count;
            auto shaped_text = shape_text_node(text_node);
            if (shaped_text == nullptr || shaped_text->bitmap_data() == nullptr ||
                shaped_text->bitmap_width() <= 0 || shaped_text->bitmap_height() <= 0) {
                return true;
            }

            last_hotspot_counters_.text_bitmap_pixel_count +=
                static_cast<std::size_t>(shaped_text->bitmap_width()) *
                static_cast<std::size_t>(shaped_text->bitmap_height());

            TextCommand command{
                .rect = {text_node.bounds().x,
                         text_node.bounds().y,
                         shaped_text->bitmap_width() / scale_factor_,
                         shaped_text->bitmap_height() / scale_factor_},
                .shaped_text = std::move(shaped_text),
            };
            command.clip_count = static_cast<uint32_t>(active_clips_.size());
            std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
            text_commands_.push_back(std::move(command));
            draw_commands_.push_back(
                {.kind = DrawCommandKind::Text, .command_index = text_commands_.size() - 1});
            return true;
        }
        }
        return false;
    }

    void destroy_context() {
        ready_ = false;
        recreate_swapchain_ = false;
        graphics_queue_ = VK_NULL_HANDLE;
        queue_family_index_ = std::numeric_limits<uint32_t>::max();
        surface_ = nullptr;
        display_ = nullptr;
        last_root_ = nullptr;
        draw_commands_.clear();
        primitive_commands_.clear();
        image_commands_.clear();
        text_commands_.clear();
        active_clips_.clear();
        draw_textures_.clear();
        draw_descriptor_sets_.clear();
        damage_regions_.clear();
        present_regions_.clear();
        use_gpu_path_ = false;
        software_frame_rendered_ = false;
        software_frame_finalized_ = false;
        incremental_present_supported_ = false;

        if (device_ != VK_NULL_HANDLE) {
            (void)vkDeviceWaitIdle(device_);
        }

        destroy_texture_caches();
        destroy_staging_buffer();
        destroy_swapchain();

        if (linear_sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, linear_sampler_, nullptr);
            linear_sampler_ = VK_NULL_HANDLE;
        }
        if (nearest_sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, nearest_sampler_, nullptr);
            nearest_sampler_ = VK_NULL_HANDLE;
        }
        if (descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }
        if (descriptor_set_layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
            descriptor_set_layout_ = VK_NULL_HANDLE;
        }
        if (in_flight_fence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, in_flight_fence_, nullptr);
            in_flight_fence_ = VK_NULL_HANDLE;
        }
        if (render_finished_semaphore_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, render_finished_semaphore_, nullptr);
            render_finished_semaphore_ = VK_NULL_HANDLE;
        }
        if (image_available_semaphore_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, image_available_semaphore_, nullptr);
            image_available_semaphore_ = VK_NULL_HANDLE;
        }
        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
            command_buffer_ = VK_NULL_HANDLE;
        }

        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (vk_surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, vk_surface_, nullptr);
            vk_surface_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    std::unique_ptr<SoftwareRenderer> software_;
    TextShaper* text_shaper_ = nullptr;
    std::unordered_map<TextKey, std::shared_ptr<ShapedText>, TextKeyHash> shaped_text_cache_;
    std::unordered_map<ImageTextureCacheKey, TextureCacheEntry, ImageTextureCacheKeyHash>
        image_texture_cache_;
    std::unordered_map<TextTextureCacheKey, TextureCacheEntry, TextTextureCacheKeyHash>
        text_texture_cache_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR vk_surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_image_views_;
    std::vector<VkFramebuffer> swapchain_framebuffers_;
    VkImage scene_image_ = VK_NULL_HANDLE;
    VkDeviceMemory scene_image_memory_ = VK_NULL_HANDLE;
    VkImageView scene_image_view_ = VK_NULL_HANDLE;
    VkFramebuffer scene_framebuffer_ = VK_NULL_HANDLE;
    VkImageLayout scene_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline primitive_pipeline_ = VK_NULL_HANDLE;
    VkPipeline image_pipeline_ = VK_NULL_HANDLE;
    VkSampler nearest_sampler_ = VK_NULL_HANDLE;
    VkSampler linear_sampler_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent_{};
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkSemaphore image_available_semaphore_ = VK_NULL_HANDLE;
    VkSemaphore render_finished_semaphore_ = VK_NULL_HANDLE;
    VkFence in_flight_fence_ = VK_NULL_HANDLE;
    VkBuffer staging_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory staging_buffer_memory_ = VK_NULL_HANDLE;
    VkDeviceSize staging_buffer_size_ = 0;
    VkFormat staging_format_ = VK_FORMAT_UNDEFINED;
    uint8_t* staging_mapped_ = nullptr;
    std::vector<uint8_t> converted_pixels_;
    std::vector<uint8_t> image_upload_buffer_;
    std::vector<DrawCommand> draw_commands_;
    std::vector<PrimitiveCommand> primitive_commands_;
    std::vector<ImageCommand> image_commands_;
    std::vector<TextCommand> text_commands_;
    std::vector<TextureCacheEntry*> draw_textures_;
    std::vector<VkDescriptorSet> draw_descriptor_sets_;
    std::vector<ClipRegion> active_clips_;
    std::vector<Rect> damage_regions_;
    std::vector<VkRectLayerKHR> present_regions_;
    uint32_t queue_family_index_ = std::numeric_limits<uint32_t>::max();
    wl_display* display_ = nullptr;
    wl_surface* surface_ = nullptr;
    Size logical_viewport_{};
    float scale_factor_ = 1.0F;
    uint64_t frame_serial_ = 0;
    const RenderNode* last_root_ = nullptr;
    RenderHotspotCounters last_hotspot_counters_{};
    bool recreate_swapchain_ = false;
    bool ready_ = false;
    bool scene_initialized_ = false;
    bool use_gpu_path_ = false;
    bool full_redraw_ = true;
    bool incremental_present_supported_ = false;
    bool software_frame_rendered_ = false;
    bool software_frame_finalized_ = false;
};

} // namespace

std::unique_ptr<Renderer> create_vulkan_renderer() {
    return std::make_unique<VulkanRenderer>();
}

} // namespace nk

#endif
