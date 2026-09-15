#include "PlayMode.hpp"

#include "gl_errors.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

PlayMode::PlayMode() {
	//procedurally generate a short beep per lane (C5 D5 E5 G5 -- pentatonic-ish, hard to make sound bad):
	const float freqs[4] = {523.25f, 587.33f, 659.25f, 783.99f};
	constexpr uint32_t N = 48000 / 4; //0.25 seconds at 48kHz
	for (float f : freqs) {
		std::vector< float > data(N);
		for (uint32_t i = 0; i < N; ++i) {
			float t = float(i) / 48000.0f;
			float env = std::exp(-12.0f * t);
			data[i] = env * (0.6f * std::sin(2.0f * glm::pi< float >() * f * t)
			               + 0.2f * std::sin(4.0f * glm::pi< float >() * f * t));
		}
		lane_samples.emplace_back(data);
	}

	//core profile requires a bound VAO even when drawing with zero attributes:
	glGenVertexArrays(1, &empty_vao);
}

PlayMode::~PlayMode() {
	glDeleteVertexArrays(1, &empty_vao);
}

void PlayMode::spawn_ripple(glm::vec2 const &pos, float amp) {
	ripples.push_back({pos, time, amp});
	if (ripples.size() > RippleProgram::MaxRipples) ripples.pop_front();
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		int lane = -1;
		if (evt.key.key == SDLK_A) lane = 0;
		else if (evt.key.key == SDLK_S) lane = 1;
		else if (evt.key.key == SDLK_D) lane = 2;
		else if (evt.key.key == SDLK_F) lane = 3;
		if (lane >= 0) {
			glm::vec2 pos((float(lane) + 0.5f) / 4.0f, 0.35f);
			spawn_ripple(pos, 1.0f);
			//pan follows lane position, so the sound stage matches the visuals:
			Sound::play(lane_samples[lane], 0.5f, pos.x * 2.0f - 1.0f);
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		//SDL mouse coords are window layout pixels with upper-left origin;
		// ripples want 0..1 fractions with lower-left origin:
		glm::vec2 pos(
			evt.button.x / float(window_size.x),
			1.0f - evt.button.y / float(window_size.y)
		);
		pos = glm::clamp(pos, 0.0f, 1.0f);
		spawn_ripple(pos, 1.0f);
		int lane = std::min(3, std::max(0, int(pos.x * 4.0f)));
		Sound::play(lane_samples[lane], 0.5f, pos.x * 2.0f - 1.0f);
		return true;
	}

	return false;
}

void PlayMode::update(float elapsed) {
	time += elapsed;

	//cull ripples that have faded to invisibility:
	while (!ripples.empty() && time - ripples.front().t0 > 3.0f) {
		ripples.pop_front();
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	{ //draw ripples (full-screen triangle):
		glUseProgram(ripple_program.program);

		glUniform2f(ripple_program.drawable_size_vec2, float(drawable_size.x), float(drawable_size.y));
		glUniform1f(ripple_program.time_float, time);
		glUniform3f(ripple_program.color_vec3, 0.2f, 1.0f, 0.9f);

		uint32_t n = std::min< uint32_t >(RippleProgram::MaxRipples, uint32_t(ripples.size()));
		glm::vec2 centers[RippleProgram::MaxRipples];
		float starts[RippleProgram::MaxRipples];
		float amps[RippleProgram::MaxRipples];
		uint32_t i = 0;
		for (Ripple const &r : ripples) {
			if (i >= n) break;
			centers[i] = r.pos;
			starts[i] = r.t0;
			amps[i] = r.amp;
			++i;
		}
		glUniform1i(ripple_program.ripple_count_int, GLint(i));
		if (i > 0) {
			glUniform2fv(ripple_program.ripple_center_vec2, GLsizei(i), glm::value_ptr(centers[0]));
			glUniform1fv(ripple_program.ripple_start_float, GLsizei(i), starts);
			glUniform1fv(ripple_program.ripple_amp_float, GLsizei(i), amps);
		}

		glBindVertexArray(empty_vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glUseProgram(0);
	}

	GL_ERRORS();
}
