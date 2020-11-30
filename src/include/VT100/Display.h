/**
 * Display.h
 *
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

namespace VT100
{
class Display
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

} // namespace VT100
