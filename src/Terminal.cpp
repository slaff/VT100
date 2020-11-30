/**
	This file is part of FORTMAX.

	FORTMAX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FORTMAX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FORTMAX.  If not, see <http://www.gnu.org/licenses/>.

	Copyright: Martin K. Schröder (info@fortmax.se) 2014
*/

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <stringutil.h>
#include <m_printf.h>

#include "include/VT100/Terminal.h"

#define KEY_ESC 0x1b
#define KEY_DEL 0x7f
#define KEY_BELL 0x07

namespace VT100
{
const Terminal::StateMethod Terminal::stateTable[] = {
#define XX(s) &Terminal::state_##s,
	VT100_STATE_MAP(XX)
#undef XX
};

void Terminal::reset()
{
	charHeight = display.getCharHeight();
	charWidth = display.getCharWidth();
	screenWidth = display.getWidth();
	screenHeight = display.getHeight();
	rowCount = screenHeight / charHeight;
	colCount = screenWidth / charWidth;
	backColor = 0x0000;
	frontColor = 0xffff;
	cursorPos = {};
	savedCursorPos = {};
	args = {};
	state = State::idle;
	ret_state = State::idle;
	resetScroll();
	flags.val = 0;
	display.setFrontColor(frontColor);
	display.setBackColor(backColor);
}

void Terminal::resetScroll()
{
	scrollStartRow = 0;
	scrollEndRow = rowCount - 1;
}

void Terminal::clearLines(uint16_t start_line, uint16_t end_line)
{
	for(int c = start_line; c <= end_line; c++) {
		uint16_t cy = cursorPos.row;
		cursorPos.row = c;
		display.fillRect(0, cursorPos.row * charHeight, screenWidth, charHeight, 0x0000);
		cursorPos.row = cy;
	}
}

// moves the cursor relative to current cursor position and scrolls the screen
void Terminal::move(int16_t right_left, int16_t bottom_top)
{
	// calculate how many lines we need to move down or up if x movement goes outside screen
	int16_t new_x = right_left + cursorPos.col;
	if(new_x >= colCount) {
		if(flags.cursor_wrap) {
			bottom_top += new_x / colCount;
			cursorPos.col = new_x % colCount;
		} else {
			cursorPos.col = colCount;
		}
	} else if(new_x < 0) {
		bottom_top += new_x / colCount;
		cursorPos.col = colCount - (abs(new_x) % colCount);
	} else {
		cursorPos.col = new_x;
	}

	if(bottom_top != 0) {
		int16_t new_y = cursorPos.row + bottom_top;
		// bottom margin 39 marks last line as static on 40 line display
		// therefore, we would scroll when new cursor has moved to line 39
		if(new_y > scrollEndRow) {
			cursorPos.row = scrollEndRow;
		} else if(new_y < scrollStartRow) {
			cursorPos.row = scrollStartRow;
		} else {
			cursorPos.row = new_y;
			return;
		}

		// scrolls the scroll region up (lines > 0) or down (lines < 0)
		auto lines = new_y - cursorPos.row;
		display.scroll(scrollStartRow * charHeight, ((1 + scrollEndRow) * charHeight) - 1, lines * charHeight);

		// clearing of lines that we have scrolled up or down
		if(lines > 0) {
			clearLines(1 + scrollEndRow - lines, scrollEndRow);
		} else {
			clearLines(scrollStartRow, scrollStartRow - lines - 1);
		}
	}
}

void Terminal::drawCursor()
{
	//uint16_t x = t->cursorPos.col * t->char_width;
	//uint16_t y = t->cursorPos.row * t->char_height;

	//display.fillRect(x, y, t->char_width, t->char_height, t->front_color);
}

// sends the character to the display and updates cursor position
void Terminal::putcInternal(uint8_t ch)
{
	if(ch < 0x20 || ch > 0x7e) {
		putcInternal('0');
		putcInternal('x');
		putcInternal(hexchar((ch & 0xf0) >> 4));
		putcInternal(hexchar(ch & 0x0f));
		return;
	}

	display.setFrontColor(frontColor);
	display.setBackColor(backColor);
	display.drawChar(cursorPos.col * charWidth, cursorPos.row * charHeight, ch);

	// move cursor right
	move(1, 0);
	drawCursor();
}

void Terminal::state_command_arg(uint8_t ev, uint16_t arg)
{
	if(ev != EV_CHAR) {
		return;
	}

	if(isdigit(arg)) { // a digit argument
		args.addDigit(arg);
		return;
	}

	if(arg == ';') { // separator
		++args.count;
		return;
	}

	// no more arguments, go back to command state
	++args.count;
	state = ret_state;
	ret_state = State::idle;

	// execute next state as well because we have already consumed a char!
	callState(ev, arg);
}

void Terminal::state_esc_sq_bracket(uint8_t ev, uint16_t arg)
{
	if(ev != EV_CHAR) {
		// for all other events restore normal mode
		state = State::idle;
		return;
	}

	if(isdigit(arg)) {
		// start of an argument
		ret_state = State::esc_sq_bracket;
		state_command_arg(ev, arg);
		state = State::command_arg;
		return;
	}

	if(arg == ';') {
		// skip arg separator, and stay in the command state
		return;
	}

	// otherwise we execute the command and go back to idle
	switch(arg) {
	// move cursor up (cursor stops at top margin)
	case 'A': {
		int n = (args.count > 0) ? args[0] : 1;
		cursorPos.row -= n;
		if(cursorPos.row < scrollStartRow) {
			cursorPos.row = scrollStartRow;
		}
		state = State::idle;
		break;
	}

	// cursor down (cursor stops at bottom margin)
	case 'B': {
		int n = (args.count > 0) ? args[0] : 1;
		cursorPos.row += n;
		if(cursorPos.row > scrollEndRow) {
			cursorPos.row = scrollEndRow;
		}
		state = State::idle;
		break;
	}

	// cursor right (cursor stops at right margin)
	case 'C': {
		int n = (args.count > 0) ? args[0] : 1;
		cursorPos.col += n;
		if(cursorPos.col > colCount)
			cursorPos.col = colCount;
		state = State::idle;
		break;
	}

	// cursor left
	case 'D': {
		auto n = (args.count > 0) ? args[0] : 1;
		cursorPos.col -= n;
		if(cursorPos.col < 0) {
			cursorPos.col = 0;
		}
		state = State::idle;
		break;
	}

	// move cursor to position (default 0;0)
	case 'f':
	case 'H':
		// cursor stops at respective margins
		cursorPos.col = (args.count >= 1) ? (args[1] - 1) : 0;
		cursorPos.row = (args.count == 2) ? (args[0] - 1) : 0;

		if(flags.origin_mode) {
			cursorPos.row += scrollStartRow;
			if(cursorPos.row > scrollEndRow) {
				cursorPos.row = scrollEndRow;
			}
		}

		if(cursorPos.col >= colCount) {
			cursorPos.col = colCount - 1;
		}
		if(cursorPos.row >= rowCount) {
			cursorPos.row = rowCount - 1;
		}

		state = State::idle;
		break;

	// clear screen from cursor up or down
	case 'J':
		if(args.count == 0 || (args.count == 1 && args[0] == 0)) {
			// clear down to the bottom of screen (including cursor)
			clearLines(cursorPos.row, rowCount);
		} else if(args.count == 1 && args[0] == 1) {
			// clear top of screen to current line (including cursor)
			clearLines(0, cursorPos.row);
		} else if(args.count == 1 && args[0] == 2) {
			// clear whole screen
			clearLines(0, rowCount);
			// reset scroll value
			resetScroll();
		}
		state = State::idle;
		break;

	// clear line from cursor right/left
	case 'K': {
		uint16_t x = cursorPos.col * charWidth;
		uint16_t y = cursorPos.row * charHeight;

		if(args.count == 0 || (args.count == 1 && args[0] == 0)) {
			// clear to end of line (to \n or to edge?), including cursor
			display.fillRect(x, y, screenWidth - x, charHeight, backColor);
		} else if(args.count == 1 && args[0] == 1) {
			// clear from left to current cursor position
			display.fillRect(0, y, x + charWidth, charHeight, backColor);
		} else if(args.count == 1 && args[0] == 2) {
			// clear whole current line
			display.fillRect(0, y, screenWidth, charHeight, backColor);
		}
		state = State::idle;
		break;
	}

	// insert lines (args[0] = number of lines)
	case 'L':
	// delete lines (args[0] = number of lines)
	case 'M':
		state = State::idle;
		break;

	// delete characters args[0] or 1 in front of cursor
	case 'P': {
		// TODO: this needs to correctly delete n chars
		int n = ((args.count > 0) ? args[0] : 1);
		move(-n, 0);
		for(int c = 0; c < n; c++) {
			putcInternal(' ');
		}
		state = State::idle;
		break;
	}

	// query device code
	case 'c':
		callbacks.sendResponse("\e[?1;0c");
		state = State::idle;
		break;

	case 'x':
		state = State::idle;
		break;

	// save cursor pos
	case 's':
		savedCursorPos = cursorPos;
		state = State::idle;
		break;

	// restore cursor pos
	case 'u':
		cursorPos = savedCursorPos;
		// moveCursor(saved_cursor_x, saved_cursor_y);
		state = State::idle;
		break;

	case 'h':
	case 'l':
		state = State::idle;
		break;

	case 'g':
		state = State::idle;
		break;

	// sets colors. Accepts up to 3 args
	case 'm':
		// [m means reset the colors to default
		if(args.count == 0) {
			frontColor = 0xffff;
			backColor = 0x0000;
		}

		while(args.count) {
			args.count--;
			int n = args[args.count];
			static const uint16_t colors[] = {
				0x0000, // black
				0xf800, // red
				0x0780, // green
				0xfe00, // yellow
				0x001f, // blue
				0xf81f, // magenta
				0x07ff, // cyan
				0xffff  // white
			};
			if(n == 0) { // all attributes off
				frontColor = 0xffff;
				backColor = 0x0000;

				display.setFrontColor(frontColor);
				display.setBackColor(backColor);
			}
			if(n >= 30 && n < 38) { // fg colors
				frontColor = colors[n - 30];
				display.setFrontColor(frontColor);
			} else if(n >= 40 && n < 48) {
				backColor = colors[n - 40];
				display.setBackColor(backColor);
			}
		}
		state = State::idle;
		break;

	// Insert Characters
	case '@':
		state = State::idle;
		break;

	// Set scroll region (top and bottom margins) e.g. [1;40r
	case 'r':
		// the top value is first row of scroll region
		// the bottom value is the first row of static region after scroll
		if(args.count == 2 && args[0] < args[1]) {
			scrollStartRow = args[0] - 1;
			scrollEndRow = args[1] - 1;
		} else {
			resetScroll();
		}
		state = State::idle;
		break;

	// Printing
	case 'i':
	// self test modes..
	case 'y':

	// argument follows...
	case '=': {
		//state = State::screen_mode;
		state = State::idle;
		break;
	}

	// '[?' escape mode
	case '?':
		state = State::esc_question;
		break;

		// unknown sequence
	default:
		state = State::idle;
	}
}

void Terminal::state_esc_question(uint8_t ev, uint16_t arg)
{
	// DEC mode commands
	if(ev != EV_CHAR) {
		return;
	}

	if(isdigit(arg)) { // start of an argument
		ret_state = State::esc_question;
		state_command_arg(ev, arg);
		state = State::command_arg;
		return;
	}

	// arg separator
	if(arg == ';') {
		// skip. And also stay in the command state
		return;
	}

	switch(arg) {
	// dec mode: OFF (arg[0] = function)
	case 'l':
	// dec mode: ON (arg[0] = function)
	case 'h': {
		switch(args[0]) {
		// cursor keys mode
		case 1:
			// h = esc 0 A for cursor up
			// l = cursor keys send ansi commands
			break;

		// ansi / vt52
		case 2:
			// h = ansi mode
			// l = vt52 mode
			break;

		case 3:
			// h = 132 chars per line
			// l = 80 chars per line
			break;

		case 4:
			// h = smooth scroll
			// l = jump scroll
			break;

		case 5:
			// h = black on white bg
			// l = white on black bg
			break;

		case 6:
			// h = cursor relative to scroll region
			// l = cursor independent of scroll region
			flags.origin_mode = (arg == 'h') ? 1 : 0;
			break;

		case 7:
			// h = new line after last column
			// l = cursor stays at the end of line
			flags.cursor_wrap = (arg == 'h') ? 1 : 0;
			break;

		case 8:
			// h = keys will auto repeat
			// l = keys do not auto repeat when held down
			break;

		case 9:
			// h = display interlaced
			// l = display not interlaced
			break;

			// 10-38 - all quite DEC-specific so omitted here
		}
		state = State::idle;
		break;
	}

	// Printing
	case 'i':
	// Request printer status
	case 'n':
	default:
		break;
	}

	state = State::idle;
}

void Terminal::state_esc_left_br(uint8_t ev, uint16_t arg)
{
	if(ev != EV_CHAR) {
		return;
	}

	switch(arg) {
	case 'A':
	case 'B':
		// translation map command?
	case '0':
	case 'O':
		// another translation map command?
		state = State::idle;
		break;
	default:
		state = State::idle;
	}
	//state = State::idle;
}

void Terminal::state_esc_right_br(uint8_t ev, uint16_t arg)
{
	if(ev != EV_CHAR) {
		return;
	}

	switch(arg) {
	case 'A':
	case 'B':
		// translation map command?
	case '0':
	case 'O':
		// another translation map command?
		state = State::idle;
		break;
	default:
		state = State::idle;
	}
	//state = State::idle;
}

void Terminal::state_esc_hash(uint8_t ev, uint16_t arg)
{
	if(ev != EV_CHAR) {
		return;
	}

	switch(arg) {
	case '8': {
		// self test: fill the screen with 'E'

		state = State::idle;
		break;
	}
	default:
		state = State::idle;
	}
}

void Terminal::state_escape(uint8_t ev, uint16_t arg)
{
	if(ev != EV_CHAR) {
		// for all other events restore normal mode
		state = State::idle;
		return;
	}

	switch(arg) {
	// command
	case '[': {
		// prepare command state and switch to it
		args = {};
		state = State::esc_sq_bracket;
		break;
	}

	/* ESC ( */
	case '(':
		args = {};
		state = State::esc_left_br;
		break;

	/* ESC ) */
	case ')':
		args = {};
		state = State::esc_right_br;
		break;

	// ESC #
	case '#':
		args = {};
		state = State::esc_hash;
		break;

	// ESC P (DCS, Device Control String)
	case 'P':
		state = State::idle;
		break;

	// move cursor down one line and scroll window if at bottom line
	case 'D':
		move(0, 1);
		state = State::idle;
		break;

	// move cursor up one line and scroll window if at top line
	case 'M':
		move(0, -1);
		state = State::idle;
		break;

	// next line (same as '\r\n')
	case 'E':
		move(0, 1);
		cursorPos.col = 0;
		state = State::idle;
		break;

	// Save attributes and cursor position
	case '7':
	case 's':
		savedCursorPos = cursorPos;
		state = State::idle;
		break;

	// Restore attributes and cursor position
	case '8':
	case 'u':
		cursorPos = savedCursorPos;
		state = State::idle;
		break;

	// Keypad into applications mode
	case '=':
		state = State::idle;
		break;

	// Keypad into numeric mode
	case '>':
		state = State::idle;
		break;

	// Report terminal type
	case 'Z':
		// vt 100 response
		callbacks.sendResponse("\033[?1;0c");
		// unknown terminal
		//out("\033[?c");
		state = State::idle;
		break;

	// Reset terminal to initial state
	case 'c':
		reset();
		state = State::idle;
		break;

	// Set tab in current position
	case 'H':
	// G2 character set for next character only
	case 'N':
	// G3 "               "
	case 'O':
	// Exit vt52 mode
	case '<':
		// ignore
		state = State::idle;
		break;

	// marks start of next escape sequence
	case KEY_ESC:
		// stay in escape state
		break;

	// unknown sequence - return to normal mode
	default:
		state = State::idle;
	}
}

void Terminal::state_idle(uint8_t ev, uint16_t arg)
{
	if(ev != EV_CHAR) {
		return;
	}

	switch(arg) {
	// AnswerBack for vt100's
	case 5:
		// should send SCCS_ID?
		callbacks.sendResponse("X");
		break;

	// new line
	case '\n':
		move(0, 1);
		cursorPos.col = 0;
		// moveCursor(0, cursorPos.row + 1);
		// do scrolling here!
		break;

	// carrage return (0x0d)
	case '\r':
		cursorPos.col = 0;
		// move(0, 1);
		// moveCursor(0, cursorPos.row);
		break;

	// backspace 0x08
	case '\b':
		move(-1, 0);
		// backspace does not delete the character! Only moves cursor!
		//display.drawChar(cursorPos.col * char_width,
		//	cursorPos.row * char_height, ' ');
		break;

	// del - delete character under cursor
	case KEY_DEL:
		// Problem: with current implementation, we can't move the rest of line
		// to the left as is the proper behavior of the delete character
		// fill the current position with background color
		putcInternal(' ');
		move(-1, 0);
		// clearChar(cursorPos.col, cursorPos.row);
		break;

	// tab
	case '\t': {
		// tab fills characters on the line until we reach a multiple of tab_stop
		int tab_stop = 4;
		int to_put = tab_stop - (cursorPos.col % tab_stop);
		while(to_put--) {
			putcInternal(' ');
		}
		break;
	}

	// bell is sent by bash for ex. when doing tab completion
	case KEY_BELL:
		// sound the speaker bell?
		// skip
		break;

	// escape
	case KEY_ESC:
		state = State::escape;
		break;

	default:
		putcInternal(arg);
	}
}

void Terminal::putc(uint8_t c, unsigned count)
{
	while(count--) {
		callState(EV_CHAR, 0x0000 | c);
	}
}

void Terminal::puts(const char* str)
{
	while(*str) {
		putc(*str++);
	}
}

size_t Terminal::nputs(const char* str, size_t length)
{
	unsigned n = length;
	while(n--) {
		putc(*str++);
	}
	return length;
}

size_t Terminal::printf(const char* fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	char buf[256];
	size_t n = m_vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	nputs(buf, n);

	return n;
}

} // namespace VT100
