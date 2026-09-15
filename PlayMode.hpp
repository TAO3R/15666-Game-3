#include "Mode.hpp"

#include "RippleProgram.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

#include <array>
#include <deque>
#include <random>
#include <vector>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	enum class State { PreGame, Entering, Playing };
	State state = State::PreGame;

	//ripple effect state:
	struct Ripple {
		glm::vec2 pos;   //0..1 screen-fraction coordinates, lower-left origin
		float t0;        //spawn time in seconds (on the 'time' clock below)
		float amp;       //brightness multiplier
		glm::vec3 color; //assigned at spawn
	};
	std::deque< Ripple > ripples;
	float time = 0.0f; //accumulated game time; drives the ripple shader

	std::mt19937 rng;
	std::uniform_real_distribution< float > uniform_dist{0.0f, 1.0f};

	void spawn_ripple(glm::vec2 const &pos, float amp);

	//beat clock, runs during Entering and Playing:
	float beat_timer = 0.0f;   //counts down to the next beat
	uint32_t beat_count = 0;   //beats since the clock (re)started
	float last_beat_at = 0.0f; //'time' value of the most recent beat
	float entering_started_at = 0.0f;
	void on_beat();

	//one round = two 8-beat phrases: pattern demo, then player reproduction:
	static constexpr uint32_t PhraseBeats = 8;
	std::array< bool, PhraseBeats > pattern;
	std::array< bool, PhraseBeats > player_hits;
	uint32_t strip_mask = 0; //bit i: strip for beat i is lit
	uint32_t round_beat = 0; //next beat to fire within the round
	bool round_failed = false;
	uint32_t score = 0;

	void start_round();
	void press(glm::vec2 const &pos); //judge a gameplay key press and give feedback

	//120bpm metronome:
	static Sound::Sample make_beat_sample(float pitch);
	Sound::Sample beat_sample = make_beat_sample(1.0f);
	Sound::Sample accent_sample = make_beat_sample(2.0f); //octave-up kick for phrase downbeats

	//ripple shader + empty vao needed for attribute-less full-screen drawing:
	RippleProgram ripple_program;
	GLuint empty_vao = 0;

	//procedurally generated notes; screen width is a pitch grid, one note per cell:
	std::vector< Sound::Sample > note_samples;
};
