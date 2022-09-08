#include "PPU466.hpp"
#include "Mode.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <random> // for std::mt19937
#include <chrono> // for std::chrono
#include <iostream>

// gravity and button acceleration values
constexpr float GravAccel = -100.0f;
constexpr float ThrusterAccel = 200.0f;
constexpr float RCSAccel = 100.0f;


// barrier struct
struct Barrier {
	// place at which barrier is located
	uint8_t x=0;
	// bottom of opening
	uint8_t y=0;
	// height of opening
	uint8_t w=0;
};

// obstacle course formed by multiple barriers
struct Course {
	// list of barriers
	std::vector<Barrier> barriers;
	
	// construct barriers, specified by tile coordinate
	Course(const uint8_t num_barriers=4) {
		// clear barriers
		barriers.clear();
		// seed for random generator
		// random generator code from https://www.learncpp.com/cpp-tutorial/generating-random-numbers-using-mersenne-twister/
		std::mt19937 mt{ static_cast<unsigned int>(
			std::chrono::steady_clock::now().time_since_epoch().count()
		) };	

		// leave room for 4 tiles at left and right of screen to allow maneuver space
		uint8_t course_start = 0;
		uint8_t course_end = PPU466::BackgroundHeight / 2;

		for (uint8_t i = 0; i < num_barriers; i++) {
			// generate random barrier
			Barrier b;
			// generate x coordinates
			b.y = course_start + (course_end - course_start) / (num_barriers + 1) * (i + 1) + 2;
			// generate random y coordinates, leaving room for max 8 tile opening
			b.x = mt() % ((PPU466::BackgroundWidth / 2) - 8);
			// generate random height, leaving room for min 4 and max 6 tile opening
			b.w = (mt() % 4) + 4;
			// add barrier to course
			barriers.push_back(b);
		}
	};
};

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	// default background 
	std::array<uint16_t, PPU466::BackgroundWidth * PPU466::BackgroundHeight> default_bg;

	// obstacle course
	Course course;

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;

	//player position:
	glm::vec2 player_at = glm::vec2(0.0f);
	glm::vec2 player_vel = glm::vec2(0.0f);

	// level
	uint8_t level = 1;
	// course generation
	void generate_background_and_course(bool new_course = true);
	// reset level
	void reset_level(bool level_incr = false, bool new_course = true);

	// check for collision with course
	bool check_collision();

	//----- drawing handled by PPU466 -----
	PPU466 ppu;
};
