#version 460

layout(set = 0, binding = 0) uniform StaticUbo {
  float window_width;
  float window_height;
} static_ubo;

layout(location = 0) in vec3 in_position;

void main() {
  float pixelx = int(in_position.x + static_ubo.window_width) % int(static_ubo.window_width);
  float pixely = int(in_position.y + static_ubo.window_height) % int(static_ubo.window_height);

  float ndc_x = 2.f * pixelx / static_ubo.window_width  - 1.f;
  float ndc_y = 1.f - 2.f * pixely / static_ubo.window_height;
  gl_Position = vec4(ndc_x, ndc_y, in_position.z, 1.f);
}