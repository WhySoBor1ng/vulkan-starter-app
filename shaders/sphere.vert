#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec4 vertex_color;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
    mat4 view_projection;
} scene;

layout(std140, set = 1, binding = 0) uniform ModelUniforms {
    mat4 model;
    vec4 color;
} object_data;

void main() {
    gl_Position = scene.view_projection * object_data.model * vec4(in_position, 1.0);
    vertex_color = vec4(in_color, 1.0) * object_data.color;
}
