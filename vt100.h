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

#include <stdint.h>

//#define VT100_SCREEN_WIDTH term->display->getWidth()
//#define VT100_SCREEN_HEIGHT term->display->getHeight()
//#define VT100_CHAR_WIDTH 9
//#define VT100_CHAR_HEIGHT 13
//#define VT100_HEIGHT (VT100_SCREEN_HEIGHT / VT100_CHAR_HEIGHT)
//#define VT100_WIDTH (VT100_SCREEN_WIDTH / VT100_CHAR_WIDTH)

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

	virtual void setScrollStart(uint16_t start) = 0;
	virtual void setScrollMargins(uint16_t top, uint16_t bottom) = 0;

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

void vt100_init(DisplayDevice* display, VT100Callbacks* callbacks);
void vt100_putc(uint8_t ch);
void vt100_puts(const char* str);
size_t vt100_nputs(const char* str, size_t length);
