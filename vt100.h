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

	@author 2019 mikee47 <mike@sillyhouse.net>

	Modified from original code at https://github.com/mkschreder/avr-vt100
*/

#pragma once

#include <cstdint>
#include <sming_attr.h>

class DisplayDevice
{
public:
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

// events that are passed into states
enum {
	EV_CHAR = 1,
};

#define VT100_STATE_MAP(XX)                                                                                            \
	XX(idle)                                                                                                           \
	XX(command_arg)                                                                                                    \
	XX(esc_sq_bracket)                                                                                                 \
	XX(esc_question)                                                                                                   \
	XX(esc_hash)                                                                                                       \
	XX(esc_left_br)                                                                                                    \
	XX(esc_right_br)                                                                                                   \
	XX(escape)

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

	uint16_t width() const
	{
		return colCount;
	}

	uint16_t height() const
	{
		return rowCount;
	}

	uint16_t getRowCount() const
	{
		return rowCount;
	}

	uint16_t getColumnCount() const
	{
		return colCount;
	}

protected:
	enum class State {
#define XX(s) s,
		VT100_STATE_MAP(XX)
#undef XX
	};

	void resetScroll();
	void clearLines(uint16_t start_line, uint16_t end_line);
	void move(int16_t right_left, int16_t bottom_top);
	void drawCursor();
	void putcInternal(uint8_t ch);

#define XX(s) void state_##s(uint8_t ev, uint16_t arg);
	VT100_STATE_MAP(XX)
#undef XX

	__forceinline void callState(uint8_t ev, uint16_t arg)
	{
		(this->*stateTable[unsigned(state)])(ev, arg);
	}

private:
	using StateMethod = void (Vt100::*)(uint8_t ev, uint16_t arg);
	static const StateMethod stateTable[];

	union Flags {
		uint8_t val;
		struct {
			// 0 = cursor remains on last column when it gets there
			// 1 = lines wrap after last column to next line
			bool cursor_wrap : 1;
			bool scroll_mode : 1;
			bool origin_mode : 1;
		};
	};
	Flags flags;

	struct Pos {
		uint16_t col;
		uint16_t row;
	};

	// cursor position on the screen (0, 0) = top left corner.
	Pos cursorPos;
	Pos savedCursorPos;
	uint16_t scrollStartRow;
	uint16_t scrollEndRow;
	// Screem size in pixels
	uint16_t screenWidth;
	uint16_t screenHeight;
	// Screen size in characters
	uint16_t rowCount, colCount;
	// colors used for rendering current characters
	uint16_t backColor;
	uint16_t frontColor;
	//
	uint8_t charWidth;
	uint8_t charHeight;

	// command arguments that get parsed as they appear in the terminal
	struct Args {
		uint16_t values[4];
		uint8_t count;

		__forceinline void addDigit(char c)
		{
			values[count] *= 10;
			values[count] += c - '0';
		}

		uint16_t operator[](unsigned index) const
		{
			return values[index];
		}
	};
	Args args;

	State state;
	State ret_state;

	DisplayDevice& display;
	VT100Callbacks& callbacks;
};
