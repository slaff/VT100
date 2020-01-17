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
#include <debug_progmem.h>

#include "vt100.h"

#define KEY_ESC 0x1b
#define KEY_DEL 0x7f
#define KEY_BELL 0x07

const Vt100::StateMethod Vt100::stateTable[] = {
#define XX(s) &Vt100::_st_##s,
	VT100_STATE_MAP(XX)
#undef XX
};

void Vt100::reset()
{
	char_height = display.getCharHeight();
	char_width = display.getCharWidth();
	screen_width = display.getWidth();
	screen_height = display.getHeight();
	row_count = screen_height / char_height;
	col_count = screen_width / char_width;
	back_color = 0x0000;
	front_color = 0xffff;
	cursor_x = cursor_y = saved_cursor_x = saved_cursor_y = 0;
	narg = 0;
	state = State::idle;
	ret_state = State::idle;
	resetScroll();
	flags.cursor_wrap = 0;
	flags.origin_mode = 0;
	display.setFrontColor(front_color);
	display.setBackColor(back_color);
}

void Vt100::resetScroll()
{
	scroll_start_row = 0;
	scroll_end_row = row_count - 1;
}

void Vt100::clearLines(uint16_t start_line, uint16_t end_line)
{
	for(int c = start_line; c <= end_line; c++) {
		uint16_t cy = cursor_y;
		cursor_y = c;
		display.fillRect(0, VT100_CURSOR_Y(), screen_width, char_height, 0x0000);
		cursor_y = cy;
	}
}

// moves the cursor relative to current cursor position and scrolls the screen
void Vt100::move(int16_t right_left, int16_t bottom_top)
{
	// calculate how many lines we need to move down or up if x movement goes outside screen
	int16_t new_x = right_left + cursor_x;
	if(new_x >= col_count) {
		if(flags.cursor_wrap) {
			bottom_top += new_x / col_count;
			cursor_x = new_x % col_count;
		} else {
			cursor_x = col_count;
		}
	} else if(new_x < 0) {
		bottom_top += new_x / col_count;
		cursor_x = col_count - (abs(new_x) % col_count);
	} else {
		cursor_x = new_x;
	}

	if(bottom_top != 0) {
		int16_t new_y = cursor_y + bottom_top;
		// bottom margin 39 marks last line as static on 40 line display
		// therefore, we would scroll when new cursor has moved to line 39
		if(new_y > scroll_end_row) {
			cursor_y = scroll_end_row;
		} else if(new_y < scroll_start_row) {
			cursor_y = scroll_start_row;
		} else {
			cursor_y = new_y;
			return;
		}

		// scrolls the scroll region up (lines > 0) or down (lines < 0)
		auto lines = new_y - cursor_y;
		auto top = VT100_Y(scroll_start_row);
		auto bottom = VT100_Y(1 + scroll_end_row);
		display.scroll(top, bottom, VT100_Y(lines));

		// clearing of lines that we have scrolled up or down
		if(lines > 0) {
			clearLines(1 + scroll_end_row - lines, scroll_end_row);
		} else {
			clearLines(scroll_start_row, scroll_start_row - lines - 1);
		}
	}
}

void Vt100::drawCursor()
{
	//uint16_t x = t->cursor_x * t->char_width;
	//uint16_t y = t->cursor_y * t->char_height;

	//display.fillRect(x, y, t->char_width, t->char_height, t->front_color);
}

// sends the character to the display and updates cursor position
void Vt100::_putc(uint8_t ch)
{
	if(ch < 0x20 || ch > 0x7e) {
		_putc('0');
		_putc('x');
		_putc(hexchar((ch & 0xf0) >> 4));
		_putc(hexchar(ch & 0x0f));
		return;
	}

	display.setFrontColor(front_color);
	display.setBackColor(back_color);
	display.drawChar(VT100_CURSOR_X(), VT100_CURSOR_Y(), ch);

	// move cursor right
	move(1, 0);
	drawCursor();
}

void Vt100::_st_command_arg(uint8_t ev, uint16_t arg)
{
	if(ev != EV_CHAR) {
		return;
	}

	if(isdigit(arg)) { // a digit argument
		args[narg] = args[narg] * 10 + (arg - '0');
		return;
	}

	if(arg == ';') { // separator
		++narg;
		return;
	}

	// no more arguments, go back to command state
	++narg;
	state = ret_state;
	ret_state = State::idle;

	// execute next state as well because we have already consumed a char!
	callState(ev, arg);
}

void Vt100::_st_esc_sq_bracket(uint8_t ev, uint16_t arg)
{
	if(ev != EV_CHAR) {
		// for all other events restore normal mode
		state = State::idle;
		return;
	}

	if(isdigit(arg)) {
		// start of an argument
		ret_state = State::esc_sq_bracket;
		_st_command_arg(ev, arg);
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
		int n = (narg > 0) ? args[0] : 1;
		cursor_y -= n;
		if(cursor_y < scroll_start_row) {
			cursor_y = scroll_start_row;
		}
		state = State::idle;
		break;
	}

	// cursor down (cursor stops at bottom margin)
	case 'B': {
		int n = (narg > 0) ? args[0] : 1;
		cursor_y += n;
		if(cursor_y > scroll_end_row) {
			cursor_y = scroll_end_row;
		}
		state = State::idle;
		break;
	}

	// cursor right (cursor stops at right margin)
	case 'C': {
		int n = (narg > 0) ? args[0] : 1;
		cursor_x += n;
		if(cursor_x > col_count)
			cursor_x = col_count;
		state = State::idle;
		break;
	}

	// cursor left
	case 'D': {
		int n = (narg > 0) ? args[0] : 1;
		cursor_x -= n;
		if(cursor_x < 0) {
			cursor_x = 0;
		}
		state = State::idle;
		break;
	}

	// move cursor to position (default 0;0)
	case 'f':
	case 'H':
		// cursor stops at respective margins
		cursor_x = (narg >= 1) ? (args[1] - 1) : 0;
		cursor_y = (narg == 2) ? (args[0] - 1) : 0;

		if(flags.origin_mode) {
			cursor_y += scroll_start_row;
			if(cursor_y > scroll_end_row) {
				cursor_y = scroll_end_row;
			}
		}

		if(cursor_x >= col_count) {
			cursor_x = col_count - 1;
		}
		if(cursor_y >= row_count) {
			cursor_y = row_count - 1;
		}

		state = State::idle;

		debug_i("CURSOR = %u, %u", cursor_x, cursor_y);
		break;

	// clear screen from cursor up or down
	case 'J':
		if(narg == 0 || (narg == 1 && args[0] == 0)) {
			// clear down to the bottom of screen (including cursor)
			clearLines(cursor_y, row_count);
		} else if(narg == 1 && args[0] == 1) {
			// clear top of screen to current line (including cursor)
			clearLines(0, cursor_y);
		} else if(narg == 1 && args[0] == 2) {
			// clear whole screen
			clearLines(0, row_count);
			// reset scroll value
			resetScroll();
		}
		state = State::idle;
		break;

	// clear line from cursor right/left
	case 'K': {
		uint16_t x = VT100_CURSOR_X();
		uint16_t y = VT100_CURSOR_Y();

		if(narg == 0 || (narg == 1 && args[0] == 0)) {
			// clear to end of line (to \n or to edge?), including cursor
			display.fillRect(x, y, screen_width - x, char_height, back_color);
		} else if(narg == 1 && args[0] == 1) {
			// clear from left to current cursor position
			display.fillRect(0, y, x + char_width, char_height, back_color);
		} else if(narg == 1 && args[0] == 2) {
			// clear whole current line
			display.fillRect(0, y, screen_width, char_height, back_color);
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
		int n = ((narg > 0) ? args[0] : 1);
		move(-n, 0);
		for(int c = 0; c < n; c++) {
			_putc(' ');
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
		saved_cursor_x = cursor_x;
		saved_cursor_y = cursor_y;
		state = State::idle;
		break;

	// restore cursor pos
	case 'u':
		cursor_x = saved_cursor_x;
		cursor_y = saved_cursor_y;
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
		if(narg == 0) {
			front_color = 0xffff;
			back_color = 0x0000;
		}

		while(narg) {
			narg--;
			int n = args[narg];
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
				front_color = 0xffff;
				back_color = 0x0000;

				display.setFrontColor(front_color);
				display.setBackColor(back_color);
			}
			if(n >= 30 && n < 38) { // fg colors
				front_color = colors[n - 30];
				display.setFrontColor(front_color);
			} else if(n >= 40 && n < 48) {
				back_color = colors[n - 40];
				display.setBackColor(back_color);
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
		if(narg == 2 && args[0] < args[1]) {
			scroll_start_row = args[0] - 1;
			scroll_end_row = args[1] - 1;
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

void Vt100::_st_esc_question(uint8_t ev, uint16_t arg)
{
	// DEC mode commands
	if(ev != EV_CHAR) {
		return;
	}

	if(isdigit(arg)) { // start of an argument
		ret_state = State::esc_question;
		_st_command_arg(ev, arg);
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
			debug_i("ORIGIN MODE: %u", flags.origin_mode);
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

void Vt100::_st_esc_left_br(uint8_t ev, uint16_t arg)
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

void Vt100::_st_esc_right_br(uint8_t ev, uint16_t arg)
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

void Vt100::_st_esc_hash(uint8_t ev, uint16_t arg)
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

void Vt100::_st_escape(uint8_t ev, uint16_t arg)
{
	if(ev != EV_CHAR) {
		// for all other events restore normal mode
		state = State::idle;
		return;
	}

	auto clearArgs = [this]() {
		narg = 0;
		memset(args, 0, sizeof(args));
	};

	switch(arg) {
	// command
	case '[': {
		// prepare command state and switch to it
		clearArgs();
		state = State::esc_sq_bracket;
		break;
	}

	/* ESC ( */
	case '(':
		clearArgs();
		state = State::esc_left_br;
		break;

	/* ESC ) */
	case ')':
		clearArgs();
		state = State::esc_right_br;
		break;

	// ESC #
	case '#':
		clearArgs();
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
		cursor_x = 0;
		state = State::idle;
		break;

	// Save attributes and cursor position
	case '7':
	case 's':
		saved_cursor_x = cursor_x;
		saved_cursor_y = cursor_y;
		state = State::idle;
		break;

	// Restore attributes and cursor position
	case '8':
	case 'u':
		cursor_x = saved_cursor_x;
		cursor_y = saved_cursor_y;
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

void Vt100::_st_idle(uint8_t ev, uint16_t arg)
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
		cursor_x = 0;
		// moveCursor(0, cursor_y + 1);
		// do scrolling here!
		break;

	// carrage return (0x0d)
	case '\r':
		cursor_x = 0;
		// move(0, 1);
		// moveCursor(0, cursor_y);
		break;

	// backspace 0x08
	case '\b':
		move(-1, 0);
		// backspace does not delete the character! Only moves cursor!
		//display.drawChar(cursor_x * char_width,
		//	cursor_y * char_height, ' ');
		break;

	// del - delete character under cursor
	case KEY_DEL:
		// Problem: with current implementation, we can't move the rest of line
		// to the left as is the proper behavior of the delete character
		// fill the current position with background color
		_putc(' ');
		move(-1, 0);
		// clearChar(cursor_x, cursor_y);
		break;

	// tab
	case '\t': {
		// tab fills characters on the line until we reach a multiple of tab_stop
		int tab_stop = 4;
		int to_put = tab_stop - (cursor_x % tab_stop);
		while(to_put--) {
			_putc(' ');
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
		_putc(arg);
	}
}

void Vt100::putc(uint8_t c, unsigned count)
{
	/*char *buffer = 0;
	switch(c){
		case KEY_UP:         buffer="\e[A";    break;
		case KEY_DOWN:       buffer="\e[B";    break;
		case KEY_RIGHT:      buffer="\e[C";    break;
		case KEY_LEFT:       buffer="\e[D";    break;
		case KEY_BACKSPACE:  buffer="\b";      break;
		case KEY_IC:         buffer="\e[2~";   break;
		case KEY_DC:         buffer="\e[3~";   break;
		case KEY_HOME:       buffer="\e[7~";   break;
		case KEY_END:        buffer="\e[8~";   break;
		case KEY_PPAGE:      buffer="\e[5~";   break;
		case KEY_NPAGE:      buffer="\e[6~";   break;
		case KEY_SUSPEND:    buffer="\x1A";    break;      // ctrl-z
		case KEY_F(1):       buffer="\e[[A";   break;
		case KEY_F(2):       buffer="\e[[B";   break;
		case KEY_F(3):       buffer="\e[[C";   break;
		case KEY_F(4):       buffer="\e[[D";   break;
		case KEY_F(5):       buffer="\e[[E";   break;
		case KEY_F(6):       buffer="\e[17~";  break;
		case KEY_F(7):       buffer="\e[18~";  break;
		case KEY_F(8):       buffer="\e[19~";  break;
		case KEY_F(9):       buffer="\e[20~";  break;
		case KEY_F(10):      buffer="\e[21~";  break;
	}
	if(buffer){
		while(*buffer){
			state(EV_CHAR, *buffer++);
		}
	} else {
		state(EV_CHAR, 0x0000 | c);
	}*/
	while(count--) {
		callState(EV_CHAR, 0x0000 | c);
	}
}

void Vt100::puts(const char* str)
{
	while(*str) {
		putc(*str++);
	}
}

size_t Vt100::nputs(const char* str, size_t length)
{
	unsigned n = length;
	while(n--) {
		putc(*str++);
	}
	return length;
}

size_t Vt100::printf(const char* fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	char buf[256];
	size_t n = m_vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	nputs(buf, n);

	return n;
}
