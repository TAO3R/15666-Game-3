#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "PathFont.hpp"
#include "gl_errors.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace {

constexpr float BeatInterval = 0.5f;  //120bpm beat grid
constexpr float HitWindow = 0.1f;     //a press within +-this of a beat counts as that beat
constexpr uint32_t EnteringBeats = 6; //3s countdown before the first round

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

//width of a string in DrawLines units (1 unit == glyph height), mirroring DrawLines::draw_text's advance
float text_width(std::string const &text) {
	float w = 0.0f;
	uint32_t start = 0;
	while (start < text.size()) {
		uint32_t end = start;
		uint32_t glyph = -1U;
		while (end < text.size()) {
			end += 1;
			auto f = PathFont::font.glyph_map.find(text.substr(start, end - start));
			if (f == PathFont::font.glyph_map.end()) {
				end -= 1;
				break;
			}
			glyph = f->second;
		}
		if (glyph == -1U) {
			end += 1;
			w += 0.6f; //tofu advance, same as DrawLines
		} else {
			w += PathFont::font.glyph_widths[glyph];
		}
		start = end;
	}
	return w;
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

Sound::Sample PlayMode::make_beat_sample(float pitch) {
	//kick drum: sine with a pitch sweep and fast decay; 'pitch' scales the sweep (150->50Hz at 1.0)
	constexpr uint32_t N = 48000 / 5; //0.2 seconds
	std::vector< float > data(N);
	const float f0 = 50.0f * pitch, f1 = 100.0f * pitch, k = 30.0f;
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
	ripples.push_back({pos, time, amp, hue_to_rgb(uniform_dist(rng))});
	if (ripples.size() > RippleProgram::MaxRipples) ripples.pop_front();

	//x position picks the note from the grid; pan follows x:
	uint32_t note = std::min(uint32_t(note_samples.size()) - 1, uint32_t(pos.x * float(note_samples.size())));
	Sound::play(note_samples[note], 0.5f, pos.x * 2.0f - 1.0f);
}

void PlayMode::start_round() {
	round_beat = 0;
	round_failed = false;
	player_hits.fill(false);

	//pick 2-5 of the 8 beats:
	std::uniform_int_distribution< uint32_t > count_dist(2, 5);
	uint32_t k = count_dist(rng);
	pattern.fill(false);
	std::array< uint32_t, PhraseBeats > order = {0, 1, 2, 3, 4, 5, 6, 7};
	std::shuffle(order.begin(), order.end(), rng);
	for (uint32_t i = 0; i < k; ++i) pattern[order[i]] = true;
}

void PlayMode::on_beat() {
	if (state == State::Entering && beat_count >= EnteringBeats) {
		state = State::Playing;
		start_round();
	}

	bool downbeat = false;
	if (state == State::Playing) {
		if (round_beat >= 2 * PhraseBeats) { //round over: check the reproduction
			bool ok = !round_failed;
			for (uint32_t i = 0; i < PhraseBeats; ++i) {
				if (pattern[i] != player_hits[i]) { ok = false; break; }
			}
			score = (ok ? score + 1 : 0);
			start_round();
		}
		downbeat = (round_beat % PhraseBeats == 0);
	}

	//phrase downbeats get a higher-pitched kick so the 8-beat boundary stays audible:
	if (downbeat) Sound::play(accent_sample, 1.0f);
	else if (beat_count % 2 == 0) Sound::play(beat_sample, 1.0f);

	if (state == State::Playing) {
		if (round_beat < PhraseBeats && pattern[round_beat]) {
			//demo cue: ripple + note up high, at a random x
			spawn_ripple(glm::vec2(0.2f + 0.6f * uniform_dist(rng), 0.65f), 1.0f);
		}
		++round_beat;
	}
}

void PlayMode::press(glm::vec2 const &pos) {
	//snap to the closest beat on the grid:
	float offset = time - last_beat_at;
	uint32_t closest = round_beat - 1;
	float err = offset;
	if (offset > BeatInterval * 0.5f) {
		closest = round_beat;
		err = BeatInterval - offset;
	}
	if (closest < PhraseBeats || closest >= 2 * PhraseBeats) return; //input only during the second phrase
	spawn_ripple(pos, 1.0f);

	uint32_t b = closest - PhraseBeats;
	if (err <= HitWindow && pattern[b] && !player_hits[b]) {
		player_hits[b] = true;
	} else {
		round_failed = true; //off-grid, wrong beat, or double press
	}
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &) {
	if (evt.type != SDL_EVENT_KEY_DOWN) return false;

	if (evt.key.key == SDLK_ESCAPE) {
		if (state != State::PreGame) {
			state = State::PreGame;
			return true;
		}
		return false;
	}

	switch (state) {
	case State::PreGame: //any other key starts the game
		state = State::Entering;
		entering_started_at = time;
		beat_count = 0;
		beat_timer = 0.0f;
		score = 0;
		return true;
	case State::Entering:
		return true; //countdown swallows keys
	case State::Playing: {
		int lane = -1;
		if (evt.key.key == SDLK_D) lane = 0;
		else if (evt.key.key == SDLK_F) lane = 1;
		else if (evt.key.key == SDLK_J) lane = 2;
		else if (evt.key.key == SDLK_K) lane = 3;
		if (lane >= 0) {
			press(glm::vec2((float(lane) + 0.5f) / 4.0f, 0.35f));
			return true;
		} else if (evt.key.key == SDLK_SPACE) {
			press(glm::vec2(0.25f + 0.5f * uniform_dist(rng), 0.25f + 0.5f * uniform_dist(rng)));
			return true;
		}
		return false;
	}
	}
	return false;
}

void PlayMode::update(float elapsed) {
	time += elapsed;

	//run the beat clock outside of PreGame ('+=' so the grid doesn't drift):
	if (state != State::PreGame) {
		beat_timer -= elapsed;
		if (beat_timer <= 0.0f) {
			last_beat_at = time + beat_timer; //exact beat moment; beat_timer <= 0 here
			beat_timer += BeatInterval;
			on_beat();
			++beat_count;
		}
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

	{ //HUD text:
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		//centers text horizontally at (0, y) with glyph height h:
		auto draw_centered = [&](std::string const &text, float y, float h) {
			float w = h * text_width(text);
			lines.draw_text(text, glm::vec3(-0.5f * w, y, 0.0f),
				glm::vec3(h, 0.0f, 0.0f), glm::vec3(0.0f, h, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0xff));
		};

		if (state == State::PreGame) {
			draw_centered("PRESS ANY KEY TO START", -0.04f, 0.08f);
		} else if (state == State::Entering) {
			float remaining = EnteringBeats * BeatInterval - (time - entering_started_at);
			draw_centered(std::to_string(std::max(1, int(std::ceil(remaining)))), -0.2f, 0.4f);
		} else { //Playing
			lines.draw_text("SCORE " + std::to_string(score),
				glm::vec3(-aspect + 0.05f, 0.92f, 0.0f),
				glm::vec3(0.06f, 0.0f, 0.0f), glm::vec3(0.0f, 0.06f, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0xff));
			draw_centered(round_beat - 1 < PhraseBeats ? "LISTEN" : "REPEAT", 0.84f, 0.06f);
		}
	}
	GL_ERRORS();
}
