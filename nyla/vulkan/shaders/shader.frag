#version 450

layout(location = 0) in vec3 frag_color;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = vec4(1,1,1,1);
	// out_color = vec4(frag_color, 1.0);
}

