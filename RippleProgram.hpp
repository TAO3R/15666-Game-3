#pragma once

#include "GL.hpp"

#include <cstdint>

//Full-screen "pixel ripple" effect:
// expanding monochrome rings on a black background,
// driven entirely by uniform data (no vertex buffers needed).
struct RippleProgram {
	RippleProgram();
	~RippleProgram();

	GLuint program = 0;

	//maximum number of simultaneous ripples; must match the array sizes in the shader:
	static constexpr uint32_t MaxRipples = 16;

	//uniform locations:
	GLuint drawable_size_vec2 = 0;
	GLuint time_float = 0;
	GLuint ripple_count_int = 0;
	GLuint ripple_center_vec2 = 0; //array of MaxRipples vec2, 0..1 screen-fraction coords, lower-left origin
	GLuint ripple_start_float = 0; //array of MaxRipples float, spawn time in seconds
	GLuint ripple_amp_float = 0;   //array of MaxRipples float, brightness multiplier
	GLuint ripple_color_vec3 = 0;  //array of MaxRipples vec3, per-ripple color
};
