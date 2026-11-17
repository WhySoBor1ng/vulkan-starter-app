#pragma once

#include "graphics_internal.hpp"
#include "render_data.hpp"

namespace graphics::core {

bool initialize();
void shutdown();
bool upload_mesh(MeshKind kind, const MeshData &mesh);
VkRect2D viewportArea();
void render(const internal::FrameData &frame_data, const RenderFrame &frame);

}
