#include "PlayMode.hpp"

#include "gl_errors.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace {

constexpr float BeatInterval = 0.5f; //120bpm beat grid, for judgement; the metronome ticks sparser

//HSV(h, 1, 1) -> RGB for h in [0,1)
glm::vec3 hue_to_rgb(float h) {
	float r = std::abs(h * 6.0f - 3.0f) - 1.0f;
	float g = 2.0f - std::abs(h * 6.0f - 2.0f);
	float b = 2.0f - std::abs(h * 6.0f - 4.0f);
	return glm::clamp(glm::vec3(r, g, b), 0.0f, 1.0f);
}

//Karplus-Strong plucked string through a soft-clip "amp": cheap electric guitar
// ref: https://users.soe.ucsc.edu/~karplus/papers/digitar.pdf
Sound::Sample make_pluck(float freq) {
	constexpr uint32_t N = 48000; //1 second
	const uint32_t L = std::max(2u, uint32_t(48000.0f / freq + 0.5f)); //string period in samples
	std::vector< float > line(L);
	std::mt19937 rng(0xC0FFEE);
	std::uniform_real_distribution< float > noise(-1.0f, 1.0f);
	for (float &v : line) v = noise(rng);

	std::vector< float > data(N);
	for (uint32_t i = 0; i < N; ++i) {
		uint32_t j = i % L;
		data[i] = line[j];
		line[j] = 0.996f * 0.5f * (line[j] + line[(j + 1) % L]); //damped feedback controls sustain
	}
	for (float &v : data) v = std::tanh(2.5f * v); //overdriven amp
	return Sound::Sample(data);
}

} //namespace

PlayMode::PlayMode() : rng(std::random_device{}()) {
	//C major chord arpeggio across three octaves (C3 E3 G3 ... C6):
	const float freqs[] = {130.81f, 164.81f, 196.00f, 261.63f, 329.63f, 392.00f, 523.25f, 659.25f, 783.99f, 1046.50f};
	for (float f : freqs) {
		note_samples.emplace_back(make_pluck(f));
	}

	//core profile requires a bound VAO even when drawing with zero attributes:
	glGenVertexArrays(1, &empty_vao);
}

PlayMode::~PlayMode() {
	glDeleteVertexArrays(1, &empty_vao);
}

Sound::Sample PlayMode::make_beat_sample() {
	//kick drum: sine with a 150->50Hz pitch sweep and fast decay
	constexpr uint32_t N = 48000 / 5; //0.2 seconds
	std::vector< float > data(N);
	const float f0 = 50.0f, f1 = 100.0f, k = 30.0f;
	for (uint32_t i = 0; i < N; ++i) {
		float t = float(i) / 48000.0f;
		//phase is the integral of f0 + f1 * exp(-k * t):
		float phase = 2.0f * glm::pi< float >() * (f0 * t + (f1 / k) * (1.0f - std::exp(-k * t)));
		//tanh soft-clip at 2x gain: louder and punchier without hard clipping
		data[i] = std::tanh(2.0f * std::exp(-12.0f * t) * std::sin(phase));
	}
	return Sound::Sample(data);
}

void PlayMode::spawn_ripple(glm::vec2 const &pos, float amp) {
	ripples.push_back({pos, time, amp, hue_to_rgb(hue_dist(rng))});
	if (ripples.size() > RippleProgram::MaxRipples) ripples.pop_front();

	//x position picks the note from the grid; pan follows x:
	uint32_t note = std::min(uint32_t(note_samples.size()) - 1, uint32_t(pos.x * float(note_samples.size())));
	Sound::play(note_samples[note], 0.5f, pos.x * 2.0f - 1.0f);
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		int lane = -1;
		if (evt.key.key == SDLK_D) lane = 0;
		else if (evt.key.key == SDLK_F) lane = 1;
		else if (evt.key.key == SDLK_J) lane = 2;
		else if (evt.key.key == SDLK_K) lane = 3;
		else if (evt.key.key == SDLK_SPACE) {
			//random position in the central half of the screen:
			glm::vec2 pos(0.25f + 0.5f * hue_dist(rng), 0.25f + 0.5f * hue_dist(rng));
			spawn_ripple(pos, 1.0f);
			return true;
		}
		if (lane >= 0) {
			spawn_ripple(glm::vec2((float(lane) + 0.5f) / 4.0f, 0.35f), 1.0f);
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		//mouse position -> 0..1 fraction, lower-left origin:
		glm::vec2 pos(
			evt.button.x / float(window_size.x),
			1.0f - evt.button.y / float(window_size.y)
		);
		pos = glm::clamp(pos, 0.0f, 1.0f);
		spawn_ripple(pos, 1.0f);
		return true;
	}

	return false;
}

void PlayMode::update(float elapsed) {
	time += elapsed;

	//metronome ticks every other beat (1.0s), '+=' so that extra time within one frame wont accumulate to the next tick
	beat_timer -= elapsed;
	if (beat_timer <= 0.0f) {
		beat_timer += 2.0f * BeatInterval;
		Sound::play(beat_sample, 1.0f);
	}

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

		uint32_t n = std::min< uint32_t >(RippleProgram::MaxRipples, uint32_t(ripples.size()));
		glm::vec2 centers[RippleProgram::MaxRipples];
		float starts[RippleProgram::MaxRipples];
		float amps[RippleProgram::MaxRipples];
		glm::vec3 colors[RippleProgram::MaxRipples];
		uint32_t i = 0;
		for (Ripple const &r : ripples) {
			if (i >= n) break;
			centers[i] = r.pos;
			starts[i] = r.t0;
			amps[i] = r.amp;
			colors[i] = r.color;
			++i;
		}
		glUniform1i(ripple_program.ripple_count_int, GLint(i));
		if (i > 0) {
			glUniform2fv(ripple_program.ripple_center_vec2, GLsizei(i), glm::value_ptr(centers[0]));
			glUniform1fv(ripple_program.ripple_start_float, GLsizei(i), starts);
			glUniform1fv(ripple_program.ripple_amp_float, GLsizei(i), amps);
			glUniform3fv(ripple_program.ripple_color_vec3, GLsizei(i), glm::value_ptr(colors[0]));
		}

		glBindVertexArray(empty_vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glUseProgram(0);
	}

	GL_ERRORS();
}
