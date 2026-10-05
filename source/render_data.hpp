#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace graphics::core {
struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
struct RenderObject {
    glm::mat4 model;
    glm::vec4 color;
};
}
