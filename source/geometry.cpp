#include "geometry.hpp"

#include <cmath>
#include <glm/gtc/constants.hpp>

namespace geometry {
graphics::core::MeshData make_sphere(uint32_t longitude_segments, uint32_t latitude_segments) {
    graphics::core::MeshData mesh;
    if (longitude_segments < 3 || latitude_segments < 2) {
        return mesh;
    }
    auto &vertices = mesh.vertices;
    auto &indices = mesh.indices;
    vertices.reserve(2 + (latitude_segments - 1) * longitude_segments);
    indices.reserve(6 * longitude_segments * (latitude_segments - 1));

    const auto add_vertex = [&vertices](const glm::vec3 &position) {
        vertices.push_back({
            .position = position,
            .color = (position + glm::vec3(1.0f)) * 0.5f,
        });
    };

    add_vertex({0.0f, 1.0f, 0.0f});
    for (uint32_t lat = 1; lat < latitude_segments; ++lat) {
        const float theta = glm::pi<float>() * static_cast<float>(lat) / latitude_segments;
        for (uint32_t lon = 0; lon < longitude_segments; ++lon) {
            const float phi = 2.0f * glm::pi<float>() * static_cast<float>(lon) / longitude_segments;
            add_vertex({std::sin(theta) * std::cos(phi), std::cos(theta),
                        std::sin(theta) * std::sin(phi)});
        }
    }

    const uint32_t bottom = static_cast<uint32_t>(vertices.size());
    add_vertex({0.0f, -1.0f, 0.0f});

    for (uint32_t lon = 0; lon < longitude_segments; ++lon) {
        const uint32_t next = (lon + 1) % longitude_segments;
        indices.insert(indices.end(), {0, next + 1, lon + 1});
    }
    for (uint32_t ring = 0; ring < latitude_segments - 2; ++ring) {
        for (uint32_t lon = 0; lon < longitude_segments; ++lon) {
            const uint32_t next = (lon + 1) % longitude_segments;
            const uint32_t upper_left = ring * longitude_segments + lon + 1;
            const uint32_t upper_right = ring * longitude_segments + next + 1;
            const uint32_t lower_left = upper_left + longitude_segments;
            const uint32_t lower_right = upper_right + longitude_segments;
            indices.insert(indices.end(), {upper_left, upper_right, lower_left,
                                            upper_right, lower_right, lower_left});
        }
    }
    const uint32_t last_ring = bottom - longitude_segments;
    for (uint32_t lon = 0; lon < longitude_segments; ++lon) {
        const uint32_t next = (lon + 1) % longitude_segments;
        indices.insert(indices.end(), {bottom, last_ring + lon, last_ring + next});
    }
    return mesh;
}

}
