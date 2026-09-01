/*
 * HD44780-compatible character LCD controller
 *
 * This is the panel itself: the instruction set, the character and custom-glyph
 * memories, and the font ROM. It takes whole command or data bytes and knows
 * nothing about how they reached it, so a transport can be a bit-banged 4-bit
 * parallel bus on a modchip's I/O port, an I2C backpack, or anything else.
 *
 * The instruction decode is ported from the ESP32 bus sniffer in
 * Team-Resurgent's X3-LCD-OLED firmware (src/hd44780_slave.c), which is proven
 * against both PrometheOS and XBMC4Xbox.
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

#ifndef HW_XBOX_LCD_HD44780_H
#define HW_XBOX_LCD_HD44780_H

#include <stdbool.h>
#include <stdint.h>

#define HD44780_COLS        20
#define HD44780_ROWS        4
#define HD44780_CGRAM_CHARS 8
#define HD44780_GLYPH_ROWS  8

typedef struct HD44780State {
    /* Panel contents in panel character codes, ready to hand to the font. */
    uint8_t ddram[HD44780_ROWS][HD44780_COLS];
    /* Custom glyph bitmaps, 5 bits per row, bit 4 leftmost. */
    uint8_t cgram[HD44780_CGRAM_CHARS][HD44780_GLYPH_ROWS];

    uint8_t backlight;

    /* Controller state. */
    uint8_t ddram_addr;
    uint8_t entry_mode;
    uint8_t cgram_addr;
    bool in_cgram;
    bool display_on;
    bool cursor_on;
    bool blink_on;

    /* Set once the guest has sent anything, so the UI can tell an unused panel
     * apart from one deliberately cleared. */
    bool active;
} HD44780State;

void hd44780_reset(HD44780State *s);

/* Accepts one assembled byte. rs selects the data register over the instruction
 * register, exactly as the RS line does on the real part. */
void hd44780_write_byte(HD44780State *s, bool rs, uint8_t byte);
void hd44780_set_backlight(HD44780State *s, uint8_t val);

/* Publishes a panel for the UI to draw. Only one panel is supported, since only
 * one display can be attached at a time. */
void hd44780_register_debug(HD44780State *s);
const HD44780State *hd44780_get_debug_state(void);

/* Resolves a character code to its 5x8 bitmap, taking custom glyphs from CGRAM
 * and everything else from the font ROM, so callers need not know about CGRAM. */
void hd44780_get_glyph(const HD44780State *s, uint8_t code,
                       uint8_t rows[HD44780_GLYPH_ROWS]);

#endif
