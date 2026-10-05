#include "application.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <imgui.h>
#include <vector>

#include "geometry.hpp"
#include "graphics.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/scalar_constants.hpp"
#include "glm/gtc/type_ptr.hpp"

namespace application {
namespace {
	using namespace graphics::core;

	constexpr uint32_t longitude = 14;
	constexpr uint32_t latitude = 8;
	constexpr uint32_t vertex_count = 2 + (latitude - 1) * longitude;
	constexpr uint32_t triangle_count = 2 * longitude * (latitude - 1);
	constexpr int kMaxObjectCount = 2;
	static_assert(vertex_count == 100);

	struct ObjectState {
		glm::vec3 position{};
		glm::vec3 rotation{};
		glm::vec3 scale{0.85f};
		glm::vec4 color{1.0f};
		float phase = 0.0f;
	};

	std::array<ObjectState, kMaxObjectCount> objects{};
	bool use_perspective = true;
	float field_of_view = 55;
	float orthographic_size = 2.5f;
	int selected_object = 0;
	int object_count = 1;
	bool animation_playing = false;
	float animation_speed = 1.0f;
	float orbit_radius = 0.5f;
	float orbit_height = 0.3f;
	float rotation_speed = 0.6f;
	double previous_time = 0.0;
	double animation_time = 0.0;

	void resetScene() {
		objects = {};
		objects[1].position = {-2.0f, 0, 0};
		objects[1].scale = glm::vec3(0.65f);
		objects[1].color = {0.55f, 0.8f, 1, 1};
		objects[1].phase = 2 * glm::pi<float>() / 3;
		use_perspective = true;
		field_of_view = 55;
		orthographic_size = 2.5f;
		selected_object = 0;
		object_count = 1;
		animation_playing = false;
		animation_speed = 1.0f;
		orbit_radius = 0.5f;
		orbit_height = 0.3f;
		rotation_speed = 0.6f;
		animation_time = 0.0;
	}

	glm::mat4 makeModelMatrix(const ObjectState& object, double time) {
		const float t = static_cast<float>(time);
		glm::vec3 position = object.position;
		position.x += orbit_radius * (std::cos(t + object.phase) - std::cos(object.phase));
		position.y += orbit_height * (std::sin(2 * t + object.phase) - std::sin(object.phase));
		position.z += orbit_radius * (std::sin(t + object.phase) - std::sin(object.phase));

		glm::mat4 model = glm::translate(glm::mat4(1), position);
		model = glm::rotate(model, glm::radians(object.rotation.x) + t * rotation_speed, {1, 0, 0});
		model = glm::rotate(model, glm::radians(object.rotation.y) + t * rotation_speed * 0.7f, {0, 1, 0});
		model = glm::rotate(model, glm::radians(object.rotation.z), {0, 0, 1});
		return glm::scale(model, object.scale);
	}

} // namespace

bool initialize() {
	resetScene();
	if (!graphics::core::initialize()) {
		return false;
	}
	if (!graphics::core::upload_mesh(geometry::make_sphere(longitude, latitude))) {
		graphics::core::shutdown();
		return false;
	}
	return true;
}

void shutdown() {
	graphics::core::shutdown();
}

void update(double time) {
	const double delta = std::clamp(time - previous_time, 0.0, 0.1);
	previous_time = time;
	if (animation_playing) {
		animation_time += delta * animation_speed;
	}

	ImGui::SetNextWindowPos({16, 16}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({370, 650}, ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 1 - Variant 5: Sphere");
	ImGui::Text("%u vertices, %u triangles", vertex_count, triangle_count);
	ImGui::TextUnformatted("Vulkan / GLFW / ImGui");
	if (ImGui::Button("Reset scene")) {
		resetScene();
	}

	if (ImGui::CollapsingHeader("Projection", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::RadioButton("Perspective", use_perspective)) use_perspective = true;
		ImGui::SameLine();
		if (ImGui::RadioButton("Orthographic", !use_perspective)) use_perspective = false;
		if (use_perspective) {
			ImGui::SliderFloat("Field of view", &field_of_view, 25, 90, "%.0f deg");
		} else {
			ImGui::SliderFloat("View size", &orthographic_size, 1, 6);
		}
	}

	if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderInt("Visible spheres", &object_count, 1, kMaxObjectCount);
		selected_object = std::min(selected_object, object_count - 1);
		for (int i = 0; i < object_count; ++i) {
			ImGui::PushID(i);
			if (i != 0) ImGui::SameLine();
			if (ImGui::RadioButton(i == 0 ? "Sphere 1" : "Sphere 2", selected_object == i)) {
				selected_object = i;
			}
			ImGui::PopID();
		}
		auto& object = objects[selected_object];
		ImGui::DragFloat3("Position", glm::value_ptr(object.position), 0.02f, -5, 5);
		ImGui::DragFloat3("Rotation", glm::value_ptr(object.rotation), 1, -360, 360, "%.0f deg");
		ImGui::DragFloat3("Scale", glm::value_ptr(object.scale), 0.01f, 0.1f, 2.5f);
		for (int axis = 0; axis < 3; ++axis) object.scale[axis] = std::clamp(object.scale[axis], 0.1f, 2.5f);
		ImGui::ColorEdit3("Color multiplier", glm::value_ptr(object.color));
	}

	if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Button(animation_playing ? "Pause" : "Play")) animation_playing = !animation_playing;
		ImGui::SameLine();
		if (ImGui::Button("Rewind")) animation_time = 0.0;
		ImGui::SliderFloat("Speed", &animation_speed, 0, 3, "%.2fx");
		ImGui::SliderFloat("Orbit radius", &orbit_radius, 0, 1.5f);
		ImGui::SliderFloat("Orbit height", &orbit_height, 0, 1);
		ImGui::SliderFloat("Rotation speed", &rotation_speed, 0, 3);
		ImGui::Text("Time: %.2f s", animation_time);
	}
	ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
	const auto extent = graphics::core::viewportArea().extent;
	if (extent.width == 0 || extent.height == 0) return;
	const float aspect = static_cast<float>(extent.width) / extent.height;
	const auto view = glm::lookAt(glm::vec3(0, 0, 7), glm::vec3(0), glm::vec3(0, 1, 0));
	const float half_width = orthographic_size * aspect;
	auto projection = use_perspective
		? glm::perspective(glm::radians(field_of_view), aspect, 0.1f, 100.0f)
		: glm::ortho(-half_width, half_width, -orthographic_size, orthographic_size, 0.1f, 100.0f);
	projection[1][1] *= -1;
	std::vector<RenderObject> draws;
	for (int i = 0; i < object_count; ++i) {
		draws.push_back({makeModelMatrix(objects[i], animation_time), objects[i].color});
	}
	graphics::core::render(fd, projection * view, draws);
}

} // namespace application
