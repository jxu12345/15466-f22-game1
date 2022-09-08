#include "PlayMode.hpp"
#include "generated_assets.hpp"
//for the GL_ERRORS() macro:
#include "gl_errors.hpp"

//for glm::value_ptr() :
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <random>
#include <string>
#include <iostream>
#include <sstream>
// helper function to turn a number into bit0 and bit1
void numToBit(uint8_t num, uint8_t &bit0, uint8_t &bit1) {
	assert(num < 4);
	bit0 = num & 1;
	bit1 = (num >> 1) & 1;
}

// helper function to parse a 64 entry array into PPU466::Tile
void arrToTile(uint8_t arr[64], PPU466::Tile& tile) {
	// clear tile
	tile.bit0.fill(0);
	tile.bit1.fill(0);

	// fill tile with data
	for (uint32_t j = 0; j < 64; ++j) {
		uint8_t bit0, bit1;
		numToBit(arr[j], bit0, bit1);
		tile.bit0[7 - (j / 8)] |= bit0 << (7 - (j % 8));
		tile.bit1[7 - (j / 8)] |= bit1 << (7 - (j % 8));
	}
		
}

PlayMode::PlayMode() {
	// blank tile
	PPU466::Tile blank_tile;
	blank_tile.bit0.fill(0);
	blank_tile.bit1.fill(0);
	ppu.tile_table[255] = blank_tile;

	// initialize transparent barrier tile
	PPU466::Tile barrier_tile;
	barrier_tile.bit0.fill(0);
	barrier_tile.bit1.fill(0);
	// fill row 3-4 with white color, at index 1
	for (uint32_t i = 0; i < 8; i++) {
		barrier_tile.bit0[3] = 0xff;
		barrier_tile.bit0[4] = 0xff;
		// bit1 is always 0
	}
	// use index 254 on tile table for barrier tile
	ppu.tile_table[254] = barrier_tile;
	
	// load data for lander
	LanderData lander;
	// use palette table entry 0 for lander color scheme
	ppu.palette_table[0] = lander.color;
	// load tiles for lander, starting from tile 0
	for (uint32_t i = 0; i < 4; ++i) {
		arrToTile(lander.tile_inds[i], ppu.tile_table[i]);
	}

	// load data for background
	MoonData moon;
	// use palette table entry 1 for background color scheme
	ppu.palette_table[1] = moon.color;
	// load tiles for background, starting from tile 4
	uint32_t tile_count = moon.tileCount;
	for (uint32_t i = 0; i < tile_count; ++i) {
		arrToTile(moon.tile_inds[i], ppu.tile_table[i + 4]);
	}
	// load background map
	assert(ppu.background.size() == 64 * 60 && "background size is wrong");

	uint16_t moon_bitmap[64*60];
	for (uint32_t i = 0; i < ppu.background.size(); i++) {
		// format into 16 bit background format
		uint16_t bitmap = (1 << 8) | ((moon.backgroundTileNum[i] + 4) & 0xff); // palette 1 and starting from tile 4
		moon_bitmap[i] = bitmap;
	}

	// load data for stars
	StarData stars;
	// use palette table entry 2 for stars color scheme
	ppu.palette_table[2] = stars.color;
	// load tiles for stars, starting from tile 4 + moon's tile count
	assert(moon.tileCount + stars.tileCount < 250 && "too many tiles");
	for (uint32_t i = 0; i < stars.tileCount; ++i) {
		arrToTile(stars.tile_inds[i], ppu.tile_table[i + 4 + moon.tileCount]);
	}
	// load stars map
	uint16_t stars_bitmap[64*60];
	for (uint32_t i = 0; i < ppu.background.size(); i++) {
		// format into 16 bit background format
		uint16_t bitmap = (2 << 8) | ((stars.backgroundTileNum[i] + 4 + moon.tileCount) & 0xff); // palette 2 and starting from tile 4 + moon's tile count
		stars_bitmap[i] = bitmap;
	}

	// blend moon and star background, using moon tile if nonzero else star tile1
	for (uint32_t i = 0; i < ppu.background.size(); i++) {
		if ((moon_bitmap[i] & 0xff) == 4) { // first moon tile is blank
			ppu.background[i] = stars_bitmap[i];
		} else {
			ppu.background[i] = moon_bitmap[i];
		}
	}

	// copy to default background
	std::copy(ppu.background.begin(), ppu.background.end(), default_bg.begin());
	//solid color palette:
	ppu.palette_table[7] = {
		glm::u8vec4(0x00, 0x00, 0x00, 0x00),
		glm::u8vec4(0xff, 0xff, 0xff, 0xff),
		glm::u8vec4(0x00, 0x00, 0x00, 0xff),
		glm::u8vec4(0x00, 0x00, 0x00, 0x00),
	};

	// create level
	level = 1;
	reset_level(false, true);

}

PlayMode::~PlayMode() {
}

// helper function to draw course based on level
void PlayMode::generate_background_and_course(bool new_course) {
	// draw and place background
	ppu.background_position.x = 0;
	ppu.background_position.y = 0;
	assert(ppu.background_position.x < 32);
	assert(ppu.background_position.y < 20);
	
	for (uint32_t i = 0; i < ppu.background.size(); ++i) {
		ppu.background[i] = default_bg[i];
	}
	// generate new course
	if (new_course) {
		course = Course(level);
	}

	// draw barriers for course
	for (uint32_t i = 0; i < course.barriers.size(); i++) {
		Barrier barrier = course.barriers[i];
		// std::cout << "barrier " << std::to_string(i) << " x: " << std::to_string(barrier.x) << " y: " << std::to_string(barrier.y) << " w: " << std::to_string(barrier.w) << std::endl;
		// draw barrier
		for (uint32_t col = 0; col < PPU466::BackgroundWidth / 2; col++) {
			if ((col < barrier.x) || (col >= (uint32_t)(barrier.x + barrier.w))) {
				ppu.background[barrier.y * PPU466::BackgroundWidth + col] = (0xf << 8) | 254;
			}
		}
	}
}

void PlayMode::reset_level(bool level_incr, bool new_course) {
	// reset position, velocity of lander
	player_at = glm::vec2(0.0f, 0.0f);
	player_vel = glm::vec2(0.0f, 0.0f);

	// increment level
	if (level_incr) {
		level = (uint8_t)std::min(6, level + 1);
	}
	// reset course
	generate_background_and_course(new_course);
}

// check for collisions
bool PlayMode::check_collision() {
	
	// calculate hitbox bounds
	// for simplicity, hitbox is set to 8x8 square in middle
	uint32_t hitbox_left = (uint32_t)player_at.x + 4;
	uint32_t hitbox_right = (uint32_t)player_at.x + 12;
	uint32_t hitbox_bottom = (uint32_t)player_at.y + 4;
	uint32_t hitbox_top = (uint32_t)player_at.y + 12;

	for (auto barrier : course.barriers) {
		// check if sprite overlaps with barrier
		uint32_t barrier_y_pos = barrier.y * 8 + 3; // barrier position in pixels, offset to the line in middle
		uint32_t barrier_x_pos = barrier.x * 8;
		uint32_t barrier_w_pixels = barrier.w * 8; // barrier width in pixels

		if ((hitbox_top < barrier_y_pos) || (hitbox_bottom > barrier_y_pos)) {
			// sprite is below or above barrier
			continue;
		}
		else if ((hitbox_left > barrier_x_pos) && (hitbox_right < barrier_x_pos + barrier_w_pixels)) {
			// sprite is within barrier opening
			continue;
		}
		else {
			// some overlap between hitbox and barrier line, collision
			return true;
		}
	}
	// no collision
	return false;
}

// event callback function
bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_LEFT) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_RIGHT) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_UP) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_DOWN) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_LEFT) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_RIGHT) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_UP) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_DOWN) {
			down.pressed = false;
			return true;
		}
	}

	return false;
}

// update function
void PlayMode::update(float elapsed) {

	// update y position and velocity:
	// on ground
	if (player_at.y < 0) {
		player_at.y = 0;
		player_vel.y = 0.0f;
		player_vel.x = 0.0f;
	}
	// level successfully complete
	else if (player_at.y + 16 > PPU466::ScreenHeight) {
		reset_level(true, true); // increment level
	}
	// normal case
	else {
		player_at.y += player_vel.y * elapsed;
		// add effects of gravity
		player_vel.y += GravAccel * elapsed;
		// add effects of button presses
		if (down.pressed) player_vel.y  -= ThrusterAccel * elapsed;
		if (up.pressed) player_vel.y  += ThrusterAccel * elapsed;
	}

	// update x position and velocity:
	// edge case
	if (player_at.x < 0) {
		player_at.x = 0;
		player_vel.x = 0.0f;
	}
	else if (player_at.x + 16 > PPU466::ScreenWidth) {
		player_at.x = PPU466::ScreenWidth - 16;
		player_vel.x = 0.0f;
	}
	// normal case
	else {
		player_at.x += player_vel.x * elapsed;
		// add effects of button presses
		if (left.pressed) player_vel.x  -= RCSAccel * elapsed;
		if (right.pressed) player_vel.x  += RCSAccel * elapsed;
	}

	// collision checking
	if (check_collision()) {
		reset_level(false, false); // reset to same level
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//--- set ppu state based on game state ---

	 // set attribute: foreground, palette 0
	 uint8_t lander_attribute = 0;
	//player sprite:
	ppu.sprites[0].x = int8_t(player_at.x);
	ppu.sprites[0].y = int8_t(player_at.y);
	ppu.sprites[0].index = 3;
	ppu.sprites[0].attributes = lander_attribute;
	//sprites connected to player sprite:
	ppu.sprites[1].x = ppu.sprites[0].x + 8;
	ppu.sprites[1].y = ppu.sprites[0].y;
	ppu.sprites[1].index = 2;
	ppu.sprites[1].attributes = lander_attribute;

	ppu.sprites[2].x = ppu.sprites[0].x;
	ppu.sprites[2].y = ppu.sprites[0].y + 8;
	ppu.sprites[2].index = 1;
	ppu.sprites[2].attributes = lander_attribute;

	ppu.sprites[3].x = ppu.sprites[0].x + 8;
	ppu.sprites[3].y = ppu.sprites[0].y + 8;
	ppu.sprites[3].index = 0;
	ppu.sprites[3].attributes = lander_attribute;


	//some other misc sprites:
	for (uint32_t i = 4; i < 63; ++i) {	
		// move sprites out of bounds to hide them
		ppu.sprites[i].x = uint8_t(255);
		ppu.sprites[i].y = uint8_t(255);
	}

	//--- actually draw ---
	ppu.draw(drawable_size);
}
