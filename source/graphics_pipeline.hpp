#pragma once

#include <vulkan/vulkan.h>

namespace graphics::core::detail {

struct Pipelines {
    VkPipeline sphere = VK_NULL_HANDLE;
};

bool create_pipelines(VkDevice device, VkRenderPass render_pass, VkPipelineLayout layout,
                      Pipelines &pipelines);
void destroy_pipelines(VkDevice device, Pipelines &pipelines);

}
