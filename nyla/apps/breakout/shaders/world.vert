#version 460

layout(push_constant) uniform SceneUbo {
  mat4 view;
  mat4 proj;
} scene;

layout(set = 0, binding = 1) uniform EntityUbo {
  mat4 model;
} entity;

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 frag_color;

void main() {
  gl_Position = scene.proj * scene.view * entity.model * vec4(in_position, 0.f, 1.f);
  frag_color = in_color;
}