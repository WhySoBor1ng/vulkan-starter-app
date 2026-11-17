#include "application.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <vector>

#include "camera.hpp"
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
	constexpr size_t kMaxLightsPerType = 8;

	struct ObjectState {
		glm::vec3 position{};
		glm::vec3 rotation{};
		glm::vec3 scale{0.85f};
		Material material{.specular_color = glm::vec3(0.45f), .use_vertex_color = true};
		float phase = 0.0f;
	};

	std::array<ObjectState, kMaxObjectCount> objects{};
	Camera camera;
	AmbientLight ambient;
	DirectionalLight directional;
	std::vector<PointLight> point_lights;
	std::vector<SpotLight> spot_lights;
	bool show_light_markers = true;
	bool show_room = true;
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
		objects[1].material.albedo = {0.55f, 0.8f, 1, 1};
		objects[1].phase = 2 * glm::pi<float>() / 3;
		camera = {};
		camera.position.y = 0.7f;
		camera.pitch_degrees = -6.0f;
		camera.field_of_view_degrees = 55.0f;
		ambient = {.color = glm::vec3(1), .intensity = 0.17f};
		directional = {};
		point_lights = {{{-1.6f, 1.25f, 2.0f}, {0.45f, 0.7f, 1.0f}, 4.0f}};
		spot_lights = {{
			.position = {1.6f, 1.5f, 2.2f},
			.direction = {-1.6f, -1.5f, -2.2f},
			.intensity = 10.0f,
		}};
		show_light_markers = true;
		show_room = true;
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

	template<typename Light>
	void drawLocalLights(const char* label, std::vector<Light>& lights) {
		if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) return;
		ImGui::PushID(label);
		ImGui::BeginDisabled(lights.size() >= kMaxLightsPerType);
		if (ImGui::Button("Add light")) lights.emplace_back();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::Text("%zu / %zu", lights.size(), kMaxLightsPerType);
		for (size_t i = 0; i < lights.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			bool remove = false;
			if (ImGui::TreeNode("source", "Light %zu", i + 1)) {
				auto& light = lights[i];
				ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.05f);
				ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
				ImGui::SliderFloat("Intensity", &light.intensity, 0, 100);
				if constexpr (requires { light.direction; }) {
					ImGui::DragFloat3("Direction", glm::value_ptr(light.direction), 0.02f);
					ImGui::SliderFloat("Inner angle", &light.inner_angle_degrees, 0, light.outer_angle_degrees);
					ImGui::SliderFloat("Outer angle", &light.outer_angle_degrees, light.inner_angle_degrees, 75);
				}
				remove = ImGui::Button("Remove light");
				ImGui::TreePop();
			}
			ImGui::PopID();
			if (remove) {
				lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(i));
				break;
			}
		}
		ImGui::PopID();
	}

	void drawLightingControls() {
		ImGui::Checkbox("Show light markers", &show_light_markers);
		ImGui::Checkbox("Show floor and walls", &show_room);
		if (ImGui::CollapsingHeader("Ambient light", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::ColorEdit3("Ambient color", glm::value_ptr(ambient.color));
			ImGui::SliderFloat("Ambient intensity", &ambient.intensity, 0, 2);
		}
		if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::DragFloat3("Direction", glm::value_ptr(directional.direction), 0.02f);
			ImGui::ColorEdit3("Directional color", glm::value_ptr(directional.color));
			ImGui::SliderFloat("Directional intensity", &directional.intensity, 0, 5);
		}
		drawLocalLights("Point lights", point_lights);
		drawLocalLights("Spot lights", spot_lights);
	}

	void appendRoom(RenderFrame& frame) {
		const auto append_surface = [&frame](glm::vec3 position, glm::vec3 scale,
				float angle, glm::vec3 axis, glm::vec4 color) {
			auto model = glm::translate(glm::mat4(1), position);
			model = glm::rotate(model, angle, axis);
			frame.objects.push_back({
				.mesh = MeshKind::Plane,
				.model = glm::scale(model, scale),
				.material = {.albedo = color, .specular_color = glm::vec3(0.09f), .shininess = 24.0f},
			});
		};
		append_surface({0, -1.15f, 0}, {8, 1, 8}, 0, {1, 0, 0}, {0.17f, 0.19f, 0.23f, 1});
		append_surface({0, 2.35f, -3.2f}, {8, 1, 3.5f}, glm::half_pi<float>(),
			{1, 0, 0}, {0.09f, 0.11f, 0.16f, 1});
		append_surface({-6, 2.35f, 0}, {3.5f, 1, 8}, -glm::half_pi<float>(),
			{0, 0, 1}, {0.08f, 0.1f, 0.15f, 1});
	}

	void appendLightMarker(RenderFrame& frame, glm::vec3 position, glm::vec3 color) {
		const auto model = glm::translate(glm::mat4(1), position);
		frame.objects.push_back({
			.shading = Shading::Unlit,
			.model = glm::scale(model, glm::vec3(0.1f)),
			.material = {.albedo = glm::vec4(color, 1)},
		});
	}
} // namespace

bool initialize() {
	resetScene();
	if (!graphics::core::initialize()) {
		return false;
	}
	if (!graphics::core::upload_mesh(MeshKind::Sphere, geometry::make_sphere(longitude, latitude)) ||
		!graphics::core::upload_mesh(MeshKind::Plane, geometry::make_plane())) {
		graphics::core::shutdown();
		return false;
	}
	return true;
}

void shutdown() {
	graphics::core::shutdown();
}

void update(double time, GLFWwindow* window) {
	const double delta = std::clamp(time - previous_time, 0.0, 0.1);
	previous_time = time;
	camera.update_from_keyboard(window, static_cast<float>(delta));
	if (animation_playing) {
		animation_time += delta * animation_speed;
	}

	ImGui::SetNextWindowPos({16, 16}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({370, 650}, ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 2 - Variant 5: Lighting");
	ImGui::Text("%u vertices, %u triangles", vertex_count, triangle_count);
	ImGui::TextUnformatted("Vulkan / GLFW / ImGui");
	if (ImGui::Button("Reset scene")) {
		resetScene();
	}

	if (ImGui::BeginTabBar("Controls")) {
	if (ImGui::BeginTabItem("Lighting")) {
		drawLightingControls();
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Objects and camera")) {
	if (ImGui::CollapsingHeader("Camera and projection", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped("WASD: move | Space / Ctrl: up / down | Arrows: look");
		if (ImGui::RadioButton("Perspective", camera.use_perspective)) camera.use_perspective = true;
		ImGui::SameLine();
		if (ImGui::RadioButton("Orthographic", !camera.use_perspective)) camera.use_perspective = false;
		if (camera.use_perspective) {
			ImGui::SliderFloat("Field of view", &camera.field_of_view_degrees, 25, 90, "%.0f deg");
		} else {
			ImGui::SliderFloat("View size", &camera.orthographic_half_height, 1, 6);
		}
		ImGui::SliderFloat("Movement speed", &camera.movement_speed, 0.5f, 12);
		ImGui::SliderFloat("Turn speed", &camera.turn_speed_degrees, 20, 180);
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
		ImGui::ColorEdit3("Albedo", glm::value_ptr(object.material.albedo));
		ImGui::ColorEdit3("Specular color", glm::value_ptr(object.material.specular_color));
		ImGui::SliderFloat("Shininess", &object.material.shininess, 1, 256, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::Checkbox("Use vertex colors", &object.material.use_vertex_color);
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
	ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
	}
	ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
	const auto extent = graphics::core::viewportArea().extent;
	if (extent.width == 0 || extent.height == 0) return;
	const float aspect = static_cast<float>(extent.width) / extent.height;
	const glm::mat4 view = camera.view_matrix();
	RenderFrame frame;
	frame.view_projection = camera.projection_matrix(aspect) * view;
	frame.camera_position = camera.position;
	frame.orthographic_view_direction = {view[0][2], view[1][2], view[2][2],
		camera.use_perspective ? 0.0f : 1.0f};
	frame.ambient = ambient;
	frame.directional = directional;
	frame.point_lights = point_lights;
	frame.spot_lights = spot_lights;
	if (show_room) appendRoom(frame);
	for (int i = 0; i < object_count; ++i) {
		frame.objects.push_back({
			.model = makeModelMatrix(objects[i], animation_time),
			.material = objects[i].material,
		});
	}
	if (show_light_markers) {
		for (const auto& light : point_lights) appendLightMarker(frame, light.position, light.color);
		for (const auto& light : spot_lights) {
			appendLightMarker(frame, light.position, light.color);
			geometry::append_spotlight_cone(frame.light_lines, light);
		}
	}
	graphics::core::render(fd, frame);
}

} // namespace application
