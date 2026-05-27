
#include <cstdio>
#include <cstdarg>
#include <cstdint>

#include <functional>
#include <map>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan.h>

#include "VertexShader.vert.h"
#include "FragmentShader.frag.h"

enum
{
    KB = 1024,
    MB = 1024 * KB,
    GB = 1024 * MB
};

const char* APP_NAME = "Hello Triangle";

enum
{
    WIDTH  = 1024,
    HEIGHT = 1024
};

#define ALIGN(size, alignment) (((size) + ((alignment) - 1)) & ~((alignment) - 1))

enum : uint64_t { NANOSECONDS_PER_SECOND = 1000000000 };

void Assert(bool Condition, const char* ErrorMessage, ...)
{
    if (!Condition)
    {
        std::va_list Args;
        va_start(Args, ErrorMessage);

        vprintf(ErrorMessage, Args);
        putc('\n', stdout);

        va_end(Args);
        throw -1;
    }
}

class HelloTriangle
{
private: // Variables
    struct Vertex
    {
        float position[3];
        float color[3];
    };

    static constexpr const Vertex TriangleVertices[] =
    {
        { // vertex 0
            { -0.8f, +0.8f,  0.0f }, // position
            {  0.0f,  0.0f,  1.0f }  // color
        },
        { // vertex 1
            { +0.8f, +0.8f,  0.0f }, // position
            {  0.0f,  1.0f,  0.0f }  // color
        },
        { // vertex 2
            {  0.0f, -0.8f,  0.0f }, // position
            {  1.0f,  0.0f,  0.0f }  // color
        }
    };

    bool                                Running { true };

    SDL_Window*                         Window { nullptr };

    VkInstance                          Instance { nullptr };

    VkPhysicalDevice                    PhysicalDevice { nullptr };
    VkDevice                            Device { nullptr };

    VkQueue                             GraphicsQueue { nullptr };
    VkCommandPool                       CommandPool { nullptr };
    VkCommandBuffer                     CommandBuffer { nullptr };
    VkFence                             Fence { nullptr };

    VkPhysicalDeviceMemoryProperties    MemoryProperties {};

    uint32_t                            PrimaryHeap { UINT32_MAX };
    uint32_t                            UploadHeap { UINT32_MAX };

    VkBuffer                            UploadBuffer { nullptr };
    VkDeviceMemory                      UploadBufferMemory { nullptr };

    uint64_t                            UploadBufferSize { 0 };
    void*                               UploadBufferCpuVA { nullptr };
    
    uint32_t                            GraphicsQueueGroup { UINT32_MAX };

    VkSurfaceKHR                        Surface { nullptr };
    VkSurfaceFormatKHR                  SurfaceFormat { VK_FORMAT_UNDEFINED };

    enum
    {
        MinSwapchainImages = 2,
        MaxSwapchainImages = 4
    };

    uint32_t                            NumSwapchainImages { 0 };
    VkImage                             SwapchainImages[MaxSwapchainImages] {};
    VkImageView                         SwapchainImageViews[MaxSwapchainImages] {};
    VkSwapchainKHR                      Swapchain { nullptr };
    VkRenderPass                        RenderPass { nullptr };
    VkFramebuffer                       Framebuffers[MaxSwapchainImages] {};

    VkSemaphore                         RenderSemaphores[MaxSwapchainImages] {};
    VkSemaphore                         PresentSemaphores[MaxSwapchainImages] {};

    uint32_t                            FrameIndex { 0 }; // 0 to NumSwapchainImages

    VkBuffer                            VertexBuffer { nullptr };
    VkDeviceMemory                      VertexBufferMemory { nullptr };

    VkPipelineLayout                    PipelineLayout { nullptr };
    VkPipeline                          GraphicsPipeline { nullptr };

#ifdef DEBUG
    VkDebugReportCallbackEXT            hVkDebugReport { nullptr };
    PFN_vkCreateDebugReportCallbackEXT  vkCreateDebugReportCb { nullptr };
    PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCb { nullptr };
#endif

private: // Functions
#ifdef DEBUG
    static VkBool32 VulkanDebugReportCb(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
    {
        printf("%s: %s\n", pLayerPrefix, pMessage);
        return VK_FALSE; // The vulkan spec states the application should always return VK_FALSE, because VK_TRUE is only used in layer development
    }
#endif

    void CreateWindow(void)
    {
        Assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS), "Could not initialize SDL");
        Assert(SDL_Vulkan_LoadLibrary(nullptr), "Could not load the vulkan library");

        SDL_PropertiesID WindowProperties = SDL_CreateProperties();
        SDL_SetNumberProperty(WindowProperties, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
        SDL_SetNumberProperty(WindowProperties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
        SDL_SetNumberProperty(WindowProperties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, WIDTH);
        SDL_SetNumberProperty(WindowProperties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, HEIGHT);
        SDL_SetNumberProperty(WindowProperties, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
        
        Window = SDL_CreateWindowWithProperties(WindowProperties);
        
        SDL_DestroyProperties(WindowProperties);

        Assert(Window != nullptr, "Could not create SDL window");
    }

    void CreateVulkanInstance(void)
    {
        uint32_t ExtCount = 0;
        uint32_t LayerCount = 0;

        Assert(vkEnumerateInstanceLayerProperties(&LayerCount, nullptr) == VK_SUCCESS, "Could not get number of instance layers");

        std::vector<VkLayerProperties> AvailableLayers(LayerCount);
        Assert(vkEnumerateInstanceLayerProperties(&LayerCount, AvailableLayers.data()) == VK_SUCCESS, "Could not get instance layers");

        for (uint32_t i = 0; i <= AvailableLayers.size(); i++)
        {
            const char* pLayerName = (i == 0) ? nullptr : AvailableLayers[i - 1].layerName;

            Assert(vkEnumerateInstanceExtensionProperties(pLayerName, &ExtCount, nullptr) == VK_SUCCESS, "Could not get extension count for instance layer");

            std::vector<VkExtensionProperties> AvailableExtensions(ExtCount);
            Assert(vkEnumerateInstanceExtensionProperties(pLayerName, &ExtCount, AvailableExtensions.data()) == VK_SUCCESS, "Could not get extensions for instance layer");

            printf("Instance layer: %s\n", (pLayerName == nullptr) ? "Global" : pLayerName);
            for (uint32_t j = 0; j < AvailableExtensions.size(); j++)
            {
                printf("\t%s\n", AvailableExtensions[j].extensionName);
            }
        }

        char const* const* RequiredSDLExtensions = SDL_Vulkan_GetInstanceExtensions(&ExtCount);
        Assert(RequiredSDLExtensions != nullptr, "Could not get number of required SDL extensions");

        std::vector<const char*> RequiredLayers;
        std::vector<const char*> RequiredExtensions(RequiredSDLExtensions, RequiredSDLExtensions + ExtCount); // Create RequiredExtensions vector with copy of the RequiredSDLExtensions array

#ifdef DEBUG // Only add the validation layer/extension if this is a debug build
        RequiredLayers.push_back("VK_LAYER_KHRONOS_validation");
        RequiredExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

        VkApplicationInfo AppInfo =
        {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = APP_NAME,
            .applicationVersion = 1,
            .pEngineName = APP_NAME,
            .engineVersion = 1,
            .apiVersion = VK_API_VERSION_1_0
        };

        VkInstanceCreateInfo InstanceInfo =
        {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &AppInfo,
            .enabledLayerCount = static_cast<uint32_t>(RequiredLayers.size()),
            .ppEnabledLayerNames = RequiredLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(RequiredExtensions.size()),
            .ppEnabledExtensionNames = RequiredExtensions.data()
        };

        Assert(vkCreateInstance(&InstanceInfo, nullptr, &Instance) == VK_SUCCESS, "Failed to create vulkan instance");

#ifdef DEBUG
        vkCreateDebugReportCb = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(Instance, "vkCreateDebugReportCallbackEXT"));
        vkDestroyDebugReportCb = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(Instance, "vkDestroyDebugReportCallbackEXT"));

        Assert(vkCreateDebugReportCb != nullptr, "Could not get debug report callback");
        Assert(vkDestroyDebugReportCb != nullptr, "Could not get debug report callback");

        VkDebugReportCallbackCreateInfoEXT CallbackInfo =
        {
            .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                     VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT,
            .pfnCallback = VulkanDebugReportCb,
            .pUserData = nullptr
        };

        Assert(vkCreateDebugReportCb(Instance, &CallbackInfo, nullptr, &hVkDebugReport) == VK_SUCCESS, "Failed to register debug callback\n");
#endif
    }

    void EnumerateGPUs(void)
    {
        const std::map<VkPhysicalDeviceType, uint32_t> PreferenceOrder =
        {
            { VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,   2 },
            { VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, 1 }
        };

        struct PhysicalDeviceInfo
        {
            VkPhysicalDevice Handle = nullptr;
            uint32_t         OriginalIndex = 0;
            uint32_t         PreferenceIndex = 0;
            uint32_t         GraphicsQueueGroup = UINT32_MAX;
            uint32_t         NumGraphicsQueues = 0;
            uint64_t         LocalHeapSize = 0;

            // Boolean operator to check if a device is valid
            operator bool() const
            {
                return (Handle != nullptr)
                    && (PreferenceIndex > 0)
                    && (GraphicsQueueGroup != UINT32_MAX);
            }

            // Comparison operator to compare two devices
            // Checks if this device has a greater preference, graphics queue count, and local heap size than the other device
            bool operator > (const PhysicalDeviceInfo& rOtherDevice) const
            {
                return std::tie(this->PreferenceIndex, this->NumGraphicsQueues, this->LocalHeapSize) >
                    std::tie(rOtherDevice.PreferenceIndex, rOtherDevice.NumGraphicsQueues, rOtherDevice.LocalHeapSize);
            }
        };

        uint32_t DeviceCount = 0;
        Assert(vkEnumeratePhysicalDevices(Instance, &DeviceCount, nullptr) == VK_SUCCESS, "Could not get number of physical devices");

        std::vector<VkPhysicalDevice> DeviceHandles(DeviceCount);
        Assert(vkEnumeratePhysicalDevices(Instance, &DeviceCount, DeviceHandles.data()) == VK_SUCCESS, "Could not get physical devices");

        PhysicalDeviceInfo SelectedDevice = {};

        for (uint32_t i = 0; i < DeviceHandles.size(); i++)
        {
            PhysicalDeviceInfo DeviceInfo = { DeviceHandles[i], i };

            VkPhysicalDeviceProperties DeviceProperties {};
            VkPhysicalDeviceMemoryProperties MemoryProperties {};

            vkGetPhysicalDeviceProperties(DeviceHandles[i], &DeviceProperties);
            vkGetPhysicalDeviceMemoryProperties(DeviceHandles[i], &MemoryProperties);

            uint32_t QueueCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(DeviceHandles[i], &QueueCount, nullptr);

            std::vector<VkQueueFamilyProperties> QueueGroups(QueueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(DeviceHandles[i], &QueueCount, QueueGroups.data());

            std::map<VkPhysicalDeviceType, uint32_t>::const_iterator it = PreferenceOrder.find(DeviceProperties.deviceType);
            if (it != PreferenceOrder.end())
            {
                DeviceInfo.PreferenceIndex = it->second;
            }

            for (uint32_t j = 0; j < QueueGroups.size(); j++)
            {
                if (QueueGroups[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    DeviceInfo.GraphicsQueueGroup = std::min(DeviceInfo.GraphicsQueueGroup, j); // Pick the first (minimum) available group, we only use 1 gfx queue, so the group does not matter
                    DeviceInfo.NumGraphicsQueues += QueueGroups[j].queueCount;
                }
            }

            for (uint32_t j = 0; j < MemoryProperties.memoryHeapCount; j++)
            {
                if (MemoryProperties.memoryHeaps[j].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                {
                    DeviceInfo.LocalHeapSize += MemoryProperties.memoryHeaps[j].size;
                }
            }

            if (DeviceInfo)
            {
                SelectedDevice = (DeviceInfo > SelectedDevice) ? DeviceInfo : SelectedDevice;
            }
        }

        Assert(SelectedDevice, "Could not find a supported GPU");

        PhysicalDevice = SelectedDevice.Handle;
        GraphicsQueueGroup = SelectedDevice.GraphicsQueueGroup;
    }

    void CreateVulkanDevice(void)
    {
        uint32_t ExtCount = 0;
        uint32_t LayerCount = 0;

        Assert(vkEnumerateDeviceLayerProperties(PhysicalDevice, &LayerCount, nullptr) == VK_SUCCESS, "Failed to get number of device layers");

        std::vector<VkLayerProperties> AvailableLayers(LayerCount);
        Assert(vkEnumerateDeviceLayerProperties(PhysicalDevice, &LayerCount, AvailableLayers.data()) == VK_SUCCESS, "Failed to get device layers");

        for (uint32_t i = 0; i <= AvailableLayers.size(); i++)
        {
            const char* pLayerName = (i == 0) ? nullptr : AvailableLayers[i - 1].layerName;

            Assert(vkEnumerateDeviceExtensionProperties(PhysicalDevice, pLayerName, &ExtCount, nullptr) == VK_SUCCESS, "Could not get extension count for instance layer");

            std::vector<VkExtensionProperties> AvailableExtensions(ExtCount);
            Assert(vkEnumerateDeviceExtensionProperties(PhysicalDevice, pLayerName, &ExtCount, AvailableExtensions.data()) == VK_SUCCESS, "Could not get extensions for instance layer");

            printf("Device layer: %s\n", (pLayerName == nullptr) ? "Global" : pLayerName);
            for (uint32_t j = 0; j < AvailableExtensions.size(); j++)
            {
                printf("\t%s\n", AvailableExtensions[j].extensionName);
            }
        }

        std::vector<const char*> RequiredExtensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        const float QueuePriority = 1.0f;

        VkDeviceQueueCreateInfo QueueInfo =
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = GraphicsQueueGroup,
            .queueCount = 1,
            .pQueuePriorities = &QueuePriority
        };

        VkDeviceCreateInfo DeviceInfo =
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &QueueInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(RequiredExtensions.size()),
            .ppEnabledExtensionNames = RequiredExtensions.data(),
            .pEnabledFeatures = nullptr
        };

        Assert(vkCreateDevice(PhysicalDevice, &DeviceInfo, nullptr, &Device) == VK_SUCCESS, "Could not create vk device");
    }

    void CreateGraphicsQueue(void)
    {
        vkGetDeviceQueue(Device, GraphicsQueueGroup, 0, &GraphicsQueue);
        Assert(GraphicsQueue != nullptr, "Could not get gfx queue 0");

        VkCommandPoolCreateInfo CommandPoolInfo =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = GraphicsQueueGroup
        };

        Assert(vkCreateCommandPool(Device, &CommandPoolInfo, nullptr, &CommandPool) == VK_SUCCESS, "Could not create the command pool");

        VkCommandBufferAllocateInfo CommandBufferInfo =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = CommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };

        Assert(vkAllocateCommandBuffers(Device, &CommandBufferInfo, &CommandBuffer) == VK_SUCCESS, "Could not create the command buffer");

        VkFenceCreateInfo FenceInfo =
        {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0
        };

        Assert(vkCreateFence(Device, &FenceInfo, nullptr, &Fence) == VK_SUCCESS, "Failed to create fence");
    }

    void AllocateMemory(const VkMemoryRequirements& rMemoryRequirements, uint32_t HeapType, VkDeviceMemory& rMemory) const
    {
        if ((rMemoryRequirements.memoryTypeBits & (1 << HeapType)) == 0)
        {
            Assert(false, "Required memory heap not supported for allocation");
        }

        VkMemoryAllocateInfo AllocationInfo =
        {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = rMemoryRequirements.size,
            .memoryTypeIndex = HeapType
        };

        Assert(vkAllocateMemory(Device, &AllocationInfo, nullptr, &rMemory) == VK_SUCCESS, "Failed to allocate memory");
    }

    void EnumerateMemoryHeaps(void)
    {
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);

        // Helper lambda to scan available memory types
        std::function<bool(uint32_t, uint32_t&)> FindHeap = [&](uint32_t Flags, uint32_t& MemoryType) -> bool
        {
            uint32_t Mask = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            uint64_t MaxSize = 0;
            MemoryType = UINT32_MAX;
            for (uint32_t i = 0; i < MemoryProperties.memoryTypeCount; i++)
            {
                uint32_t HeapIndex = MemoryProperties.memoryTypes[i].heapIndex;
                uint64_t HeapSize = MemoryProperties.memoryHeaps[HeapIndex].size;
                uint32_t HeapFlags = MemoryProperties.memoryTypes[i].propertyFlags;
                if ((HeapFlags & Mask) == Flags && HeapSize > MaxSize)
                {
                    MemoryType = i;
                    MaxSize = HeapSize;
                }
            }
            return (MemoryType != UINT32_MAX);
        };

        uint32_t GpuLocalCpuVisibleHeap = UINT32_MAX; // GPU Local VRAM + CPU Visible Heap
        uint32_t GpuLocalCpuInvisibleHeap = UINT32_MAX; // GPU Local VRAM + CPU Invisible Heap

        // Try to find GpuLocalCpuVisibleHeap
        if (FindHeap(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, GpuLocalCpuVisibleHeap)) {} // GPU Local VRAM, HostVisible, HostCoherent, !HostCached
        else if (FindHeap(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, GpuLocalCpuVisibleHeap)) {} // GPU Local VRAM, HostVisible, !HostCoherent, !HostCached

        // Try to find GpuLocalCpuInvisibleHeap
        if (FindHeap(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, GpuLocalCpuInvisibleHeap)) {} // GPU Local VRAM, !HostVisible, !HostCoherent, !HostCached

        if (GpuLocalCpuVisibleHeap != UINT32_MAX && GpuLocalCpuInvisibleHeap != UINT32_MAX
            && MemoryProperties.memoryTypes[GpuLocalCpuVisibleHeap].heapIndex == MemoryProperties.memoryTypes[GpuLocalCpuInvisibleHeap].heapIndex)
        {
            // If both the GPU Local VRAM + CPU visible and CPU invisible memory types are on the same memory heap, we can just use the CPU visible one
            // This happens when resizable bar is enabled
            PrimaryHeap = GpuLocalCpuVisibleHeap;
            UploadHeap = UINT32_MAX; // Upload heap is not needed, we will write our data directly to the GPU VRAM
        }
        else if (GpuLocalCpuVisibleHeap != UINT32_MAX && GpuLocalCpuInvisibleHeap == UINT32_MAX)
        {
            // If there is no GPU Local VRAM + CPU invisible heap, but there is a GPU Local VRAM + CPU visible heap, we can use that
            // This can happen on iGPUs
            PrimaryHeap = GpuLocalCpuVisibleHeap;
            UploadHeap = UINT32_MAX; // Upload heap is not needed, we will write our data directly to the GPU VRAM
        }
        else if (GpuLocalCpuInvisibleHeap != UINT32_MAX)
        {
            // Otherwise we try to default to the primary heap being the GPU Local VRAM + CPU invisible heap
            // And the upload heap being on the system memory
            PrimaryHeap = GpuLocalCpuInvisibleHeap;

            if (FindHeap(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UploadHeap)) {}
            else if (FindHeap(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, UploadHeap)) {}
            else
            {
                // Cannot find a suitable upload heap
                PrimaryHeap = UINT32_MAX;
                UploadHeap = UINT32_MAX;
            }
        }

        if (PrimaryHeap == UINT32_MAX)
        {
            // If we couldn't find suitable device local memory, we fall back to system memory for the primary heap
            if (FindHeap(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, PrimaryHeap)) {}
            else if (FindHeap(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, PrimaryHeap)) {}
            else
            {
                Assert(false, "Unable to find primary heap");
            }
        }

        // If we have an upload heap, we will need an upload buffer on that heap
        if (UploadHeap != UINT32_MAX)
        {
            uint64_t HeapSize = MemoryProperties.memoryHeaps[MemoryProperties.memoryTypes[UploadHeap].heapIndex].size;
            UploadBufferSize = std::min(ALIGN(HeapSize / 4, MB), static_cast<uint64_t>(16 * MB));

            VkBufferCreateInfo UploadBufferInfo =
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = UploadBufferSize,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr
            };

            Assert(vkCreateBuffer(Device, &UploadBufferInfo, nullptr, &UploadBuffer) == VK_SUCCESS, "Failed to create upload buffer");

            VkMemoryRequirements UploadBufferRequirements = {};
            vkGetBufferMemoryRequirements(Device, UploadBuffer, &UploadBufferRequirements);

            AllocateMemory(UploadBufferRequirements, UploadHeap, UploadBufferMemory);
            Assert(vkBindBufferMemory(Device, UploadBuffer, UploadBufferMemory, 0) == VK_SUCCESS, "Failed to bind upload buffer memory");
            Assert(vkMapMemory(Device, UploadBufferMemory, 0, UploadBufferRequirements.size, 0, &UploadBufferCpuVA) == VK_SUCCESS, "Failed to map upload buffer memory");
        }
    }

    void CreateSwapchain(void)
    {
        Assert(SDL_Vulkan_CreateSurface(Window, Instance, nullptr, &Surface), "Failed to create surface");

        uint32_t PresentModeCount = 0;
        uint32_t SurfaceFormatCount = 0;

        Assert(vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &PresentModeCount, nullptr) == VK_SUCCESS, "Could not get the number of supported presentation modes");
        Assert(vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, nullptr) == VK_SUCCESS, "Could not get the number of supported surface formats");

        std::vector<VkPresentModeKHR> PresentModes(PresentModeCount);
        std::vector<VkSurfaceFormatKHR> SurfaceFormats(SurfaceFormatCount);

        Assert(vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &PresentModeCount, PresentModes.data()) == VK_SUCCESS, "Could not get the supported presentation modes");
        Assert(vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, SurfaceFormats.data()) == VK_SUCCESS, "Could not get the number of supported surface formats");

        for (std::vector<VkSurfaceFormatKHR>::const_iterator it = SurfaceFormats.begin(); it != SurfaceFormats.end(); it++)
        {
            if (it->format == VK_FORMAT_B8G8R8A8_UNORM) { SurfaceFormat = *it; break; }
        }

        Assert(SurfaceFormat.format != VK_FORMAT_UNDEFINED, "Could not find required surface format");
        Assert(std::find(PresentModes.begin(), PresentModes.end(), VK_PRESENT_MODE_FIFO_KHR) != PresentModes.end(), "Could not find required present mode");

        VkSurfaceCapabilitiesKHR SurfaceCapabilities = { 0 };
        Assert(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &SurfaceCapabilities) == VK_SUCCESS, "Could not get surface capabilities");

        VkSwapchainCreateInfoKHR SwapchainInfo =
        {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = Surface,
            .minImageCount = SurfaceCapabilities.minImageCount,
            .imageFormat = SurfaceFormat.format,
            .imageColorSpace = SurfaceFormat.colorSpace,
            .imageExtent =
            {
                .width = SurfaceCapabilities.currentExtent.width,
                .height = SurfaceCapabilities.currentExtent.height
            },
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
            .oldSwapchain = nullptr
        };

        Assert(vkCreateSwapchainKHR(Device, &SwapchainInfo, nullptr, &Swapchain) == VK_SUCCESS, "Failed to create swapchain");

        Assert(vkGetSwapchainImagesKHR(Device, Swapchain, &NumSwapchainImages, nullptr) == VK_SUCCESS, "Could not get number of swapchain images");
        Assert((NumSwapchainImages >= MinSwapchainImages) && (NumSwapchainImages <= MaxSwapchainImages), "Invalid number of swapchain images");
        Assert(vkGetSwapchainImagesKHR(Device, Swapchain, &NumSwapchainImages, SwapchainImages) == VK_SUCCESS, "Could not get swapchain images");

        VkSemaphoreCreateInfo SemaphoreInfo =
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0
        };

        for (uint32_t i = 0; i < NumSwapchainImages; i++)
        {
            Assert(vkCreateSemaphore(Device, &SemaphoreInfo, nullptr, &RenderSemaphores[i]) == VK_SUCCESS, "Failed to create render semaphore %u", i);
            Assert(vkCreateSemaphore(Device, &SemaphoreInfo, nullptr, &PresentSemaphores[i]) == VK_SUCCESS, "Failed to create present semaphore %u", i);
        }
    }

    void CreateRenderPass(void)
    {
        VkAttachmentDescription AttachmentDescriptions[] =
        {
            { // Color attachment
                .flags = 0,
                .format = SurfaceFormat.format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, // image layout undefined at the beginning of the render pass
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR // prepare the color attachment for presentation
            }
        };

        VkAttachmentReference ColorAttachments[] =
        {
            { // Color attachment
                .attachment = 0,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // image layout is VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL during render pass
            }
        };

        VkSubpassDescription SubpassDescription =
        {
            .flags = 0,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = 0,
            .pInputAttachments = nullptr,
            .colorAttachmentCount = 1,
            .pColorAttachments = ColorAttachments,
            .pResolveAttachments = nullptr,
            .pDepthStencilAttachment = nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = nullptr
        };

        VkRenderPassCreateInfo RenderPassInfo =
        {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .attachmentCount = sizeof(AttachmentDescriptions) / sizeof(VkAttachmentDescription),
            .pAttachments = AttachmentDescriptions,
            .subpassCount = 1,
            .pSubpasses = &SubpassDescription,
            .dependencyCount = 0,
            .pDependencies = nullptr
        };

        Assert(vkCreateRenderPass(Device, &RenderPassInfo, nullptr, &RenderPass) == VK_SUCCESS, "Failed to create render pass");
    }

    void CreateFramebuffers(void)
    {
        for (uint32_t i = 0; i < NumSwapchainImages; i++)
        {
            VkImageViewCreateInfo ImageViewInfo =
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = SwapchainImages[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = SurfaceFormat.format,
                .components =
                {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY
                },
                .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            Assert(vkCreateImageView(Device, &ImageViewInfo, nullptr, &SwapchainImageViews[i]) == VK_SUCCESS, "Failed to create image view");
        }

        for (uint32_t i = 0; i < NumSwapchainImages; i++)
        {
            VkImageView FramebufferAttachments[] =
            {
                SwapchainImageViews[i]
            };

            VkFramebufferCreateInfo FramebufferInfo =
            {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .renderPass = RenderPass,
                .attachmentCount = sizeof(FramebufferAttachments) / sizeof(VkImageView),
                .pAttachments = FramebufferAttachments,
                .width = WIDTH,
                .height = HEIGHT,
                .layers = 1
            };

            Assert(vkCreateFramebuffer(Device, &FramebufferInfo, nullptr, &Framebuffers[i]) == VK_SUCCESS, "Failed to create framebuffer");
        }
    }

    void CreateVertexBuffer(void)
    {
        VkBufferCreateInfo BufferInfo =
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = sizeof(TriangleVertices),
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr
        };

        Assert(vkCreateBuffer(Device, &BufferInfo, nullptr, &VertexBuffer) == VK_SUCCESS, "Failed to create vertex buffer");

        VkMemoryRequirements BufferRequirements = {};
        vkGetBufferMemoryRequirements(Device, VertexBuffer, &BufferRequirements);

        AllocateMemory(BufferRequirements, PrimaryHeap, VertexBufferMemory);
        Assert(vkBindBufferMemory(Device, VertexBuffer, VertexBufferMemory, 0) == VK_SUCCESS, "Failed to bind vertex buffer memory");

        if (UploadBufferCpuVA)
        {
            // If the upload heap is being used, we have to copy our allocation to the upload buffer, and then transfer it to the primary allocation
            VkMappedMemoryRange FlushRange =
            {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .pNext = nullptr,
                .memory = UploadBufferMemory,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };

            memcpy(reinterpret_cast<uint8_t*>(UploadBufferCpuVA), TriangleVertices, sizeof(TriangleVertices));
            Assert(vkFlushMappedMemoryRanges(Device, 1, &FlushRange) == VK_SUCCESS, "Failed to flush vertex buffer memory");

            // Generate the command buffer to copy the vertex data from the upload buffer to the vertex buffer
            VkCommandBufferBeginInfo CommandBufferBeginInfo =
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = nullptr,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                .pInheritanceInfo = nullptr
            };

            VkBufferCopy CopyCmd =
            {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = sizeof(TriangleVertices)
            };

            Assert(vkBeginCommandBuffer(CommandBuffer, &CommandBufferBeginInfo) == VK_SUCCESS, "Failed to initialize command buffer");
            vkCmdCopyBuffer(CommandBuffer, UploadBuffer, VertexBuffer, 1, &CopyCmd);
            Assert(vkEndCommandBuffer(CommandBuffer) == VK_SUCCESS, "Failed to finalize command buffer");

            // Submit the copy command buffer to the graphics queue and wait for the copy to finish
            VkSubmitInfo SubmitInfo =
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = nullptr,
                .waitSemaphoreCount = 0,
                .pWaitSemaphores = nullptr,
                .pWaitDstStageMask = nullptr,
                .commandBufferCount = 1,
                .pCommandBuffers = &CommandBuffer,
                .signalSemaphoreCount = 0,
                .pSignalSemaphores = nullptr
            };

            Assert(vkQueueSubmit(GraphicsQueue, 1, &SubmitInfo, Fence) == VK_SUCCESS, "Failed to submit command buffer");
            Assert(vkWaitForFences(Device, 1, &Fence, VK_TRUE, 1 * NANOSECONDS_PER_SECOND) == VK_SUCCESS, "Fence timeout");
            Assert(vkResetFences(Device, 1, &Fence) == VK_SUCCESS, "Could not reset fence");
        }
        else
        {
            // If we don't need an upload heap, it means we can map the primary buffer's memory and copy our vertex data directly to it
            void* VertexBufferCpuVA = nullptr;
            Assert(vkMapMemory(Device, VertexBufferMemory, 0, BufferRequirements.size, 0, &VertexBufferCpuVA) == VK_SUCCESS, "Failed to map vertex buffer memory");

            VkMappedMemoryRange FlushRange =
            {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .pNext = nullptr,
                .memory = VertexBufferMemory,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };

            // Copy the vertex data to the CPU mapped memory
            memcpy(reinterpret_cast<uint8_t*>(VertexBufferCpuVA), TriangleVertices, sizeof(TriangleVertices));
            Assert(vkFlushMappedMemoryRanges(Device, 1, &FlushRange) == VK_SUCCESS, "Failed to flush vertex buffer memory");

            // Unmap the vertex buffer - we don't need it to be CPU accessible anymore
            vkUnmapMemory(Device, VertexBufferMemory);
        }
    }

    void CreateGraphicsPipeline(void)
    {
        bool status = true;

        VkShaderModuleCreateInfo VertexShaderInfo =
        {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = sizeof(VertexShader),
            .pCode = VertexShader
        };

        VkShaderModuleCreateInfo FragmentShaderInfo =
        {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = sizeof(FragmentShader),
            .pCode = FragmentShader
        };

        VkShaderModule VertexShaderModule = nullptr;
        VkShaderModule FragmentShaderModule = nullptr;

        try
        {
            Assert(vkCreateShaderModule(Device, &VertexShaderInfo, nullptr, &VertexShaderModule) == VK_SUCCESS, "Could not create vertex shader module");
            Assert(vkCreateShaderModule(Device, &FragmentShaderInfo, nullptr, &FragmentShaderModule) == VK_SUCCESS, "Could not create fragment shader module");

            VkPipelineLayoutCreateInfo PipelineLayoutInfo =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .setLayoutCount = 0,
                .pSetLayouts = nullptr,
                .pushConstantRangeCount = 0,
                .pPushConstantRanges = nullptr
            };

            Assert(vkCreatePipelineLayout(Device, &PipelineLayoutInfo, nullptr, &PipelineLayout) == VK_SUCCESS, "Failed to create pipeline layout");

            VkPipelineShaderStageCreateInfo PipelineShaderStageInfo[2] =
            {
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = VertexShaderModule,
                    .pName = "main",
                    .pSpecializationInfo = nullptr
                },
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = FragmentShaderModule,
                    .pName = "main",
                    .pSpecializationInfo = nullptr
                }
            };

            VkVertexInputBindingDescription Bindings[] =
            {
                {
                    .binding = 0,
                    .stride = sizeof(Vertex),
                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                }
            };

            VkVertexInputAttributeDescription Attributes[] =
            {
                {
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex, position)
                },
                {
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex, color)
                }
            };

            VkPipelineVertexInputStateCreateInfo PipelineVertexInputStateInfo =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .vertexBindingDescriptionCount = sizeof(Bindings) / sizeof(VkVertexInputBindingDescription),
                .pVertexBindingDescriptions = Bindings,
                .vertexAttributeDescriptionCount = sizeof(Attributes) / sizeof(VkVertexInputAttributeDescription),
                .pVertexAttributeDescriptions = Attributes
            };

            VkPipelineInputAssemblyStateCreateInfo PipelineInputAssemblyStateInfo =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                .primitiveRestartEnable = VK_FALSE
            };

            VkViewport Viewport =
            {
                .x = 0.0f,
                .y = 0.0f,
                .width = static_cast<float>(WIDTH),
                .height = static_cast<float>(HEIGHT),
                .minDepth = 0.0f,
                .maxDepth = 1.0f
            };

            VkRect2D Scissor =
            {
                .offset =
                {
                    .x = 0,
                    .y = 0
                },
                .extent =
                {
                    .width = WIDTH,
                    .height = HEIGHT
                }
            };

            VkPipelineViewportStateCreateInfo PipelineViewportStateInfo =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .viewportCount = 1,
                .pViewports = &Viewport,
                .scissorCount = 1,
                .pScissors = &Scissor
            };

            VkPipelineRasterizationStateCreateInfo PipelineRasterizationStateInfo =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .depthClampEnable = VK_FALSE,
                .rasterizerDiscardEnable = VK_FALSE,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .depthBiasEnable = VK_FALSE,
                .depthBiasConstantFactor = 0.0f,
                .depthBiasClamp = 0.0f,
                .depthBiasSlopeFactor = 0.0f,
                .lineWidth = 1.0f
            };

            VkPipelineMultisampleStateCreateInfo PipelineMultisampleStateInfo =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                .sampleShadingEnable = VK_FALSE,
                .minSampleShading = 0.0f,
                .pSampleMask = nullptr,
                .alphaToCoverageEnable = VK_FALSE,
                .alphaToOneEnable = VK_FALSE
            };

            VkPipelineColorBlendAttachmentState PipelineColorBlendAttachmentState =
            {
                .blendEnable = VK_FALSE,
                .srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
            };

            VkPipelineColorBlendStateCreateInfo PipelineColorBlendStateInfo =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .logicOpEnable = VK_FALSE,
                .logicOp = VK_LOGIC_OP_CLEAR,
                .attachmentCount = 1,
                .pAttachments = &PipelineColorBlendAttachmentState,
                .blendConstants = { 0.0f, 0.0f, 0.0f, 0.0f }
            };

            VkPipelineDepthStencilStateCreateInfo DepthStencilInfo =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .depthTestEnable = VK_FALSE,
                .depthWriteEnable = VK_FALSE,
                .depthCompareOp = VK_COMPARE_OP_NEVER,
                .depthBoundsTestEnable = VK_FALSE,
                .stencilTestEnable = VK_FALSE,
                .front =
                {
                    .failOp = VK_STENCIL_OP_KEEP,
                    .passOp = VK_STENCIL_OP_KEEP,
                    .depthFailOp = VK_STENCIL_OP_KEEP,
                    .compareOp = VK_COMPARE_OP_NEVER,
                    .compareMask = 0,
                    .writeMask = 0,
                    .reference = 0
                },
                .back =
                {
                    .failOp = VK_STENCIL_OP_KEEP,
                    .passOp = VK_STENCIL_OP_KEEP,
                    .depthFailOp = VK_STENCIL_OP_KEEP,
                    .compareOp = VK_COMPARE_OP_NEVER,
                    .compareMask = 0,
                    .writeMask = 0,
                    .reference = 0
                },
                .minDepthBounds = 0.0f,
                .maxDepthBounds = 0.0f
            };

            VkGraphicsPipelineCreateInfo GraphicsPipelineInfo =
            {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stageCount = 2,
                .pStages = PipelineShaderStageInfo,
                .pVertexInputState = &PipelineVertexInputStateInfo,
                .pInputAssemblyState = &PipelineInputAssemblyStateInfo,
                .pTessellationState = nullptr,
                .pViewportState = &PipelineViewportStateInfo,
                .pRasterizationState = &PipelineRasterizationStateInfo,
                .pMultisampleState = &PipelineMultisampleStateInfo,
                .pDepthStencilState = &DepthStencilInfo,
                .pColorBlendState = &PipelineColorBlendStateInfo,
                .pDynamicState = nullptr,
                .layout = PipelineLayout,
                .renderPass = RenderPass,
                .subpass = 0,
                .basePipelineHandle = nullptr,
                .basePipelineIndex = -1
            };

            Assert(vkCreateGraphicsPipelines(Device, nullptr, 1, &GraphicsPipelineInfo, nullptr, &GraphicsPipeline) == VK_SUCCESS, "Failed to create graphics pipeline");

        }
        catch (...)
        {
            status = false;
        }

        if (VertexShaderModule != nullptr)
        {
            vkDestroyShaderModule(Device, VertexShaderModule, nullptr);
            VertexShaderModule = nullptr;
        }
        
        if (FragmentShaderModule != nullptr)
        {
            vkDestroyShaderModule(Device, FragmentShaderModule, nullptr);
            FragmentShaderModule = nullptr;
        }

        if (!status)
        {
            throw -1;
        }
    }

    void Update(void)
    {
        SDL_Event event = { 0 };
        while (SDL_PollEvent(&event) != 0)
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                {
                    Running = false; // The main loop will exit once this becomes false
                    break;
                }
                case SDL_EVENT_KEY_DOWN:
                {
                    switch (event.key.scancode)
                    {
                        case SDL_SCANCODE_ESCAPE:
                            Running = false; // The main loop will exit once this becomes false
                            break;
                        default:
                            break;
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    void Render(void)
    {
        uint32_t SwapchainIndex = 0;
        Assert(vkAcquireNextImageKHR(Device, Swapchain, 1 * NANOSECONDS_PER_SECOND, RenderSemaphores[FrameIndex], nullptr, &SwapchainIndex) == VK_SUCCESS, "Could not get next surface image");

        // Color buffer clear color
        VkClearValue ClearColor;
        ClearColor.color.float32[0] = 0.00f;
        ClearColor.color.float32[1] = 0.00f;
        ClearColor.color.float32[2] = 0.45f;
        ClearColor.color.float32[3] = 0.00f;

        VkRect2D RenderArea =
        {
            .offset =
            {
                .x = 0,
                .y = 0
            },
            .extent =
            {
                .width = WIDTH,
                .height = HEIGHT
            }
        };

        VkCommandBufferBeginInfo CommandBufferBeginInfo =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };

        VkRenderPassBeginInfo RenderPassBeginInfo =
        {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = RenderPass,
            .framebuffer = Framebuffers[SwapchainIndex],
            .renderArea = RenderArea,
            .clearValueCount = 1,
            .pClearValues = &ClearColor
        };

        uint64_t pOffsets[1] = { 0 };
        VkBuffer pBuffers[1] = { VertexBuffer };
        const uint32_t VertexCount = sizeof(TriangleVertices) / sizeof(Vertex);

        Assert(vkBeginCommandBuffer(CommandBuffer, &CommandBufferBeginInfo) == VK_SUCCESS, "Failed to initialize command buffer");
        vkCmdBeginRenderPass(CommandBuffer, &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GraphicsPipeline);
        vkCmdBindVertexBuffers(CommandBuffer, 0, 1, pBuffers, pOffsets);
        vkCmdDraw(CommandBuffer, VertexCount, 1, 0, 0);
        vkCmdEndRenderPass(CommandBuffer);
        Assert(vkEndCommandBuffer(CommandBuffer) == VK_SUCCESS, "Failed to finalize command buffer");

        VkPipelineStageFlags WaitDstStageMasks[] = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };

        VkSubmitInfo SubmissionInfo =
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &RenderSemaphores[FrameIndex],
            .pWaitDstStageMask = WaitDstStageMasks,
            .commandBufferCount = 1,
            .pCommandBuffers = &CommandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &PresentSemaphores[FrameIndex]
        };

        VkPresentInfoKHR PresentInfo =
        {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &PresentSemaphores[FrameIndex],
            .swapchainCount = 1,
            .pSwapchains = &Swapchain,
            .pImageIndices = &SwapchainIndex,
            .pResults = nullptr
        };

        Assert(vkQueueSubmit(GraphicsQueue, 1, &SubmissionInfo, Fence) == VK_SUCCESS, "Failed to submit command buffer");
        Assert(vkQueuePresentKHR(GraphicsQueue, &PresentInfo) == VK_SUCCESS, "Failed to present");
        
        Assert(vkWaitForFences(Device, 1, &Fence, VK_TRUE, 1 * NANOSECONDS_PER_SECOND) == VK_SUCCESS, "Fence timeout");
        Assert(vkResetFences(Device, 1, &Fence) == VK_SUCCESS, "Could not reset fence");

        FrameIndex = (FrameIndex + 1) % NumSwapchainImages;
    }

public: // Functions
    HelloTriangle(void)
    {
    }

    void Init(void)
    {
        CreateWindow();
        CreateVulkanInstance();
        EnumerateGPUs();
        CreateVulkanDevice();
        CreateGraphicsQueue();
        EnumerateMemoryHeaps();

        CreateSwapchain();
        CreateRenderPass();
        CreateFramebuffers();

        CreateVertexBuffer();
        CreateGraphicsPipeline();
    }

    ~HelloTriangle(void)
    {
        if (Device != nullptr)
        {
            vkDeviceWaitIdle(Device);
        }

        if (VertexBufferMemory != nullptr)
        {
            vkFreeMemory(Device, VertexBufferMemory, nullptr);
            VertexBufferMemory = nullptr;
        }

        if (VertexBuffer != nullptr)
        {
            vkDestroyBuffer(Device, VertexBuffer, nullptr);
            VertexBuffer = nullptr;
        }

        if (GraphicsPipeline != nullptr)
        {
            vkDestroyPipeline(Device, GraphicsPipeline, nullptr);
            GraphicsPipeline = nullptr;
        }

        if (PipelineLayout != nullptr)
        {
            vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
            PipelineLayout = nullptr;
        }

        for (uint32_t i = 0; i < MaxSwapchainImages; i++)
        {
            if (SwapchainImageViews[i] != nullptr)
            {
                vkDestroyImageView(Device, SwapchainImageViews[i], nullptr);
                SwapchainImageViews[i] = nullptr;
            }

            if (Framebuffers[i] != nullptr)
            {
                vkDestroyFramebuffer(Device, Framebuffers[i], nullptr);
                Framebuffers[i] = nullptr;
            }
        }

        if (RenderPass != nullptr)
        {
            vkDestroyRenderPass(Device, RenderPass, nullptr);
            RenderPass = nullptr;
        }

        for (uint32_t i = 0; i < MaxSwapchainImages; i++)
        {
            if (RenderSemaphores[i] != nullptr)
            {
                vkDestroySemaphore(Device, RenderSemaphores[i], nullptr);
                RenderSemaphores[i] = nullptr;
            }

            if (PresentSemaphores[i] != nullptr)
            {
                vkDestroySemaphore(Device, PresentSemaphores[i], nullptr);
                PresentSemaphores[i] = nullptr;
            }
        }

        if (Swapchain != nullptr)
        {
            vkDestroySwapchainKHR(Device, Swapchain, nullptr);
            Swapchain = nullptr;

            for (uint32_t i = 0; i < NumSwapchainImages; i++)
            {
                SwapchainImages[i] = nullptr;
            }
        }

        if (Surface != nullptr)
        {
            vkDestroySurfaceKHR(Instance, Surface, nullptr);
            Surface = nullptr;
        }

        if (UploadBufferMemory != nullptr)
        {
            vkUnmapMemory(Device, UploadBufferMemory);
            vkFreeMemory(Device, UploadBufferMemory, nullptr);
            UploadBufferCpuVA = nullptr;
            UploadBufferMemory = nullptr;
        }

        if (UploadBuffer != nullptr)
        {
            vkDestroyBuffer(Device, UploadBuffer, nullptr);
            UploadBuffer = nullptr;
        }

        if (CommandBuffer != nullptr)
        {
            vkFreeCommandBuffers(Device, CommandPool, 1, &CommandBuffer);
            CommandBuffer = nullptr;
        }

        if (CommandPool != nullptr)
        {
            vkDestroyCommandPool(Device, CommandPool, nullptr);
            CommandPool = nullptr;
        }

        if (Fence != nullptr)
        {
            vkDestroyFence(Device, Fence, nullptr);
            Fence = nullptr;
        }

        if (Device != nullptr)
        {
            vkDestroyDevice(Device, nullptr);
            Device = nullptr;
        }

#ifdef DEBUG
        if (hVkDebugReport != nullptr)
        {
            vkDestroyDebugReportCb(Instance, hVkDebugReport, nullptr);
            hVkDebugReport = nullptr;
        }
#endif

        if (Instance != nullptr)
        {
            vkDestroyInstance(Instance, nullptr);
            Instance = nullptr;
        }

        if (Window != nullptr)
        {
            SDL_DestroyWindow(Window);
            Window = nullptr;
        }

        SDL_Vulkan_UnloadLibrary();
        SDL_Quit();
    }

    void Run(void)
    {
        while (Running)
        {
            Update();
            Render();
        }
    }
};

int main(int argc, char* argv[])
{
    HelloTriangle App;
    App.Init();
    App.Run();

    return 0;
}
