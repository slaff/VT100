/**
	This file is part of FORTMAX kernel.

	FORTMAX kernel is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FORTMAX kernel is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FORTMAX kernel.  If not, see <http://www.gnu.org/licenses/>.

	Copyright: Martin K. Schröder (info@fortmax.se) 2014
*/

#pragma once

#include <cstdint>
#include <sming_attr.h>

class DisplayDevice
{
public:
	//	virtual void init(void);
	//	virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
	//	virtual void drawFastHLine(int16_t x, int16_t y, int16_t h, uint16_t color);
	//	virtual void setRotation(uint8_t m);

	virtual void drawString(uint16_t x, uint16_t y, const char* text) = 0;
	virtual void drawChar(uint16_t x, uint16_t y, uint8_t c) = 0;
	virtual void setBackColor(uint16_t col) = 0;
	virtual void setFrontColor(uint16_t col) = 0;
	virtual void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) = 0;

	virtual void scroll(uint16_t top, uint16_t bottom, int16_t diff) = 0;

	virtual uint16_t getWidth() = 0;
	virtual uint16_t getHeight() = 0;
	virtual uint8_t getCharWidth() = 0;
	virtual uint8_t getCharHeight() = 0;
};

class VT100Callbacks
{
public:
	virtual void sendResponse(const char* str) = 0;
};

// states
enum { STATE_IDLE, STATE_ESCAPE, STATE_COMMAND };

// events that are passed into states
enum {
	EV_CHAR = 1,
};

#define MAX_COMMAND_ARGS 4
class Vt100
{
public:
	Vt100(DisplayDevice& display, VT100Callbacks& callbacks) : display(display), callbacks(callbacks)
	{
	}

	void reset();
	void putc(uint8_t ch, unsigned count = 1);
	void puts(const char* str);
	size_t nputs(const char* str, size_t length);
	size_t printf(const char* fmt, ...);

protected:
#define VT100_STATE_MAP(XX)                                                                                            \
	XX(idle)                                                                                                           \
	XX(command_arg)                                                                                                    \
	XX(esc_sq_bracket)                                                                                                 \
	XX(esc_question)                                                                                                   \
	XX(esc_hash)                                                                                                       \
	XX(esc_left_br)                                                                                                    \
	XX(esc_right_br)                                                                                                   \
	XX(escape)

	enum class State {
#define XX(s) s,
		VT100_STATE_MAP(XX)
#undef XX
	};

	void resetScroll();
	void clearLines(uint16_t start_line, uint16_t end_line);
	void move(int16_t right_left, int16_t bottom_top);
	void drawCursor();
	void _putc(uint8_t ch);

	void _st_idle(uint8_t ev, uint16_t arg);
	void _st_command_arg(uint8_t ev, uint16_t arg);
	void _st_esc_sq_bracket(uint8_t ev, uint16_t arg);
	void _st_esc_question(uint8_t ev, uint16_t arg);
	void _st_esc_hash(uint8_t ev, uint16_t arg);
	void _st_esc_left_br(uint8_t ev, uint16_t arg);
	void _st_esc_right_br(uint8_t ev, uint16_t arg);
	void _st_escape(uint8_t ev, uint16_t arg);

	__forceinline void callState(uint8_t ev, uint16_t arg)
	{
		(this->*stateTable[unsigned(state)])(ev, arg);
	}

	__forceinline uint16_t VT100_X(uint16_t x)
	{
		return x * char_width;
	}

	__forceinline uint16_t VT100_Y(uint16_t y)
	{
		return y * char_height;
	}

	__forceinline uint16_t VT100_CURSOR_X()
	{
		return VT100_X(cursor_x);
	}

	__forceinline uint16_t VT100_CURSOR_Y()
	{
		return VT100_Y(cursor_y);
	}

private:
	using StateMethod = void (Vt100::*)(uint8_t ev, uint16_t arg);
	static const StateMethod stateTable[];

	union Flags {
		uint8_t val;
		struct {
			// 0 = cursor remains on last column when it gets there
			// 1 = lines wrap after last column to next line
			uint8_t cursor_wrap : 1;
			uint8_t scroll_mode : 1;
			uint8_t origin_mode : 1;
		};
	};
	Flags flags;

	// cursor position on the screen (0, 0) = top left corner.
	int16_t cursor_x, cursor_y;
	int16_t saved_cursor_x, saved_cursor_y; // used for cursor save restore
	int16_t scroll_start_row, scroll_end_row;
	// character width and height
	int8_t char_width, char_height;
	// Screem size in pixels
	uint16_t screen_width, screen_height;
	// Screen size in characters
	uint16_t row_count, col_count;
	// colors used for rendering current characters
	uint16_t back_color, front_color;
	// command arguments that get parsed as they appear in the terminal
	uint8_t narg;
	uint16_t args[MAX_COMMAND_ARGS];
	// current arg pointer (we use it for parsing)
	uint8_t carg;

	State state;
	State ret_state;

	DisplayDevice& display;
	VT100Callbacks& callbacks;
};
