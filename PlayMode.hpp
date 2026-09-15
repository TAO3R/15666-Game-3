#include "Mode.hpp"

#include "RippleProgram.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

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
	std::uniform_real_distribution< float > hue_dist{0.0f, 1.0f};

	void spawn_ripple(glm::vec2 const &pos, float amp);

	//ripple shader + empty vao needed for attribute-less full-screen drawing:
	RippleProgram ripple_program;
	GLuint empty_vao = 0;

	//procedurally generated key sounds, one per lane (no asset files needed):
	std::vector< Sound::Sample > lane_samples;
};
