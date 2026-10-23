// BionicaEngine - Bionica Editor
// Refactored main.cpp (Part 1)

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "ui/Panels.h"
#include "viewport.h"

#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <set>
#include <optional>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <cstdint>
#include <cstring>

static const uint32_t WIDTH = 1600;
static const uint32_t HEIGHT = 900;
static const int MAX_FRAMES_IN_FLIGHT = 2;

static const std::vector<const char*> g_deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

ViewportRenderer* g_viewport = nullptr;

// ---------------------------------------------------------------------------
// Vulkan helpers
// ---------------------------------------------------------------------------
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

static void checkVkResult(VkResult err) {
    if (err != VK_SUCCESS) {
        std::cerr << "[vulkan] error code: " << (int)err << std::endl;
    }
}

class BionicaApp {
public:
    void run() {
        initWindow();
        initVulkan();
        initImGui();
        viewport3d.init(physicalDevice, device, graphicsQueue, commandPool);
        g_viewport = &viewport3d;
        registerPanels();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* window = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamilyIndex = 0;

    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;
    VkFormat swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapChainExtent{};
    uint32_t minImageCount = 2;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    ViewportRenderer viewport3d;
    PanelManager panels;
    bool layoutInitialized = false;
    bool resetLayoutRequested = false;

    uint32_t currentFrame = 0;
    bool framebufferResized = false;

    static void framebufferResizeCallback(GLFWwindow* win, int, int) {
        auto app = reinterpret_cast<BionicaApp*>(glfwGetWindowUserPointer(win));
        app->framebufferResized = true;
    }

    void initWindow() {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to init GLFW");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow((int)WIDTH, (int)HEIGHT, "Bionica Editor", nullptr, nullptr);
        if (!window) {
            throw std::runtime_error("Failed to create GLFW window");
        }
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    }

    void initVulkan() {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createFramebuffers();
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();
    }

    void createInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Bionica Editor";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.pEngineName = "BionicaEngine";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = (uint32_t)extensions.size();
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = 0;

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan instance");
        }
    }

    void createSurface() {
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface");
        }
        std::cout << "Vulkan surface created." << std::endl;
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice dev) {
        QueueFamilyIndices indices;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families.data());

        for (uint32_t i = 0; i < count; i++) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentSupport);
            if (presentSupport == VK_TRUE) {
                indices.presentFamily = i;
            }
            if (indices.isComplete()) {
                break;
            }
        }
        return indices;
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice dev) {
        uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, available.data());

        for (const char* required : g_deviceExtensions) {
            bool found = false;
            for (const auto& ext : available) {
                if (std::strcmp(ext.extensionName, required) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice dev) {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, &details.capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, details.formats.data());
        }

        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &modeCount, nullptr);
        if (modeCount != 0) {
            details.presentModes.resize(modeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &modeCount, details.presentModes.data());
        }
        return details;
    }

    bool isDeviceSuitable(VkPhysicalDevice dev) {
        QueueFamilyIndices indices = findQueueFamilies(dev);
        if (!indices.isComplete()) {
            return false;
        }
        if (!checkDeviceExtensionSupport(dev)) {
            return false;
        }
        SwapChainSupportDetails support = querySwapChainSupport(dev);
        return !support.formats.empty() && !support.presentModes.empty();
    }

    void pickPhysicalDevice() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (count == 0) {
            throw std::runtime_error("No Vulkan-capable GPU found");
        }
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());

        for (const auto& dev : devices) {
            if (isDeviceSuitable(dev)) {
                physicalDevice = dev;
                break;
            }
        }
        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("No suitable GPU found");
        }

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        std::cout << "GPU: " << props.deviceName << std::endl;
    }

    void createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        graphicsFamilyIndex = indices.graphicsFamily.value();

        std::set<uint32_t> uniqueFamilies = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value()
        };

        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        float priority = 1.0f;
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo qi{};
            qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = family;
            qi.queueCount = 1;
            qi.pQueuePriorities = &priority;
            queueInfos.push_back(qi);
        }

        VkPhysicalDeviceFeatures features{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = (uint32_t)queueInfos.size();
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = &features;
        createInfo.enabledExtensionCount = (uint32_t)g_deviceExtensions.size();
        createInfo.ppEnabledExtensionNames = g_deviceExtensions.data();
        createInfo.enabledLayerCount = 0;

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create logical device");
        }

        vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    }

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return f;
            }
        }
        return formats[0];
    }

    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
        for (const auto& m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
                return m;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps) {
        if (caps.currentExtent.width != (std::numeric_limits<uint32_t>::max)()) {
            return caps.currentExtent;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        VkExtent2D actual{};
        actual.width = (uint32_t)width;
        actual.height = (uint32_t)height;
        actual.width = (std::max)(caps.minImageExtent.width,
                       (std::min)(caps.maxImageExtent.width, actual.width));
        actual.height = (std::max)(caps.minImageExtent.height,
                        (std::min)(caps.maxImageExtent.height, actual.height));
        return actual;
    }

    void createSwapChain() {
        SwapChainSupportDetails support = querySwapChainSupport(physicalDevice);

        VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
        VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
        VkExtent2D extent = chooseExtent(support.capabilities);

        minImageCount = support.capabilities.minImageCount;
        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 &&
            imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t familyIndices[2] = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value()
        };

        if (indices.graphicsFamily.value() != indices.presentFamily.value()) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = familyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swap chain");
        }

        uint32_t actualCount = 0;
        vkGetSwapchainImagesKHR(device, swapChain, &actualCount, nullptr);
        swapChainImages.resize(actualCount);
        vkGetSwapchainImagesKHR(device, swapChain, &actualCount, swapChainImages.data());

        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

    void createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());
        for (size_t i = 0; i < swapChainImages.size(); i++) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = swapChainImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = swapChainImageFormat;
            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &viewInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create image view");
            }
        }
    }

    void createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create render pass");
        }
    }

    void createFramebuffers() {
        swapChainFramebuffers.resize(swapChainImageViews.size());
        for (size_t i = 0; i < swapChainImageViews.size(); i++) {
            VkImageView attachments[1] = { swapChainImageViews[i] };

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = attachments;
            fbInfo.width = swapChainExtent.width;
            fbInfo.height = swapChainExtent.height;
            fbInfo.layers = 1;

            if (vkCreateFramebuffer(device, &fbInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create framebuffer");
            }
        }
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsFamilyIndex;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create command pool");
        }
    }

    void createCommandBuffers() {
        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate command buffers");
        }
    }

    void createSyncObjects() {
        imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores.resize(swapChainImages.size());
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(device, &semInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create frame sync objects");
            }
        }
        for (size_t i = 0; i < renderFinishedSemaphores.size(); i++) {
            if (vkCreateSemaphore(device, &semInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create present semaphore");
            }
        }
    }
    // -----------------------------------------------------------------------
    // ImGui
    // -----------------------------------------------------------------------
    void createImGuiDescriptorPool() {
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 }
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1000;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = poolSizes;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui descriptor pool");
        }
    }

    void applyDarkTheme() {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.TabRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.WindowBorderSize = 1.0f;
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(6.0f, 6.0f);

        ImVec4* c = style.Colors;
        c[ImGuiCol_WindowBg]        = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
        c[ImGuiCol_ChildBg]         = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
        c[ImGuiCol_TitleBg]         = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
        c[ImGuiCol_TitleBgActive]   = ImVec4(0.14f, 0.17f, 0.20f, 1.00f);
        c[ImGuiCol_MenuBarBg]       = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
        c[ImGuiCol_Header]          = ImVec4(0.18f, 0.22f, 0.26f, 1.00f);
        c[ImGuiCol_Button]          = ImVec4(0.18f, 0.20f, 0.23f, 1.00f);
        c[ImGuiCol_ButtonHovered]   = ImVec4(0.24f, 0.28f, 0.33f, 1.00f);
        c[ImGuiCol_FrameBg]         = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
        c[ImGuiCol_Tab]             = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
        c[ImGuiCol_TabActive]       = ImVec4(0.20f, 0.24f, 0.29f, 1.00f);
        c[ImGuiCol_TabHovered]      = ImVec4(0.26f, 0.31f, 0.37f, 1.00f);
    }

    void loadCyrillicFont() {
        ImGuiIO& io = ImGui::GetIO();
        const char* fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            fontPath, 17.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        if (font == nullptr) {
            std::cerr << "Warning: Cyrillic font not loaded, using default." << std::endl;
            io.Fonts->AddFontDefault();
        }
    }

    void initImGui() {
        createImGuiDescriptorPool();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = "bionica_layout.ini";

        loadCyrillicFont();
        applyDarkTheme();

        ImGui_ImplGlfw_InitForVulkan(window, true);

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_2;
        initInfo.Instance = instance;
        initInfo.PhysicalDevice = physicalDevice;
        initInfo.Device = device;
        initInfo.QueueFamily = graphicsFamilyIndex;
        initInfo.Queue = graphicsQueue;
        initInfo.PipelineCache = VK_NULL_HANDLE;
        initInfo.DescriptorPool = imguiDescriptorPool;
        initInfo.PipelineInfoMain.RenderPass = renderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.MinImageCount = (minImageCount < 2) ? 2 : minImageCount;
        initInfo.ImageCount = (uint32_t)swapChainImages.size();
        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = checkVkResult;

        ImGui_ImplVulkan_Init(&initInfo);

        std::cout << "ImGui initialized." << std::endl;
    }

    void registerPanels() {
        panels.add(std::make_unique<SettingsBarPanel>());
        panels.add(std::make_unique<ToolbarTopPanel>());
        panels.add(std::make_unique<ToolPropertiesPanel>());
        panels.add(std::make_unique<ViewportPanel>());
        panels.add(std::make_unique<ProjectTreePanel>());
        panels.add(std::make_unique<QuickAccessBarPanel>());
    }

    // -----------------------------------------------------------------------
    // Dock layout matching the reference scheme
    // -----------------------------------------------------------------------
    void buildDefaultLayout(ImGuiID dockspaceId) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

        ImGuiID rest = dockspaceId;
        ImGuiID topRow = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Up, 0.16f, nullptr, &rest);
        ImGuiID topLeft = ImGui::DockBuilderSplitNode(topRow, ImGuiDir_Left, 0.40f, nullptr, &topRow);

        ImGuiID leftCol = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.18f, nullptr, &rest);
        ImGuiID rightCol = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.22f, nullptr, &rest);
        ImGuiID bottomBar = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Down, 0.10f, nullptr, &rest);

        ImGui::DockBuilderDockWindow("Панель настроек", topLeft);
        ImGui::DockBuilderDockWindow("Инструменты", topRow);
        ImGui::DockBuilderDockWindow("Свойства инструмента", leftCol);
        ImGui::DockBuilderDockWindow("Проект", rightCol);
        ImGui::DockBuilderDockWindow("Быстрый доступ", bottomBar);
        ImGui::DockBuilderDockWindow("Рабочая область", rest);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    void renderDockSpaceAndMenu() {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags hostFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("BionicaDockHost", nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceId = ImGui::GetID("BionicaDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        if (!layoutInitialized || resetLayoutRequested) {
            layoutInitialized = true;
            resetLayoutRequested = false;
            buildDefaultLayout(dockspaceId);
        }

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Файл")) {
                ImGui::MenuItem("Новый проект");
                ImGui::MenuItem("Открыть...");
                ImGui::MenuItem("Сохранить");
                ImGui::Separator();
                if (ImGui::MenuItem("Выход")) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Редактирование")) {
                ImGui::MenuItem("Отменить", "Ctrl+Z");
                ImGui::MenuItem("Повторить", "Ctrl+Y");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Вид")) {
                panels.renderVisibilityMenu();
                ImGui::Separator();
                if (ImGui::MenuItem("Сбросить расположение")) {
                    resetLayoutRequested = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::End();
    }

    void buildUI() {
        renderDockSpaceAndMenu();
        panels.renderAll();
    }

    // -----------------------------------------------------------------------
    // Frame
    // -----------------------------------------------------------------------
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, ImDrawData* drawData) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("Failed to begin command buffer");
        }

        viewport3d.record(cmd);

        VkClearValue clearColor{};
        clearColor.color.float32[0] = 0.08f;
        clearColor.color.float32[1] = 0.09f;
        clearColor.color.float32[2] = 0.11f;
        clearColor.color.float32[3] = 1.0f;

        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = renderPass;
        rpInfo.framebuffer = swapChainFramebuffers[imageIndex];
        rpInfo.renderArea.offset = { 0, 0 };
        rpInfo.renderArea.extent = swapChainExtent;
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        if (drawData != nullptr) {
            ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
        }
        vkCmdEndRenderPass(cmd);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            throw std::runtime_error("Failed to record command buffer");
        }
    }

    void cleanupSwapChain() {
        for (auto fb : swapChainFramebuffers) {
            vkDestroyFramebuffer(device, fb, nullptr);
        }
        swapChainFramebuffers.clear();

        for (auto view : swapChainImageViews) {
            vkDestroyImageView(device, view, nullptr);
        }
        swapChainImageViews.clear();

        if (swapChain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapChain, nullptr);
            swapChain = VK_NULL_HANDLE;
        }
    }

    void recreateSwapChain() {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(device);
        cleanupSwapChain();
        createSwapChain();
        createImageViews();
        createFramebuffers();

        ImGui_ImplVulkan_SetMinImageCount((minImageCount < 2) ? 2 : minImageCount);
    }

    void drawFrame() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        buildUI();

        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();

        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(
            device, swapChain, UINT64_MAX,
            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("Failed to acquire swap chain image");
        }

        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex, drawData);

        VkPipelineStageFlags waitStages[1] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame];
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinishedSemaphores[imageIndex];

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit draw command buffer");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphores[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapChain;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(presentQueue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to present swap chain image");
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
        }
        vkDeviceWaitIdle(device);
    }

    void cleanup() {
        viewport3d.cleanup();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if (imguiDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr);
        }

        cleanupSwapChain();

        for (auto sem : renderFinishedSemaphores) {
            vkDestroySemaphore(device, sem, nullptr);
        }
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }

        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, renderPass, nullptr);
        }
        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
        }
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
        if (window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }
};

int main() {
    std::cout << "Bionica Editor started." << std::endl;

    BionicaApp app;
    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Bionica Editor closed." << std::endl;
    return 0;
}
