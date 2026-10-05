#include "graphics.hpp"
#include "graphics_pipeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

namespace graphics::core {
namespace {
struct GPUBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void *mapped = nullptr;
};

struct Mesh {
    GPUBuffer vertices;
    GPUBuffer indices;
    uint32_t index_count = 0;
};

Mesh sphere_mesh;
GPUBuffer scene_uniform_buffer;
std::vector<GPUBuffer> model_uniforms_buffers;
VkDescriptorSetLayout scene_set_layout;
VkDescriptorSetLayout model_set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet scene_set;
std::vector<VkDescriptorSet> model_sets;
VkPipelineLayout pipeline_layout;
detail::Pipelines pipelines;

bool create_mapped_buffer(VkDeviceSize size, VkBufferUsageFlags usage, GPUBuffer &result) {
    const auto &context = internal::context;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocation_info{};
    allocation_info.flags =
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    allocation_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VmaAllocationInfo allocation_result{};

    const VkResult status = vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
                                            &result.buffer, &result.allocation, &allocation_result);

    if (status != VK_SUCCESS) {
        std::cerr << "cant create vulkan buffer\n";
        return false;
    }

    result.mapped = allocation_result.pMappedData;
    return true;
}

void destroy_buffer(GPUBuffer &buffer) {
    if (buffer.buffer == VK_NULL_HANDLE) {
        return;
    }

    const auto &context = internal::context;

    vmaDestroyBuffer(context.allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

bool write_buffer(GPUBuffer &buffer, const void *data, VkDeviceSize size) {
    if (buffer.mapped == nullptr) {
        return false;
    }

    std::memcpy(buffer.mapped, data, size);

    return vmaFlushAllocation(internal::context.allocator, buffer.allocation, 0, size) ==
           VK_SUCCESS;
}

bool ensure_model_capacity(size_t count) {
    const auto &context = internal::context;
    while (model_uniforms_buffers.size() < count) {
        GPUBuffer buffer{};
        if (!create_mapped_buffer(sizeof(RenderObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  buffer)) {
            return false;
        }

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate.descriptorPool = descriptor_pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &model_set_layout;
        if (vkAllocateDescriptorSets(context.device, &allocate, &set) != VK_SUCCESS) {
            destroy_buffer(buffer);
            std::cerr << "unable to allocate a model descriptor set\n";
            return false;
        }

        VkDescriptorBufferInfo info{buffer.buffer, 0, sizeof(RenderObject)};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
        model_uniforms_buffers.push_back(buffer);
        model_sets.push_back(set);
    }
    return true;
}

bool create_descriptor_resources() {
    const auto &context = internal::context;

    const VkDescriptorSetLayoutBinding binding{
        0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr,
    };
    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr, &scene_set_layout) !=
        VK_SUCCESS) {
        std::cerr << "unable to create scene set layout\n";
        return false;
    }

    if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr, &model_set_layout) !=
        VK_SUCCESS) {
        std::cerr << "unable to create model set layout\n";
        return false;
    }

    const VkDescriptorSetLayout layouts_for_pipeline[] = {scene_set_layout, model_set_layout};

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 2;
    pipeline_layout_info.pSetLayouts = layouts_for_pipeline;

    if (vkCreatePipelineLayout(context.device, &pipeline_layout_info, nullptr, &pipeline_layout) !=
        VK_SUCCESS) {
        std::cerr << "unable to create pipeline layout\n";
        return false;
    }

    constexpr uint32_t descriptor_set_capacity = 3;
    const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptor_set_capacity};
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = descriptor_set_capacity;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS ||
        !create_mapped_buffer(sizeof(glm::mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, scene_uniform_buffer)) {
        return false;
    }

    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = descriptor_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &scene_set_layout;
    if (vkAllocateDescriptorSets(context.device, &allocate, &scene_set) != VK_SUCCESS) {
        return false;
    }
    const VkDescriptorBufferInfo info{scene_uniform_buffer.buffer, 0, sizeof(glm::mat4)};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = scene_set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
    return ensure_model_capacity(2);
}

void record_draw_commands(const internal::FrameData &fd, size_t object_count) {
    const auto &context = internal::context;

    vkResetCommandBuffer(fd.command_buffer, 0);

    VkCommandBufferBeginInfo command_begin{};
    command_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(fd.command_buffer, &command_begin) != VK_SUCCESS) {
        std::cerr << "unable to start writing command buffer\n";
        return;
    }

    VkClearValue clear_values[2]{};
    clear_values[0].color = {{0.04f, 0.05f, 0.08f, 1.0f}};
    clear_values[1].depthStencil = {.depth = 1.0f, .stencil = 0};

    VkRenderPassBeginInfo render_begin{};
    render_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_begin.renderPass = context.render_pass;
    render_begin.framebuffer = fd.framebuffer;
    render_begin.renderArea.extent = context.swapchain_extent;
    render_begin.clearValueCount = 2;
    render_begin.pClearValues = clear_values;

    vkCmdBeginRenderPass(fd.command_buffer, &render_begin, VK_SUBPASS_CONTENTS_INLINE);

    const VkRect2D scissor = viewportArea();
    VkViewport viewport{};
    viewport.x = static_cast<float>(scissor.offset.x);
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(scissor.extent.width);
    viewport.height = static_cast<float>(scissor.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    constexpr VkDeviceSize vertex_offset = 0;

    for (size_t i = 0; i < object_count; ++i) {
        const Mesh &mesh = sphere_mesh;
        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.sphere);
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &mesh.vertices.buffer, &vertex_offset);
        vkCmdBindIndexBuffer(fd.command_buffer, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        const VkDescriptorSet sets[] = {scene_set, model_sets[i]};

        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                                0, 2, sets, 0, nullptr);
        vkCmdDrawIndexed(fd.command_buffer, mesh.index_count, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(fd.command_buffer);

    if (vkEndCommandBuffer(fd.command_buffer) != VK_SUCCESS) {
        std::cerr << "unable to finish writing command buffer\n";
    }
}
}

bool initialize() {
    if (!create_descriptor_resources() ||
        !detail::create_pipelines(internal::context.device, internal::context.render_pass,
                                  pipeline_layout, pipelines)) {
        shutdown();
        return false;
    }

    return true;
}

void shutdown() {
    const auto &context = internal::context;

    if (context.device == VK_NULL_HANDLE) {
        return;
    }

    if (context.graphics_queue != VK_NULL_HANDLE) {
        vkQueueWaitIdle(context.graphics_queue);
    }

    detail::destroy_pipelines(context.device, pipelines);

    if (pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
    }

    if (descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;
    }

    destroy_buffer(scene_uniform_buffer);
    for (GPUBuffer &buffer : model_uniforms_buffers) {
        destroy_buffer(buffer);
    }
    model_uniforms_buffers.clear();
    model_sets.clear();
    destroy_buffer(sphere_mesh.vertices);
    destroy_buffer(sphere_mesh.indices);
    sphere_mesh = {};

    if (scene_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);
        scene_set_layout = VK_NULL_HANDLE;
    }

    if (model_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context.device, model_set_layout, nullptr);
        model_set_layout = VK_NULL_HANDLE;
    }

    scene_set = VK_NULL_HANDLE;
}

bool upload_mesh(const MeshData &data) {
    if (data.vertices.empty() || data.indices.empty()) {
        std::cerr << "mesh vertices or indices are empty\n";
        return false;
    }

    Mesh replacement;
    const VkDeviceSize vertex_bytes = data.vertices.size() * sizeof(Vertex);
    const VkDeviceSize index_bytes = data.indices.size() * sizeof(uint32_t);
    if (!create_mapped_buffer(vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, replacement.vertices) ||
        !create_mapped_buffer(index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, replacement.indices) ||
        !write_buffer(replacement.vertices, data.vertices.data(), vertex_bytes) ||
        !write_buffer(replacement.indices, data.indices.data(), index_bytes)) {
        destroy_buffer(replacement.vertices);
        destroy_buffer(replacement.indices);
        return false;
    }
    replacement.index_count = static_cast<uint32_t>(data.indices.size());

    Mesh &mesh = sphere_mesh;
    if (mesh.vertices.buffer != VK_NULL_HANDLE || mesh.indices.buffer != VK_NULL_HANDLE) {
        vkQueueWaitIdle(internal::context.graphics_queue);
    }
    destroy_buffer(mesh.vertices);
    destroy_buffer(mesh.indices);
    mesh = replacement;
    return true;
}

void render(const internal::FrameData &fd, const glm::mat4 &view_projection,
            const std::vector<RenderObject> &objects) {
    bool ready = objects.size() <= model_sets.size() &&
        write_buffer(scene_uniform_buffer, &view_projection, sizeof(view_projection));
    for (size_t i = 0; ready && i < objects.size(); ++i) {
        ready = write_buffer(model_uniforms_buffers[i], &objects[i], sizeof(RenderObject));
    }
    record_draw_commands(fd, ready ? objects.size() : 0);
}

VkRect2D viewportArea() {
    const auto extent = internal::context.swapchain_extent;
    const uint32_t sidebar_width = std::min(402u, extent.width / 2);
    return {{static_cast<int32_t>(sidebar_width), 0}, {extent.width - sidebar_width, extent.height}};
}

}
