/*
 * Character LCD on the Xecuter modchip's display header
 *
 * The Xecuter exposes the panel's 4-bit parallel bus as five registers inside
 * its I/O window: one carrying a data nibble, one carrying the RS/RW/E control
 * lines, two direction registers, and a backlight control. The guest bit-bangs
 * the HD44780 protocol through them.
 *
 * This is a device in its own right rather than part of modchip-xecuter, so
 * that the display can be attached or not independently of which modchip is
 * emulated, and so that displays on other buses can be added alongside it
 * without touching any modchip. It claims its registers by overlaying the
 * modchip's window at a higher priority, which is also what happens
 * electrically: the modchip decodes the rest of the window and the display
 * header takes these.
 *
 * Everything about nibbles lives here rather than in lcd_hd44780.c, since
 * pairing is a property of a 4-bit parallel bus and not of the controller. A
 * display on a byte-wide bus feeds hd44780_write_byte directly.
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
#include "hw/isa/isa.h"
#include "qapi/error.h"

#include "hw/xbox/lcd_hd44780.h"

/* Register block, named as in PrometheOS' XECUTER_REGISTER_DISP_O_*. */
#define LCD_XECUTER_BASE          0xF503
#define LCD_XECUTER_SIZE          5
#define LCD_XECUTER_REG_LIGHT     0x0
#define LCD_XECUTER_REG_DAT       0x1
#define LCD_XECUTER_REG_CMD       0x2
#define LCD_XECUTER_REG_DIR_DAT   0x3
#define LCD_XECUTER_REG_DIR_CMD   0x4

/* Control lines within the command register. */
#define LCD_XECUTER_RS 0x01
#define LCD_XECUTER_RW 0x02
#define LCD_XECUTER_E  0x04

/* The modchip's window sits at priority 0, so anything above it wins. */
#define LCD_XECUTER_IO_PRIORITY 1

typedef struct LcdXecuterState {
    ISADevice dev;
    MemoryRegion io;
    HD44780State panel;

    /* Last values written, so reads see something plausible. */
    uint8_t data;
    uint8_t ctrl;
    uint8_t dir_dat;
    uint8_t dir_cmd;

    /* Nibble pairing. An HD44780 powers on in 8-bit mode, and we assume 8-bit
     * whenever the phase is unknown, so that the host's next wake-up strobes
     * resynchronise us rather than pairing off by one forever. */
    bool dl_8bit;
    bool lo_pending;
    uint8_t nibble_hi;
    bool latched_rs;
    uint8_t init_run;
} LcdXecuterState;

#define TYPE_LCD_XECUTER "lcd-xecuter"
#define LCD_XECUTER(obj) \
    OBJECT_CHECK(LcdXecuterState, (obj), TYPE_LCD_XECUTER)

static void lcd_xecuter_byte(LcdXecuterState *s, bool rs, uint8_t byte)
{
    /* A function set carries the interface width, and is the point at which a
     * host switching to 4-bit mode realigns our pairing. */
    if (!rs && (byte & 0xE0) == 0x20) {
        s->dl_8bit = (byte & 0x10) != 0;
        s->lo_pending = false;
    }

    hd44780_write_byte(&s->panel, rs, byte);
}

static void lcd_xecuter_strobe(LcdXecuterState *s, uint8_t ctrl)
{
    uint8_t nibble;
    bool rs;

    /* A read cycle says nothing about panel contents, and leaves our pairing
     * phase unknown. */
    if (ctrl & LCD_XECUTER_RW) {
        s->lo_pending = false;
        return;
    }

    nibble = (s->data >> 4) & 0x0F;
    rs = (ctrl & LCD_XECUTER_RS) != 0;

    /* Mode-independent reset detector. The wake-up sequence is three 0x3
     * command nibbles whatever mode the host thinks it is in, and no ordinary
     * 4-bit command stream produces three in a row. Seeing them means the host
     * is resetting, so abandon our phase and hunt in 8-bit mode; the 0x2 that
     * follows then realigns us. */
    if (!rs && nibble == 0x3) {
        if (s->init_run < 3) {
            s->init_run++;
        }
        if (s->init_run == 3) {
            s->dl_8bit = true;
            s->lo_pending = false;
        }
    } else {
        s->init_run = 0;
    }

    if (s->dl_8bit) {
        if (!rs) {
            lcd_xecuter_byte(s, false, nibble << 4);
        }
        /* Data while we believe the bus is 8 bits wide is either a genuine
         * 8-bit write, which none of the hosts do, or traffic we are
         * misaligned on. Drop it rather than draw garbage. */
        return;
    }

    if (!s->lo_pending) {
        s->nibble_hi = nibble;
        s->latched_rs = rs;
        s->lo_pending = true;
        return;
    }

    s->lo_pending = false;
    lcd_xecuter_byte(s, s->latched_rs, (s->nibble_hi << 4) | nibble);
}

static void lcd_xecuter_io_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned int size)
{
    LcdXecuterState *s = opaque;

    switch (addr) {
    case LCD_XECUTER_REG_LIGHT:
        hd44780_set_backlight(&s->panel, val);
        break;
    case LCD_XECUTER_REG_DAT:
        /* Held until E is strobed. */
        s->data = val;
        break;
    case LCD_XECUTER_REG_CMD: {
        bool e_was_high = (s->ctrl & LCD_XECUTER_E) != 0;
        bool e_is_high = (val & LCD_XECUTER_E) != 0;

        s->ctrl = val;
        if (!e_was_high && e_is_high) {
            lcd_xecuter_strobe(s, val);
        }
        break;
    }
    case LCD_XECUTER_REG_DIR_DAT:
        s->dir_dat = val;
        break;
    case LCD_XECUTER_REG_DIR_CMD:
        s->dir_cmd = val;
        break;
    default:
        break;
    }
}

static uint64_t lcd_xecuter_io_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    LcdXecuterState *s = opaque;

    switch (addr) {
    case LCD_XECUTER_REG_LIGHT:
        return s->panel.backlight;
    case LCD_XECUTER_REG_DAT:
        return s->data;
    case LCD_XECUTER_REG_CMD:
        return s->ctrl;
    case LCD_XECUTER_REG_DIR_DAT:
        return s->dir_dat;
    case LCD_XECUTER_REG_DIR_CMD:
        return s->dir_cmd;
    default:
        return 0;
    }
}

static const MemoryRegionOps lcd_xecuter_io_ops = {
    .read = lcd_xecuter_io_read,
    .write = lcd_xecuter_io_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void lcd_xecuter_realize(DeviceState *dev, Error **errp)
{
    LcdXecuterState *s = LCD_XECUTER(dev);
    ISADevice *isa = ISA_DEVICE(dev);

    hd44780_reset(&s->panel);
    hd44780_register_debug(&s->panel);

    s->dl_8bit = true;

    memory_region_init_io(&s->io, OBJECT(s), &lcd_xecuter_io_ops, s,
                          "lcd-xecuter.io", LCD_XECUTER_SIZE);
    memory_region_add_subregion_overlap(isa_address_space_io(isa),
                                        LCD_XECUTER_BASE, &s->io,
                                        LCD_XECUTER_IO_PRIORITY);
}

static void lcd_xecuter_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Xecuter character LCD";
    dc->realize = lcd_xecuter_realize;
}

static const TypeInfo lcd_xecuter_type_info = {
    .name          = TYPE_LCD_XECUTER,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(LcdXecuterState),
    .class_init    = lcd_xecuter_class_init,
};

static void lcd_xecuter_register_types(void)
{
    type_register_static(&lcd_xecuter_type_info);
}

type_init(lcd_xecuter_register_types)
