#pragma once

#include "graphics_internal.hpp"
#include "render_data.hpp"

namespace graphics::core {
bool initialize();
void shutdown();
bool upload_mesh(const MeshData &mesh);
VkRect2D viewportArea();
void render(const internal::FrameData &fd, const glm::mat4 &view_projection,
            const std::vector<RenderObject> &objects);
}
