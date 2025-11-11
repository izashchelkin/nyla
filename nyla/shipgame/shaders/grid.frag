#version 460

layout(set = 0, binding = 0) uniform SceneUbo {
  mat4 view;
  mat4 proj;
} scene;

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 out_color;

void main() {
  mat4 invvp = inverse(proj * view);

  float grid_size = .1f;
  float grid_thickness = .005f;

  if (mod(uv.x, grid_size) >= grid_thickness && mod(uv.y, grid_size) >= grid_thickness)
    discard;

  // if (mod(uv.y, 0.20f) < .01f)
  //   discard;

  out_color = vec4(1.f, 1.f, 1.f, 1.f - length(vec2(0.5, 0.5) - uv)*3);
}