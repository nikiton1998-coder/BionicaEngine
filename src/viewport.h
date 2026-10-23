// BionicaEngine - offscreen 3D viewport renderer with grid, cube and bones
#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cmath>
struct LineVertex {
    glm::vec3 pos;
    glm::vec3 color;
};
// Базовый элемент органики: Кость
struct Bone {
    glm::vec3 head = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 tail = glm::vec3(0.0f, 2.0f, 0.0f);
    float radiusHead = 0.2f;
    float radiusTail = 0.1f;
};
struct OrbitCamera {
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    float distance = 12.0f;
    float yaw = 0.7f;
    float pitch = 0.5f;
    float fovY = glm::radians(50.0f);
    float nearZ = 0.05f;
    float farZ = 500.0f;
    glm::vec3 eye() const {
        float cp = std::cos(pitch);
        return target + glm::vec3(
            distance * cp * std::sin(yaw),
            distance * std::sin(pitch),
            distance * cp * std::cos(yaw));
    }
    glm::mat4 viewProj(float aspect) const {
        glm::mat4 view = glm::lookAt(eye(), target, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(fovY, aspect, nearZ, farZ);
        proj[1][1] *= -1.0f; // Vulkan Y flip
        return proj * view;
    }
    void handleInput() {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            yaw   -= d.x * 0.008f;
            pitch += d.y * 0.008f;
            const float limit = glm::radians(89.0f);
            if (pitch > limit)  pitch = limit;
            if (pitch < -limit) pitch = -limit;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
            glm::vec3 fwd = glm::normalize(target - eye());
            glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 up = glm::normalize(glm::cross(right, fwd));
            float k = distance * 0.0015f;
            target += right * (-d.x * k) + up * (d.y * k);
        }
        if (io.MouseWheel != 0.0f) {
            distance *= std::pow(0.9f, io.MouseWheel);
            if (distance < 0.5f)   distance = 0.5f;
            if (distance > 400.0f) distance = 400.0f;
        }
    }
};
class ViewportRenderer {
public:
    OrbitCamera camera;
    std::vector<Bone> bones;
    void init(VkPhysicalDevice physicalDevice_,
              VkDevice device_,
              VkQueue graphicsQueue_,
              VkCommandPool commandPool_) {
        physicalDevice = physicalDevice_;
        device = device_;
        graphicsQueue = graphicsQueue_;
        commandPool = commandPool_;
        // Стартовая кость по умолчанию
        bones.push_back({ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 2.5f, 0.0f), 0.25f, 0.15f });
        createRenderPass();
        createSampler();
        createPipeline();
        createStaticGeometry();
        rebuildBoneGeometry();
        resize(1280, 720);
    }
    ImTextureID textureId() const {
        return (ImTextureID)(uintptr_t)descriptorSet;
    }
    uint32_t width() const  { return extent.width; }
    uint32_t height() const { return extent.height; }
    void addBone() {
        float offsetY = (float)(bones.size() * 2);
        bones.push_back({ glm::vec3(0.0f, offsetY, 0.0f), glm::vec3(0.0f, offsetY + 2.0f, 0.0f), 0.2f, 0.1f });
        rebuildBoneGeometry();
    }
    void resize(uint32_t w, uint32_t h) {
        if (w < 8)   w = 8;
        if (h < 8)   h = 8;
        if (w > 8192) w = 8192;
        if (h > 8192) h = 8192;
        if (w == extent.width && h == extent.height && colorImage != VK_NULL_HANDLE) {
            return;
        }
        vkDeviceWaitIdle(device);
        destroyTargets();
        extent.width = w;
        extent.height = h;
        createColorTarget();
        createDepthTarget();
        createFramebuffer();
        descriptorSet = ImGui_ImplVulkan_AddTexture(
            sampler, colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    void record(VkCommandBuffer cmd) {
        VkClearValue clears[2]{};
        clears[0].color.float32[0] = 0.055f;
        clears[0].color.float32[1] = 0.060f;
        clears[0].color.float32[2] = 0.070f;
        clears[0].color.float32[3] = 1.0f;
        clears[1].depthStencil.depth = 1.0f;
        clears[1].depthStencil.stencil = 0;
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass;
        rp.framebuffer = framebuffer;
        rp.renderArea.offset = { 0, 0 };
        rp.renderArea.extent = extent;
        rp.clearValueCount = 2;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.width = (float)extent.width;
        vp.height = (float)extent.height;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{};
        sc.offset = { 0, 0 };
        sc.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        float aspect = (float)extent.width / (float)extent.height;
        glm::mat4 vpMatrix = camera.viewProj(aspect);
        VkDeviceSize offset = 0;
        // 1. Сетка
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vpMatrix);
        vkCmdBindVertexBuffers(cmd, 0, 1, &gridVertexBuffer, &offset);
        vkCmdDraw(cmd, gridVertexCount, 1, 0, 0);
        // 2. Куб
        glm::mat4 cubeModel = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 1.0f, 0.0f));
        glm::mat4 cubeMvp = vpMatrix * cubeModel;
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &cubeMvp);
        vkCmdBindVertexBuffers(cmd, 0, 1, &cubeVertexBuffer, &offset);
        vkCmdDraw(cmd, cubeVertexCount, 1, 0, 0);
        // 3. Кости (скелет)
        if (boneVertexCount > 0) {
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vpMatrix);
            vkCmdBindVertexBuffers(cmd, 0, 1, &boneVertexBuffer, &offset);
            vkCmdDraw(cmd, boneVertexCount, 1, 0, 0);
        }
        vkCmdEndRenderPass(cmd);
    }
    void cleanup() {
        vkDeviceWaitIdle(device);
        destroyTargets();
        if (gridVertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, gridVertexBuffer, nullptr); vkFreeMemory(device, gridMemory, nullptr); }
        if (cubeVertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, cubeVertexBuffer, nullptr); vkFreeMemory(device, cubeMemory, nullptr); }
        if (boneVertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, boneVertexBuffer, nullptr); vkFreeMemory(device, boneMemory, nullptr); }
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
        if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
    }
private:
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkExtent2D extent{ 0, 0 };
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkBuffer gridVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory gridMemory = VK_NULL_HANDLE;
    uint32_t gridVertexCount = 0;
    VkBuffer cubeVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory cubeMemory = VK_NULL_HANDLE;
    uint32_t cubeVertexCount = 0;
    VkBuffer boneVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory boneMemory = VK_NULL_HANDLE;
    uint32_t boneVertexCount = 0;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    static const VkFormat COLOR_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
    static const VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & props) == props) {
                return i;
            }
        }
        throw std::runtime_error("Viewport: no suitable memory type");
    }
    void destroyTargets() {
        if (descriptorSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
            descriptorSet = VK_NULL_HANDLE;
        }
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }
        if (colorView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, colorView, nullptr);
            vkDestroyImage(device, colorImage, nullptr);
            vkFreeMemory(device, colorMemory, nullptr);
            colorView = VK_NULL_HANDLE;
            colorImage = VK_NULL_HANDLE;
        }
        if (depthView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, depthView, nullptr);
            vkDestroyImage(device, depthImage, nullptr);
            vkFreeMemory(device, depthMemory, nullptr);
            depthView = VK_NULL_HANDLE;
            depthImage = VK_NULL_HANDLE;
        }
    }
    void createImage(VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent.width = extent.width;
        info.extent.height = extent.height;
        info.extent.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &info, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("Viewport: failed to create image");
        }
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, image, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &alloc, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("Viewport: failed to allocate image memory");
        }
        vkBindImageMemory(device, image, memory, 0);
    }
    VkImageView createView(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange.aspectMask = aspect;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS) {
            throw std::runtime_error("Viewport: failed to create image view");
        }
        return view;
    }
    void createColorTarget() {
        createImage(COLOR_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, colorImage, colorMemory);
        colorView = createView(colorImage, COLOR_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    void createDepthTarget() {
        createImage(DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImage, depthMemory);
        depthView = createView(depthImage, DEPTH_FORMAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    void createFramebuffer() {
        VkImageView attachments[2] = { colorView, depthView };
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderPass;
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.width = extent.width;
        info.height = extent.height;
        info.layers = 1;
        if (vkCreateFramebuffer(device, &info, nullptr, &framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("Viewport: failed to create framebuffer");
        }
    }
    void createRenderPass() {
        VkAttachmentDescription attachments[2]{};
        attachments[0].format = COLOR_FORMAT;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1].format = DEPTH_FORMAT;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 2; info.pAttachments = attachments;
        info.subpassCount = 1; info.pSubpasses = &subpass;
        info.dependencyCount = 2; info.pDependencies = deps;
        if (vkCreateRenderPass(device, &info, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("Viewport: failed to create render pass");
        }
    }
    void createSampler() {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_LINEAR; info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = 1.0f;
        if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
            throw std::runtime_error("Viewport: failed to create sampler");
        }
    }
    std::vector<char> readFile(const std::string& path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Viewport: cannot open shader file: " + path);
        size_t size = (size_t)file.tellg();
        std::vector<char> buffer(size);
        file.seekg(0); file.read(buffer.data(), (std::streamsize)size);
        return buffer;
    }
    VkShaderModule createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
            throw std::runtime_error("Viewport: failed to create shader module");
        }
        return module;
    }
    void createPipeline() {
        std::string dir = BIONICA_SHADER_DIR;
        VkShaderModule vertModule = createShaderModule(readFile(dir + "/grid.vert.spv"));
        VkShaderModule fragModule = createShaderModule(readFile(dir + "/grid.frag.spv"));
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertModule; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragModule; stages[1].pName = "main";
        VkVertexInputBindingDescription binding{ 0, sizeof(LineVertex), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attrs[2]{
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineVertex, pos) },
            { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineVertex, color) }
        };
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1; vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 2; vertexInput.pVertexAttributeDescriptions = attrs;
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
        VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.5f };
        VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT };
        VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS, VK_FALSE, VK_FALSE, {}, {}, 0.0f, 1.0f };
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo colorBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &blendAttachment };
        VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dynamicStates };
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(glm::mat4);
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 0;
        layoutInfo.pSetLayouts = nullptr;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Viewport: failed to create pipeline layout");
        }
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2; pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput; pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState; pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample; pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend; pipelineInfo.pDynamicState = &dynamic;
        pipelineInfo.layout = pipelineLayout; pipelineInfo.renderPass = renderPass; pipelineInfo.subpass = 0;
        VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        if (res != VK_SUCCESS) throw std::runtime_error("Viewport: failed to create graphics pipeline");
    }
    void createStaticGeometry() {
        // Сетка
        std::vector<LineVertex> gridVerts;
        const int halfSize = 20;
        const float step = 1.0f;
        const glm::vec3 minor(0.22f, 0.23f, 0.26f);
        const glm::vec3 major(0.32f, 0.34f, 0.38f);
        for (int i = -halfSize; i <= halfSize; i++) {
            float p = (float)i * step;
            float ext = (float)halfSize * step;
            bool isMajor = (i % 5 == 0);
            glm::vec3 c = isMajor ? major : minor;
            if (i != 0) {
                gridVerts.push_back({ glm::vec3(p, 0.0f, -ext), c });
                gridVerts.push_back({ glm::vec3(p, 0.0f,  ext), c });
                gridVerts.push_back({ glm::vec3(-ext, 0.0f, p), c });
                gridVerts.push_back({ glm::vec3( ext, 0.0f, p), c });
            }
        }
        const float axisLen = (float)halfSize * step;
        gridVerts.push_back({ glm::vec3(-axisLen, 0.0f, 0.0f), glm::vec3(0.85f, 0.25f, 0.25f) });
        gridVerts.push_back({ glm::vec3( axisLen, 0.0f, 0.0f), glm::vec3(0.85f, 0.25f, 0.25f) });
        gridVerts.push_back({ glm::vec3(0.0f, 0.0f, -axisLen), glm::vec3(0.30f, 0.50f, 0.90f) });
        gridVerts.push_back({ glm::vec3(0.0f, 0.0f,  axisLen), glm::vec3(0.30f, 0.50f, 0.90f) });
        gridVerts.push_back({ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.35f, 0.80f, 0.35f) });
        gridVerts.push_back({ glm::vec3(0.0f, axisLen * 0.5f, 0.0f), glm::vec3(0.35f, 0.80f, 0.35f) });
        gridVertexCount = (uint32_t)gridVerts.size();
        createBuffer(gridVerts.data(), sizeof(LineVertex) * gridVerts.size(), gridVertexBuffer, gridMemory);
        // Куб (смещен влево)
        std::vector<LineVertex> cubeVerts;
        float s = 0.5f;
        glm::vec3 cc(0.9f, 0.6f, 0.2f);
        glm::vec3 p[8] = {
            {-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s},
            {-s, -s,  s}, {s, -s,  s}, {s, s,  s}, {-s, s,  s}
        };
        int indices[24] = { 0,1, 1,2, 2,3, 3,0, 4,5, 5,6, 6,7, 7,4, 0,4, 1,5, 2,6, 3,7 };
        for (int idx : indices) cubeVerts.push_back({ p[idx], cc });
        cubeVertexCount = (uint32_t)cubeVerts.size();
        createBuffer(cubeVerts.data(), sizeof(LineVertex) * cubeVerts.size(), cubeVertexBuffer, cubeMemory);
    }
    void rebuildBoneGeometry() {
        std::vector<LineVertex> boneVerts;
        glm::vec3 boneColor(0.8f, 0.85f, 0.95f);
        glm::vec3 jointColor(0.95f, 0.4f, 0.2f);
        for (const auto& bone : bones) {
            boneVerts.push_back({ bone.head, boneColor });
            boneVerts.push_back({ bone.tail, boneColor });
            float rh = bone.radiusHead;
            float rt = bone.radiusTail;
            boneVerts.push_back({ bone.head + glm::vec3(rh,0,0), jointColor }); boneVerts.push_back({ bone.head - glm::vec3(rh,0,0), jointColor });
            boneVerts.push_back({ bone.head + glm::vec3(0,rh,0), jointColor }); boneVerts.push_back({ bone.head - glm::vec3(0,rh,0), jointColor });
            boneVerts.push_back({ bone.head + glm::vec3(0,0,rh), jointColor }); boneVerts.push_back({ bone.head - glm::vec3(0,0,rh), jointColor });
            boneVerts.push_back({ bone.tail + glm::vec3(rt,0,0), jointColor }); boneVerts.push_back({ bone.tail - glm::vec3(rt,0,0), jointColor });
            boneVerts.push_back({ bone.tail + glm::vec3(0,rt,0), jointColor }); boneVerts.push_back({ bone.tail - glm::vec3(0,rt,0), jointColor });
            boneVerts.push_back({ bone.tail + glm::vec3(0,0,rt), jointColor }); boneVerts.push_back({ bone.tail - glm::vec3(0,0,rt), jointColor });
        }
        boneVertexCount = (uint32_t)boneVerts.size();
        if (boneVertexCount > 0) {
            if (boneVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, boneVertexBuffer, nullptr);
                vkFreeMemory(device, boneMemory, nullptr);
            }
            createBuffer(boneVerts.data(), sizeof(LineVertex) * boneVerts.size(), boneVertexBuffer, boneMemory);
        }
    }
    void createBuffer(const void* data, VkDeviceSize size, VkBuffer& outBuffer, VkDeviceMemory& outMemory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Viewport: failed to create vertex buffer");
        }
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, outBuffer, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &alloc, nullptr, &outMemory) != VK_SUCCESS) {
            throw std::runtime_error("Viewport: failed to allocate vertex memory");
        }
        vkBindBufferMemory(device, outBuffer, outMemory, 0);
        void* mapped = nullptr;
        vkMapMemory(device, outMemory, 0, size, 0, &mapped);
        std::memcpy(mapped, data, (size_t)size);
        vkUnmapMemory(device, outMemory);
    }
};