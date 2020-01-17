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

#define STATE(NAME, TERM, EV, ARG) void NAME(struct vt100* TERM, uint8_t EV, uint16_t ARG)

// states
enum { STATE_IDLE, STATE_ESCAPE, STATE_COMMAND };

// events that are passed into states
enum {
	EV_CHAR = 1,
};

#define MAX_COMMAND_ARGS 4
static struct vt100 {
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

	void (*state)(struct vt100* term, uint8_t ev, uint16_t arg);
	void (*ret_state)(struct vt100* term, uint8_t ev, uint16_t arg);

	DisplayDevice* display;
	VT100Callbacks* callbacks;
} term;

STATE(_st_idle, term, ev, arg);
STATE(_st_esc_sq_bracket, term, ev, arg);
STATE(_st_esc_question, term, ev, arg);
STATE(_st_esc_hash, term, ev, arg);

void _vt100_resetScroll();

void _vt100_reset()
{
	term.char_height = term.display->getCharHeight();
	term.char_width = term.display->getCharWidth();
	term.screen_width = term.display->getWidth();
	term.screen_height = term.display->getHeight();
	term.row_count = term.screen_height / term.char_height;
	term.col_count = term.screen_width / term.char_width;
	term.back_color = 0x0000;
	term.front_color = 0xffff;
	term.cursor_x = term.cursor_y = term.saved_cursor_x = term.saved_cursor_y = 0;
	term.narg = 0;
	term.state = _st_idle;
	term.ret_state = 0;
	_vt100_resetScroll();
	term.flags.cursor_wrap = 0;
	term.flags.origin_mode = 0;
	term.display->setFrontColor(term.front_color);
	term.display->setBackColor(term.back_color);
}

void _vt100_resetScroll()
{
	term.scroll_start_row = 0;
	term.scroll_end_row = term.row_count - 1;
}

#define VT100_X(term, x) ((x)*term->char_width)
#define VT100_Y(term, y) ((y)*term->char_height)

#define VT100_CURSOR_X(term) VT100_X(term, term->cursor_x)
#define VT100_CURSOR_Y(term) VT100_Y(term, term->cursor_y)

void _vt100_clearLines(struct vt100* t, uint16_t start_line, uint16_t end_line)
{
	for(int c = start_line; c <= end_line; c++) {
		uint16_t cy = t->cursor_y;
		t->cursor_y = c;
		t->display->fillRect(0, VT100_CURSOR_Y(t), t->screen_width, t->char_height, 0x0000);
		t->cursor_y = cy;
	}
}

// moves the cursor relative to current cursor position and scrolls the screen
void _vt100_move(struct vt100* term, int16_t right_left, int16_t bottom_top)
{
	// calculate how many lines we need to move down or up if x movement goes outside screen
	int16_t new_x = right_left + term->cursor_x;
	if(new_x >= term->col_count) {
		if(term->flags.cursor_wrap) {
			bottom_top += new_x / term->col_count;
			term->cursor_x = new_x % term->col_count;
		} else {
			term->cursor_x = term->col_count;
		}
	} else if(new_x < 0) {
		bottom_top += new_x / term->col_count;
		term->cursor_x = term->col_count - (abs(new_x) % term->col_count);
	} else {
		term->cursor_x = new_x;
	}

	if(bottom_top != 0) {
		int16_t new_y = term->cursor_y + bottom_top;
		// bottom margin 39 marks last line as static on 40 line display
		// therefore, we would scroll when new cursor has moved to line 39
		if(new_y > term->scroll_end_row) {
			term->cursor_y = term->scroll_end_row;
		} else if(new_y < term->scroll_start_row) {
			term->cursor_y = term->scroll_start_row;
		} else {
			term->cursor_y = new_y;
			return;
		}

		// scrolls the scroll region up (lines > 0) or down (lines < 0)
		auto lines = new_y - term->cursor_y;
		auto top = VT100_Y(term, term->scroll_start_row);
		auto bottom = VT100_Y(term, 1 + term->scroll_end_row);
		term->display->scroll(top, bottom, VT100_Y(term, lines));

		// clearing of lines that we have scrolled up or down
		if(lines > 0) {
			_vt100_clearLines(term, 1 + term->scroll_end_row - lines, term->scroll_end_row);
		} else {
			_vt100_clearLines(term, term->scroll_start_row, term->scroll_start_row - lines - 1);
		}
	}
}

void _vt100_drawCursor(struct vt100* t)
{
	//uint16_t x = t->cursor_x * t->char_width;
	//uint16_t y = t->cursor_y * t->char_height;

	//term.display->fillRect(x, y, t->char_width, t->char_height, t->front_color);
}

// sends the character to the display and updates cursor position
void _vt100_putc(struct vt100* t, uint8_t ch)
{
	if(ch < 0x20 || ch > 0x7e) {
		_vt100_putc(t, '0');
		_vt100_putc(t, 'x');
		_vt100_putc(t, hexchar((ch & 0xf0) >> 4));
		_vt100_putc(t, hexchar(ch & 0x0f));
		return;
	}

	// calculate current cursor position in the display ram
	uint16_t x = VT100_CURSOR_X(t);
	uint16_t y = VT100_CURSOR_Y(t);

	term.display->setFrontColor(t->front_color);
	term.display->setBackColor(t->back_color);
	term.display->drawChar(x, y, ch);

	// move cursor right
	_vt100_move(t, 1, 0);
	_vt100_drawCursor(t);
}

STATE(_st_command_arg, term, ev, arg)
{
	if(ev != EV_CHAR) {
		return;
	}

	if(isdigit(arg)) { // a digit argument
		term->args[term->narg] = term->args[term->narg] * 10 + (arg - '0');
		return;
	}

	if(arg == ';') { // separator
		++term->narg;
		return;
	}

	// no more arguments, go back to command state
	++term->narg;
	if(term->ret_state) {
		term->state = term->ret_state;
	} else {
		term->state = _st_idle;
	}

	// execute next state as well because we have already consumed a char!
	term->state(term, ev, arg);
}

STATE(_st_esc_sq_bracket, term, ev, arg)
{
	if(ev != EV_CHAR) {
		// for all other events restore normal mode
		term->state = _st_idle;
		return;
	}

	if(isdigit(arg)) {
		// start of an argument
		term->ret_state = _st_esc_sq_bracket;
		_st_command_arg(term, ev, arg);
		term->state = _st_command_arg;
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
		int n = (term->narg > 0) ? term->args[0] : 1;
		term->cursor_y -= n;
		if(term->cursor_y < term->scroll_start_row) {
			term->cursor_y = term->scroll_start_row;
		}
		term->state = _st_idle;
		break;
	}

	// cursor down (cursor stops at bottom margin)
	case 'B': {
		int n = (term->narg > 0) ? term->args[0] : 1;
		term->cursor_y += n;
		if(term->cursor_y > term->scroll_end_row) {
			term->cursor_y = term->scroll_end_row;
		}
		term->state = _st_idle;
		break;
	}

	// cursor right (cursor stops at right margin)
	case 'C': {
		int n = (term->narg > 0) ? term->args[0] : 1;
		term->cursor_x += n;
		if(term->cursor_x > term->col_count)
			term->cursor_x = term->col_count;
		term->state = _st_idle;
		break;
	}

	// cursor left
	case 'D': {
		int n = (term->narg > 0) ? term->args[0] : 1;
		term->cursor_x -= n;
		if(term->cursor_x < 0) {
			term->cursor_x = 0;
		}
		term->state = _st_idle;
		break;
	}

	// move cursor to position (default 0;0)
	case 'f':
	case 'H':
		// cursor stops at respective margins
		term->cursor_x = (term->narg >= 1) ? (term->args[1] - 1) : 0;
		term->cursor_y = (term->narg == 2) ? (term->args[0] - 1) : 0;

		if(term->flags.origin_mode) {
			term->cursor_y += term->scroll_start_row;
			if(term->cursor_y > term->scroll_end_row) {
				term->cursor_y = term->scroll_end_row;
			}
		}

		if(term->cursor_x >= term->col_count) {
			term->cursor_x = term->col_count - 1;
		}
		if(term->cursor_y >= term->row_count) {
			term->cursor_y = term->row_count - 1;
		}

		term->state = _st_idle;

		debug_i("CURSOR = %u, %u", term->cursor_x, term->cursor_y);
		break;

	// clear screen from cursor up or down
	case 'J':
		if(term->narg == 0 || (term->narg == 1 && term->args[0] == 0)) {
			// clear down to the bottom of screen (including cursor)
			_vt100_clearLines(term, term->cursor_y, term->row_count);
		} else if(term->narg == 1 && term->args[0] == 1) {
			// clear top of screen to current line (including cursor)
			_vt100_clearLines(term, 0, term->cursor_y);
		} else if(term->narg == 1 && term->args[0] == 2) {
			// clear whole screen
			_vt100_clearLines(term, 0, term->row_count);
			// reset scroll value
			_vt100_resetScroll();
		}
		term->state = _st_idle;
		break;

	// clear line from cursor right/left
	case 'K': {
		uint16_t x = VT100_CURSOR_X(term);
		uint16_t y = VT100_CURSOR_Y(term);

		if(term->narg == 0 || (term->narg == 1 && term->args[0] == 0)) {
			// clear to end of line (to \n or to edge?), including cursor
			term->display->fillRect(x, y, term->screen_width - x, term->char_height, term->back_color);
		} else if(term->narg == 1 && term->args[0] == 1) {
			// clear from left to current cursor position
			term->display->fillRect(0, y, x + term->char_width, term->char_height, term->back_color);
		} else if(term->narg == 1 && term->args[0] == 2) {
			// clear whole current line
			term->display->fillRect(0, y, term->screen_width, term->char_height, term->back_color);
		}
		term->state = _st_idle;
		break;
	}

	// insert lines (args[0] = number of lines)
	case 'L':
	// delete lines (args[0] = number of lines)
	case 'M':
		term->state = _st_idle;
		break;

	// delete characters args[0] or 1 in front of cursor
	case 'P': {
		// TODO: this needs to correctly delete n chars
		int n = ((term->narg > 0) ? term->args[0] : 1);
		_vt100_move(term, -n, 0);
		for(int c = 0; c < n; c++) {
			_vt100_putc(term, ' ');
		}
		term->state = _st_idle;
		break;
	}

	// query device code
	case 'c':
		term->callbacks->sendResponse("\e[?1;0c");
		term->state = _st_idle;
		break;

	case 'x':
		term->state = _st_idle;
		break;

	// save cursor pos
	case 's':
		term->saved_cursor_x = term->cursor_x;
		term->saved_cursor_y = term->cursor_y;
		term->state = _st_idle;
		break;

	// restore cursor pos
	case 'u':
		term->cursor_x = term->saved_cursor_x;
		term->cursor_y = term->saved_cursor_y;
		//_vt100_moveCursor(term, term->saved_cursor_x, term->saved_cursor_y);
		term->state = _st_idle;
		break;

	case 'h':
	case 'l':
		term->state = _st_idle;
		break;

	case 'g':
		term->state = _st_idle;
		break;

	// sets colors. Accepts up to 3 args
	case 'm':
		// [m means reset the colors to default
		if(term->narg == 0) {
			term->front_color = 0xffff;
			term->back_color = 0x0000;
		}

		while(term->narg) {
			term->narg--;
			int n = term->args[term->narg];
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
				term->front_color = 0xffff;
				term->back_color = 0x0000;

				term->display->setFrontColor(term->front_color);
				term->display->setBackColor(term->back_color);
			}
			if(n >= 30 && n < 38) { // fg colors
				term->front_color = colors[n - 30];
				term->display->setFrontColor(term->front_color);
			} else if(n >= 40 && n < 48) {
				term->back_color = colors[n - 40];
				term->display->setBackColor(term->back_color);
			}
		}
		term->state = _st_idle;
		break;

	// Insert Characters
	case '@':
		term->state = _st_idle;
		break;

	// Set scroll region (top and bottom margins) e.g. [1;40r
	case 'r':
		// the top value is first row of scroll region
		// the bottom value is the first row of static region after scroll
		if(term->narg == 2 && term->args[0] < term->args[1]) {
			term->scroll_start_row = term->args[0] - 1;
			term->scroll_end_row = term->args[1] - 1;
		} else {
			_vt100_resetScroll();
		}
		term->state = _st_idle;
		break;

	// Printing
	case 'i':
	// self test modes..
	case 'y':

	// argument follows...
	case '=': {
		//term->state = _st_screen_mode;
		term->state = _st_idle;
		break;
	}

	// '[?' escape mode
	case '?':
		term->state = _st_esc_question;
		break;

		// unknown sequence
	default:
		term->state = _st_idle;
	}
}

STATE(_st_esc_question, term, ev, arg)
{
	// DEC mode commands
	if(ev != EV_CHAR) {
		return;
	}

	if(isdigit(arg)) { // start of an argument
		term->ret_state = _st_esc_question;
		_st_command_arg(term, ev, arg);
		term->state = _st_command_arg;
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
		switch(term->args[0]) {
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
			term->flags.origin_mode = (arg == 'h') ? 1 : 0;
			debug_i("ORIGIN MODE: %u", term->flags.origin_mode);
			break;

		case 7:
			// h = new line after last column
			// l = cursor stays at the end of line
			term->flags.cursor_wrap = (arg == 'h') ? 1 : 0;
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
		term->state = _st_idle;
		break;
	}

	// Printing
	case 'i':
	// Request printer status
	case 'n':
	default:
		break;
	}

	term->state = _st_idle;
}

STATE(_st_esc_left_br, term, ev, arg)
{
	switch(ev) {
	case EV_CHAR: {
		switch(arg) {
		case 'A':
		case 'B':
			// translation map command?
		case '0':
		case 'O':
			// another translation map command?
			term->state = _st_idle;
			break;
		default:
			term->state = _st_idle;
		}
		//term->state = _st_idle;
	}
	}
}

STATE(_st_esc_right_br, term, ev, arg)
{
	switch(ev) {
	case EV_CHAR: {
		switch(arg) {
		case 'A':
		case 'B':
			// translation map command?
		case '0':
		case 'O':
			// another translation map command?
			term->state = _st_idle;
			break;
		default:
			term->state = _st_idle;
		}
		//term->state = _st_idle;
	}
	}
}

STATE(_st_esc_hash, term, ev, arg)
{
	switch(ev) {
	case EV_CHAR: {
		switch(arg) {
		case '8': {
			// self test: fill the screen with 'E'

			term->state = _st_idle;
			break;
		}
		default:
			term->state = _st_idle;
		}
	}
	}
}

STATE(_st_escape, term, ev, arg)
{
	if(ev != EV_CHAR) {
		// for all other events restore normal mode
		term->state = _st_idle;
		return;
	}

	auto clearArgs = [term]() {
		term->narg = 0;
		memset(term->args, 0, sizeof(term->args));
	};

	switch(arg) {
	// command
	case '[': {
		// prepare command state and switch to it
		clearArgs();
		term->state = _st_esc_sq_bracket;
		break;
	}

	/* ESC ( */
	case '(':
		clearArgs();
		term->state = _st_esc_left_br;
		break;

	/* ESC ) */
	case ')':
		clearArgs();
		term->state = _st_esc_right_br;
		break;

	// ESC #
	case '#':
		clearArgs();
		term->state = _st_esc_hash;
		break;

	// ESC P (DCS, Device Control String)
	case 'P':
		term->state = _st_idle;
		break;

	// move cursor down one line and scroll window if at bottom line
	case 'D':
		_vt100_move(term, 0, 1);
		term->state = _st_idle;
		break;

	// move cursor up one line and scroll window if at top line
	case 'M':
		_vt100_move(term, 0, -1);
		term->state = _st_idle;
		break;

	// next line (same as '\r\n')
	case 'E':
		_vt100_move(term, 0, 1);
		term->cursor_x = 0;
		term->state = _st_idle;
		break;

	// Save attributes and cursor position
	case '7':
	case 's':
		term->saved_cursor_x = term->cursor_x;
		term->saved_cursor_y = term->cursor_y;
		term->state = _st_idle;
		break;

	// Restore attributes and cursor position
	case '8':
	case 'u':
		term->cursor_x = term->saved_cursor_x;
		term->cursor_y = term->saved_cursor_y;
		term->state = _st_idle;
		break;

	// Keypad into applications mode
	case '=':
		term->state = _st_idle;
		break;

	// Keypad into numeric mode
	case '>':
		term->state = _st_idle;
		break;

	// Report terminal type
	case 'Z':
		// vt 100 response
		term->callbacks->sendResponse("\033[?1;0c");
		// unknown terminal
		//out("\033[?c");
		term->state = _st_idle;
		break;

	// Reset terminal to initial state
	case 'c':
		_vt100_reset();
		term->state = _st_idle;
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
		term->state = _st_idle;
		break;

	// marks start of next escape sequence
	case KEY_ESC:
		// stay in escape state
		break;

	// unknown sequence - return to normal mode
	default:
		term->state = _st_idle;
	}
}

STATE(_st_idle, term, ev, arg)
{
	if(ev != EV_CHAR) {
		return;
	}

	switch(arg) {
	// AnswerBack for vt100's
	case 5:
		// should send SCCS_ID?
		term->callbacks->sendResponse("X");
		break;

	// new line
	case '\n':
		_vt100_move(term, 0, 1);
		term->cursor_x = 0;
		//_vt100_moveCursor(term, 0, term->cursor_y + 1);
		// do scrolling here!
		break;

	// carrage return (0x0d)
	case '\r':
		term->cursor_x = 0;
		//_vt100_move(term, 0, 1);
		//_vt100_moveCursor(term, 0, term->cursor_y);
		break;

	// backspace 0x08
	case '\b':
		_vt100_move(term, -1, 0);
		// backspace does not delete the character! Only moves cursor!
		//term.display->drawChar(term->cursor_x * term->char_width,
		//	term->cursor_y * term->char_height, ' ');
		break;

	// del - delete character under cursor
	case KEY_DEL:
		// Problem: with current implementation, we can't move the rest of line
		// to the left as is the proper behavior of the delete character
		// fill the current position with background color
		_vt100_putc(term, ' ');
		_vt100_move(term, -1, 0);
		//_vt100_clearChar(term, term->cursor_x, term->cursor_y);
		break;

	// tab
	case '\t': {
		// tab fills characters on the line until we reach a multiple of tab_stop
		int tab_stop = 4;
		int to_put = tab_stop - (term->cursor_x % tab_stop);
		while(to_put--) {
			_vt100_putc(term, ' ');
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
		term->state = _st_escape;
		break;

	default:
		_vt100_putc(term, arg);
	}
}

void vt100_init(DisplayDevice* display, VT100Callbacks* callbacks)
{
	term.display = display;
	term.callbacks = callbacks;
	_vt100_reset();
}

void vt100_putc(uint8_t c, unsigned count)
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
			term.state(&term, EV_CHAR, *buffer++);
		}
	} else {
		term.state(&term, EV_CHAR, 0x0000 | c);
	}*/
	while(count--) {
		term.state(&term, EV_CHAR, 0x0000 | c);
	}
}

void vt100_puts(const char* str)
{
	while(*str) {
		vt100_putc(*str++);
	}
}

size_t vt100_nputs(const char* str, size_t length)
{
	unsigned n = length;
	while(n--) {
		vt100_putc(*str++);
	}
	return length;
}

size_t vt100_printf(const char* fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	char buf[256];
	size_t n = m_vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	vt100_nputs(buf, n);

	return n;
}
