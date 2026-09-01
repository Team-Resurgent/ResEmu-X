/*
 * HD44780-compatible character LCD controller
 *
 * See lcd_hd44780.h for the origin of this decode and for where the transport
 * layer sits relative to it.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "hw/xbox/lcd_hd44780.h"
#include "hw/xbox/lcd_hd44780_font.h"

#define HD44780_BLANK 0x20

typedef enum {
    HD44780_DECODED_DATA = 0,
    HD44780_DECODED_CLEAR_DISPLAY,
    HD44780_DECODED_RETURN_HOME,
    HD44780_DECODED_ENTRY_MODE_SET,
    HD44780_DECODED_DISPLAY_ON_OFF,
    HD44780_DECODED_CURSOR_OR_DISPLAY_SHIFT,
    HD44780_DECODED_FUNCTION_SET,
    HD44780_DECODED_SET_CGRAM_ADDR,
    HD44780_DECODED_SET_DDRAM_ADDR,
    HD44780_DECODED_RESERVED,
} HD44780Decoded;

static HD44780State *hd44780_debug_state;

void hd44780_register_debug(HD44780State *s)
{
    hd44780_debug_state = s;
}

const HD44780State *hd44780_get_debug_state(void)
{
    return hd44780_debug_state;
}

void hd44780_reset(HD44780State *s)
{
    memset(s, 0, sizeof(*s));
    memset(s->ddram, HD44780_BLANK, sizeof(s->ddram));
    s->entry_mode = 0x06; /* increment, no shift */
}

/* The hosts assume an HD44780 carrying the A00 character ROM, but the font ROM
 * here is the panel's own, which places some symbols differently. 0xFF is the
 * one that shows in practice: XBMC's progress bar emits it for every filled
 * block, and the solid block lives at 0x1F in this ROM, so untranslated the bar
 * renders with holes. */
static uint8_t hd44780_host_code_to_panel(uint8_t code)
{
    switch (code) {
    case 0xFF:
        return 0x1F; /* solid block */
    default:
        return code;
    }
}

static void hd44780_addr_to_cell(uint8_t addr, uint8_t *col, uint8_t *row)
{
    addr &= 0x7F;

    if (addr < 0x14) {
        *col = addr;
        *row = 0;
    } else if (addr >= 0x40 && addr < 0x40 + 0x14) {
        *col = addr - 0x40;
        *row = 1;
    } else if (addr >= 0x14 && addr < 0x28) {
        *col = addr - 0x14;
        *row = 2;
    } else if (addr >= 0x54 && addr < 0x54 + 0x14) {
        *col = addr - 0x54;
        *row = 3;
    } else {
        *col = addr % HD44780_COLS;
        *row = (addr / HD44780_COLS) % HD44780_ROWS;
    }
}

static HD44780Decoded hd44780_classify_instruction(uint8_t cmd)
{
    if (cmd == 0x01) {
        return HD44780_DECODED_CLEAR_DISPLAY;
    }
    if (cmd == 0x02) {
        return HD44780_DECODED_RETURN_HOME;
    }
    if ((cmd & 0xFC) == 0x04) {
        return HD44780_DECODED_ENTRY_MODE_SET;
    }
    if ((cmd & 0xF8) == 0x08) {
        return HD44780_DECODED_DISPLAY_ON_OFF;
    }
    if ((cmd & 0xF0) == 0x10) {
        return HD44780_DECODED_CURSOR_OR_DISPLAY_SHIFT;
    }
    if ((cmd & 0xE0) == 0x20) {
        return HD44780_DECODED_FUNCTION_SET;
    }
    if ((cmd & 0xC0) == 0x40) {
        return HD44780_DECODED_SET_CGRAM_ADDR;
    }
    if ((cmd & 0x80) != 0) {
        return HD44780_DECODED_SET_DDRAM_ADDR;
    }
    return HD44780_DECODED_RESERVED;
}

static void hd44780_write_cell(HD44780State *s, uint8_t byte)
{
    uint8_t col;
    uint8_t row;

    hd44780_addr_to_cell(s->ddram_addr, &col, &row);
    if (col < HD44780_COLS && row < HD44780_ROWS) {
        s->ddram[row][col] = hd44780_host_code_to_panel(byte);
    }

    if (s->entry_mode & 0x02) {
        s->ddram_addr = (s->ddram_addr + 1) & 0x7F;
    } else {
        s->ddram_addr = (s->ddram_addr - 1) & 0x7F;
    }
}

/* CGRAM writes advance their own counter, not DDRAM. XBMC reloads all 64 CGRAM
 * bytes every render pass, so treating these as screen content would splatter
 * bitmap data across the panel several times a second. */
static void hd44780_write_cgram(HD44780State *s, uint8_t byte)
{
    uint8_t glyph = (s->cgram_addr / HD44780_GLYPH_ROWS) %
                    HD44780_CGRAM_CHARS;
    uint8_t line = s->cgram_addr % HD44780_GLYPH_ROWS;

    s->cgram[glyph][line] = byte & 0x1F;
    s->cgram_addr = (s->cgram_addr + 1) & 0x3F;
}

static void hd44780_dispatch(HD44780State *s, uint8_t rs, HD44780Decoded kind,
                             uint8_t byte)
{
    if (rs != 0) {
        if (kind != HD44780_DECODED_DATA) {
            return;
        }
        if (s->in_cgram) {
            hd44780_write_cgram(s, byte);
        } else {
            hd44780_write_cell(s, byte);
        }
        return;
    }

    switch (kind) {
    case HD44780_DECODED_CLEAR_DISPLAY:
        memset(s->ddram, HD44780_BLANK, sizeof(s->ddram));
        s->ddram_addr = 0;
        s->in_cgram = false;
        break;
    case HD44780_DECODED_RETURN_HOME:
        s->ddram_addr = 0;
        s->in_cgram = false;
        break;
    case HD44780_DECODED_ENTRY_MODE_SET:
        s->entry_mode = byte & 0x07;
        break;
    case HD44780_DECODED_DISPLAY_ON_OFF:
        s->display_on = (byte & 0x04) != 0;
        s->cursor_on = (byte & 0x02) != 0;
        s->blink_on = (byte & 0x01) != 0;
        break;
    case HD44780_DECODED_CURSOR_OR_DISPLAY_SHIFT:
        /* Cursor shift moves the address counter; display shift does not. */
        if ((byte & 0x08) == 0) {
            if (byte & 0x04) {
                s->ddram_addr = (s->ddram_addr + 1) & 0x7F;
            } else {
                s->ddram_addr = (s->ddram_addr - 1) & 0x7F;
            }
        }
        break;
    case HD44780_DECODED_SET_CGRAM_ADDR:
        s->in_cgram = true;
        s->cgram_addr = byte & 0x3F;
        break;
    case HD44780_DECODED_SET_DDRAM_ADDR:
        s->ddram_addr = byte & 0x7F;
        s->in_cgram = false;
        break;
    default:
        break;
    }
}

void hd44780_write_byte(HD44780State *s, bool rs, uint8_t byte)
{
    s->active = true;

    if (rs) {
        hd44780_dispatch(s, 1, HD44780_DECODED_DATA, byte);
        return;
    }

    if (byte == 0x00) {
        return;
    }

    hd44780_dispatch(s, 0, hd44780_classify_instruction(byte), byte);
}

void hd44780_set_backlight(HD44780State *s, uint8_t val)
{
    s->backlight = val;
}

void hd44780_get_glyph(const HD44780State *s, uint8_t code,
                       uint8_t rows[HD44780_GLYPH_ROWS])
{
    /* Codes 0x00-0x0F address CGRAM: eight custom glyphs, mirrored twice. */
    if (code < 0x10) {
        memcpy(rows, s->cgram[code & (HD44780_CGRAM_CHARS - 1)],
               HD44780_GLYPH_ROWS);
        return;
    }

    memcpy(rows, hd44780_font_rom[code], HD44780_GLYPH_ROWS);
}
