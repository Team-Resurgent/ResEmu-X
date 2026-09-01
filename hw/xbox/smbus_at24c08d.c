/*
 * QEMU Atmel AT24C08D I2C Serial EEPROM
 *
 * Copyright (c) 2026 Team Resurgent
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/qdev-properties.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus_slave.h"
#include "smbus.h"
#include "qapi/error.h"
#include "hw/loader.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "qemu/notify.h"
#include "system/system.h"

#define TYPE_AT24C08D "at24c08d"
#define AT24C08D(obj) OBJECT_CHECK(AT24C08DDevice, (obj), TYPE_AT24C08D)

/* 8 Kbit part: 1 KiB, arranged as four 256-byte blocks of 16-byte pages. The
 * two block-select bits live in the I2C device address, so the part answers to
 * four consecutive addresses starting at the base address. */
#define AT24C08D_SIZE        1024
#define AT24C08D_NUM_BLOCKS  4
#define AT24C08D_BLOCK_SIZE  (AT24C08D_SIZE / AT24C08D_NUM_BLOCKS)
#define AT24C08D_PAGE_SIZE   16

/* The guest programs the part a couple of bytes per SMBus transaction, so wait
 * this long with no further writes before rewriting the backing file. */
#define AT24C08D_FLUSH_DELAY_MS 2000

/* Temporary tracing of every EEPROM access; set to 0 to silence. Shares the
 * modchip.log written by modchip_xecuter.c. */
#define AT24C08D_DEBUG 0

void modchip_debug_log(const char *tag, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

#if AT24C08D_DEBUG
# define DPRINTF(fmt, ...) modchip_debug_log("at24c08d", fmt, ## __VA_ARGS__)
#else
# define DPRINTF(fmt, ...) do { } while (0)
#endif

typedef struct AT24C08DDevice {
    SMBusDevice smbusdev;
    char *file;
    uint8_t addr;
    uint8_t block;
    bool persist;

    /* Backing store, owned by the block 0 device and shared by the others */
    uint8_t *data;
    uint32_t size;

    /* Word address within this device's block */
    uint32_t offset;

    /* The block 0 device owns the file and the deferred write-back; the other
     * blocks route their writes through it. */
    struct AT24C08DDevice *owner;
    QEMUTimer *flush_timer;
    Notifier exit_notifier;
    bool dirty;
} AT24C08DDevice;

static void at24c08d_flush_cb(void *opaque);
static void at24c08d_exit_notify(Notifier *n, void *opaque);

static void at24c08d_realize(DeviceState *dev, Error **errp)
{
    AT24C08DDevice *s = AT24C08D(dev);

    if (s->block >= AT24C08D_NUM_BLOCKS) {
        error_setg(errp, "%s: block %d out of range, expected 0-%d", __func__,
                   s->block, AT24C08D_NUM_BLOCKS - 1);
        return;
    }

    qdev_prop_set_uint8(dev, "address", s->addr + s->block);
    s->offset = 0;

    if (s->block > 0) {
        /* Shares the block 0 device's backing store, which only that device
         * saves and restores. */
        assert(s->data != NULL);
        assert(s->owner != NULL);
        s->size = 0;
        return;
    }

    s->owner = s;
    s->data = g_malloc0(AT24C08D_SIZE);
    s->size = AT24C08D_SIZE;
    s->flush_timer =
        timer_new_ms(QEMU_CLOCK_REALTIME, at24c08d_flush_cb, s);
    s->exit_notifier.notify = at24c08d_exit_notify;
    qemu_add_exit_notifier(&s->exit_notifier);

    if (!s->file || !*s->file) {
        error_setg(errp, "%s: file unspecified", __func__);
        return;
    }

    int size = get_image_size(s->file, NULL);
    if (size != AT24C08D_SIZE) {
        error_setg(errp, "%s: file '%s' size of %d, expected %d", __func__,
                   s->file, size, AT24C08D_SIZE);
        return;
    }

    int fd = qemu_open(s->file, O_RDONLY | O_BINARY, NULL);
    if (fd < 0) {
        error_setg(errp, "%s: file '%s' could not be opened", __func__,
                   s->file);
        return;
    }

    int rc = read(fd, s->data, AT24C08D_SIZE);
    close(fd);
    if (rc != AT24C08D_SIZE) {
        error_setg(errp, "%s: file '%s' read failure", __func__, s->file);
        return;
    }

    /* Attach a device for each of the remaining blocks so the whole part is
     * reachable across its four I2C addresses. */
    I2CBus *bus = I2C_BUS(qdev_get_parent_bus(dev));
    for (int i = 1; i < AT24C08D_NUM_BLOCKS; i++) {
        DeviceState *blk = qdev_new(TYPE_AT24C08D);
        AT24C08DDevice *bs = AT24C08D(blk);

        bs->data = s->data;
        bs->owner = s;
        qdev_prop_set_uint8(blk, "addr", s->addr);
        qdev_prop_set_uint8(blk, "block", i);
        qdev_prop_set_string(blk, "file", s->file);
        object_property_set_bool(OBJECT(blk), "persist", s->persist,
                                 &error_abort);

        if (!qdev_realize_and_unref(blk, (BusState *)bus, errp)) {
            return;
        }
    }

    DPRINTF("attached at 0x%02x-0x%02x, %d bytes from '%s'", s->addr,
            s->addr + AT24C08D_NUM_BLOCKS - 1, AT24C08D_SIZE, s->file);

#if AT24C08D_DEBUG
    /* Dump the head of the loaded image so it is obvious the file contents,
     * and not an empty array, are what the guest will see. */
    for (int off = 0; off < 64; off += 16) {
        DPRINTF("LOAD   %03x: %02x %02x %02x %02x %02x %02x %02x %02x "
                "%02x %02x %02x %02x %02x %02x %02x %02x", off,
                s->data[off + 0], s->data[off + 1], s->data[off + 2],
                s->data[off + 3], s->data[off + 4], s->data[off + 5],
                s->data[off + 6], s->data[off + 7], s->data[off + 8],
                s->data[off + 9], s->data[off + 10], s->data[off + 11],
                s->data[off + 12], s->data[off + 13], s->data[off + 14],
                s->data[off + 15]);
    }

    uint32_t sum = 0;
    for (int i = 0; i < AT24C08D_SIZE; i++) {
        sum += s->data[i];
    }
    DPRINTF("LOAD   checksum of %d bytes = 0x%08x", AT24C08D_SIZE, sum);
#endif
}

static void at24c08d_flush(AT24C08DDevice *s)
{
    if (!s->dirty || !s->persist || !s->file || !*s->file) {
        return;
    }

    s->dirty = false;

    int fd = qemu_open(s->file, O_WRONLY | O_BINARY, NULL);
    if (fd < 0) {
        DPRINTF("PERSIST '%s' could not be opened", s->file);
        return;
    }

    int wc = write(fd, s->data, AT24C08D_SIZE);
    if (wc != AT24C08D_SIZE) {
        DPRINTF("PERSIST '%s' write failure (%d/%d)", s->file, wc,
                AT24C08D_SIZE);
    } else {
        DPRINTF("PERSIST wrote %d bytes back to '%s'", AT24C08D_SIZE, s->file);
    }
    close(fd);
}

static void at24c08d_flush_cb(void *opaque)
{
    at24c08d_flush((AT24C08DDevice *)opaque);
}

static void at24c08d_exit_notify(Notifier *n, void *opaque)
{
    at24c08d_flush(container_of(n, AT24C08DDevice, exit_notifier));
}

/* Defer the write-back so that a burst of byte or word writes costs one file
 * rewrite rather than one per transaction. */
static void at24c08d_mark_dirty(AT24C08DDevice *s)
{
    AT24C08DDevice *owner = s->owner;

    owner->dirty = true;

    if (owner->flush_timer) {
        timer_mod(owner->flush_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) +
                                          AT24C08D_FLUSH_DELAY_MS);
    }
}

static int at24c08d_write_data(SMBusDevice *dev, uint8_t *buf, uint8_t len)
{
    AT24C08DDevice *s = AT24C08D(dev);

    /* len is guaranteed to be > 0; the first byte is the word address */
    s->offset = buf[0];
    buf++;
    len--;

    if (len == 0) {
        /* Address-only write, setting up a following read */
        DPRINTF("SEEK   addr=0x%02x block=%u word=0x%02x", s->addr + s->block,
                s->block, s->offset);
    }

    bool changed = false;
    for (; len > 0; len--) {
        uint32_t pos = s->block * AT24C08D_BLOCK_SIZE + s->offset;
        s->data[pos] = *buf++;
        changed = true;
        DPRINTF("WRITE  addr=0x%02x off=0x%03x data=0x%02x",
                s->addr + s->block, pos, s->data[pos]);
        /* Page writes wrap at the page boundary rather than carrying into the
         * next page, as on the real part. */
        s->offset = (s->offset & ~(AT24C08D_PAGE_SIZE - 1)) |
                    ((s->offset + 1) & (AT24C08D_PAGE_SIZE - 1));
    }

    if (changed) {
        at24c08d_mark_dirty(s);
    }

    return 0;
}

static uint8_t at24c08d_receive_byte(SMBusDevice *dev)
{
    AT24C08DDevice *s = AT24C08D(dev);

    uint32_t pos = s->block * AT24C08D_BLOCK_SIZE + s->offset;
    uint8_t val = s->data[pos];
    DPRINTF("READ   addr=0x%02x off=0x%03x val=0x%02x", s->addr + s->block, pos,
            val);
    /* Sequential reads roll over within the selected block, since the block
     * select bits are latched from the device address. */
    s->offset = (s->offset + 1) % AT24C08D_BLOCK_SIZE;

    return val;
}

static const VMStateDescription vmstate_at24c08d = {
    .name = TYPE_AT24C08D,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_SMBUS_DEVICE(smbusdev, AT24C08DDevice),
        VMSTATE_VBUFFER_UINT32(data, AT24C08DDevice, 1, NULL, size),
        VMSTATE_UINT32(offset, AT24C08DDevice),
        VMSTATE_END_OF_LIST()
    }
};

static const Property at24c08d_props[] = {
    /* A2 tied low, so the part occupies 0x50-0x53. The Xbox EEPROM at 0x54 is
     * unaffected. */
    DEFINE_PROP_UINT8("addr", AT24C08DDevice, addr, 0x50),
    DEFINE_PROP_UINT8("block", AT24C08DDevice, block, 0),
    DEFINE_PROP_BOOL("persist", AT24C08DDevice, persist, true),
    DEFINE_PROP_STRING("file", AT24C08DDevice, file),
};

static void at24c08d_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_at24c08d;
    dc->realize = at24c08d_realize;
    sc->receive_byte = at24c08d_receive_byte;
    sc->write_data = at24c08d_write_data;
    device_class_set_props(dc, at24c08d_props);
}

static TypeInfo at24c08d_info = {
    .name = TYPE_AT24C08D,
    .parent = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(AT24C08DDevice),
    .class_init = at24c08d_class_init,
};

static void at24c08d_register_devices(void)
{
    type_register_static(&at24c08d_info);
}

type_init(at24c08d_register_devices)
