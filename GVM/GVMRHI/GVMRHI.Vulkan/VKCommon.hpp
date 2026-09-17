#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif

#if defined(__APPLE__) && !defined(VK_USE_PLATFORM_METAL_EXT)
#define VK_USE_PLATFORM_METAL_EXT 1
#endif

#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif

#if defined(__OHOS__) && !defined(VK_USE_PLATFORM_OHOS)
#define VK_USE_PLATFORM_OHOS 1
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__) && defined(__has_include)
#if __has_include(<wayland-client.h>) && !defined(VK_USE_PLATFORM_WAYLAND_KHR)
#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include <wayland-client.h>
#endif
#if __has_include(<xcb/xcb.h>) && !defined(VK_USE_PLATFORM_XCB_KHR)
#define VK_USE_PLATFORM_XCB_KHR 1
#include <xcb/xcb.h>
#endif
#if __has_include(<X11/Xlib.h>) && !defined(VK_USE_PLATFORM_XLIB_KHR)
#define VK_USE_PLATFORM_XLIB_KHR 1
#include <X11/Xlib.h>
#endif
#endif

#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#endif

#ifndef VMA_DYNAMIC_VULKAN_FUNCTIONS
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif

#include <volk.h>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#if defined(__OHOS__) && !defined(VK_OHOS_SURFACE_EXTENSION_NAME)
#define VK_OHOS_SURFACE_SPEC_VERSION 1
#define VK_OHOS_SURFACE_EXTENSION_NAME "VK_OHOS_surface"
#ifndef VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS
#define VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS static_cast<VkStructureType>(1000685000)
#endif
typedef struct NativeWindow OHNativeWindow;
typedef VkFlags VkSurfaceCreateFlagsOHOS;
typedef struct VkSurfaceCreateInfoOHOS
{
    VkStructureType sType;
    const void *pNext;
    VkSurfaceCreateFlagsOHOS flags;
    OHNativeWindow *window;
} VkSurfaceCreateInfoOHOS;
typedef VkResult(VKAPI_PTR *PFN_vkCreateSurfaceOHOS)(
    VkInstance instance,
    const VkSurfaceCreateInfoOHOS *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSurfaceKHR *pSurface);
#endif

#include <GVMRHI/GVMRHI.hpp>

#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/sort.h>
#include <EASTL/vector.h>

#include <atomic>
#include <mutex>
#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    inline constexpr uint32_t VulkanApiVersion = VK_API_VERSION_1_3;
    using Mutex = std::mutex;

    /// Wraps std::scoped_lock with an explicit deduction guide for Android NDK toolchains.
    template <typename... Mutexes>
    class ScopedLock : public std::scoped_lock<Mutexes...>
    {
    public:
        /// Locks all supplied mutexes for the lifetime of this scoped guard.
        explicit ScopedLock(Mutexes &...mutexes)
            : std::scoped_lock<Mutexes...>(mutexes...)
        {
        }
    };

    template <typename... Mutexes>
    ScopedLock(Mutexes &...) -> ScopedLock<Mutexes...>;

    using Exception = std::exception;

    template <typename Container>
    auto toEastlVector(const Container &container)
    {
        using ValueType = typename Container::value_type;
        eastl::vector<ValueType> result;
        result.reserve(container.size());
        for (const ValueType &value : container)
        {
            result.push_back(value);
        }
        return result;
    }

    std::runtime_error makeRuntimeError(const char *message);

    std::runtime_error makeRuntimeError(const eastl::string &message);

    std::invalid_argument makeInvalidArgument(const char *message);

    std::invalid_argument makeInvalidArgument(const eastl::string &message);

    std::logic_error makeLogicError(const char *message);

    std::logic_error makeLogicError(const eastl::string &message);

    struct VKSubmissionCompletionState
    {
        std::atomic<bool> completed{false};
        std::atomic<uint64_t> submissionId{0u};
    };

    using VKSubmissionCompletion = eastl::shared_ptr<VKSubmissionCompletionState>;

    struct VKAtomicResourceLifetimeState
    {
        std::atomic<uint32_t> ownerReferences{0u};
        std::atomic<uint32_t> bindGroupReferences{0u};
        std::atomic<uint32_t> commandReferences{0u};
        std::atomic<bool> destroyRequested{false};
        std::atomic<bool> pendingDestroyQueued{false};
    };

    void incrementAtomicReference(std::atomic<uint32_t> &counter);

    void decrementAtomicReference(std::atomic<uint32_t> &counter, const char *apiName);

    std::out_of_range makeOutOfRange(const char *message);

    std::out_of_range makeOutOfRange(const eastl::string &message);
} // namespace GVM::RHI::Vulkan
