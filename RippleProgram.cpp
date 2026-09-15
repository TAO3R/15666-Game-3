#include "RippleProgram.hpp"

#include "gl_compile_program.hpp"

RippleProgram::RippleProgram() {
	program = gl_compile_program(
		//vertex shader: full-screen triangle generated from gl_VertexID, no attributes needed
		"#version 330\n"
		"void main() {\n"
		"	vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,\n"
		"	              (gl_VertexID == 2) ? 3.0 : -1.0);\n"
		"	gl_Position = vec4(p, 0.0, 1.0);\n"
		"}\n"
	,
		//fragment shader: pixelated expanding rings
		//NOTE: array sizes must match RippleProgram::MaxRipples
		"#version 330\n"
		"uniform vec2 drawable_size;\n"
		"uniform float time;\n"
		"uniform int ripple_count;\n"
		"uniform vec2 ripple_center[16];\n"
		"uniform float ripple_start[16];\n"
		"uniform float ripple_amp[16];\n"
		"uniform vec3 ripple_color[16];\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		//	pixelation: snap fragment position to a coarse grid
		"	float cell = 14.0;\n"
		"	vec2 frag = (floor(gl_FragCoord.xy / cell) + 0.5) * cell;\n"
		"	vec3 e = vec3(0.0);\n"
		"	for (int i = 0; i < 16; ++i) {\n"
		"		if (i >= ripple_count) break;\n"
		"		float age = time - ripple_start[i];\n"
		//		radius, distance and thickness are all snapped to whole cells,
		//		 so rings stay blocky at any size:
		"		float radius = floor(300.0 * age / cell) * cell;\n"
		"		float d = floor(distance(frag, ripple_center[i] * drawable_size) / cell) * cell;\n"
		"		float w = (1.0 + floor(age * 1.5)) * cell;\n"
		"		float ring = step(abs(d - radius), w * 0.5);\n"
		"		float fade = exp(-2.0 * age);\n"
		"		e += ring * fade * ripple_amp[i] * ripple_color[i];\n"
		"	}\n"
		"	fragColor = vec4(clamp(e, 0.0, 1.0), 1.0);\n"
		"}\n"
	);

	drawable_size_vec2 = glGetUniformLocation(program, "drawable_size");
	time_float = glGetUniformLocation(program, "time");
	ripple_count_int = glGetUniformLocation(program, "ripple_count");
	ripple_center_vec2 = glGetUniformLocation(program, "ripple_center[0]");
	ripple_start_float = glGetUniformLocation(program, "ripple_start[0]");
	ripple_amp_float = glGetUniformLocation(program, "ripple_amp[0]");
	ripple_color_vec3 = glGetUniformLocation(program, "ripple_color[0]");
}

RippleProgram::~RippleProgram() {
	glDeleteProgram(program);
	program = 0;
}
