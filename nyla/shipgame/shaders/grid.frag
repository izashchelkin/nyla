#version 460

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 out_color;

float grid_size = 50.f;

void main() {
	if (false) {
		if (mod(gl_FragCoord.y, grid_size) >= 1 && mod(gl_FragCoord.x, grid_size) > 1)
			discard;

		out_color = vec4(0.1, 0.1, 0.1, (500 - length(gl_FragCoord.xy - vec2(500, 500)))/500.f);
	}
	else {
		if (uv.x == 0)
			discard;

		out_color = vec4(1, 1, 1, 0.5);
	}
}

