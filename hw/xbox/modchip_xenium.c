/*
 * Xenium Modchip - https://github.com/Ryzee119/OpenXenium
 *
 * Copyright (c) 2021 Mike Davis
 * Copyright (c) 2021 Ryzee119
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/option.h"
#include "qemu/datadir.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/loader.h"
#include "hw/char/serial.h"
#include "hw/isa/isa.h"
#include "qapi/error.h"

#define XENIUM_REGISTER_BASE 0xEE
#define XENIUM_REGISTER0 0
#define XENIUM_REGISTER1 1

#define DEBUG
#ifdef DEBUG
# define DPRINTF(format, ...) printf(format, ## __VA_ARGS__)
#else
# define DPRINTF(format, ...) do { } while (0)
#endif

#define XENIUM_FLASH_MANUF_ID (0x01)
#define XENIUM_FLASH_DEV_ID (0xC4)
#define XENIUM_FLASH_SIZE (2 * 1024 * 1024)
#define XENIUM_MAX_BANK_SIZE (1024 * 1024)
#define MCPX_SIZE (512)

extern MemoryRegion *rom_memory__; //FIXME

uint8_t xenium_raw[XENIUM_FLASH_SIZE];
uint8_t mcpx_raw[MCPX_SIZE];

typedef struct XeniumBank {
    unsigned int offset;
    unsigned int size;
} XeniumBank_t;

static const XeniumBank_t XeniumBank[11] = 
{
    {0, 1 * 1024 * 1024},   //TSOP
    {0x180000, 256 * 1024}, //Bootloader
    {0x100000, 512 * 1024}, //XeniumOS
    {0x000000, 256 * 1024}, //Bank 1 256k
    {0x040000, 256 * 1024}, //Bank 2 256k
    {0x080000, 256 * 1024}, //Bank 3 256k
    {0x0C0000, 256 * 1024}, //Bank 4 256k
    {0x000000, 512 * 1024}, //Bank 1 512k
    {0x080000, 512 * 1024}, //Bank 2 512k
    {0x000000, 1024 * 1024}, //Bank 1 1M
    {0x1C0000, 256 * 1024}, //Recovery + More XeniumOS Data + User settings
};

// Dumped using this script https://gist.github.com/LoveMHz/8c20b0bb7fcd88588a1740657396075c
static const uint8_t XeniumFlashCFI[] = {
    /* 00h */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 10h */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 20h */ 0x51, 0x51, 0x52, 0x52, 0x59, 0x59, 0x02, 0x02, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    /* 30h */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x27, 0x27, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03,
    /* 40h */ 0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x05, 0x05, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x15, 0x15,
    /* 50h */ 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40,
    /* 60h */ 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80,
    /* 70h */ 0x00, 0x00, 0x1E, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* 80h */ 0x50, 0x50, 0x52, 0x52, 0x49, 0x49, 0x31, 0x31, 0x33, 0x33, 0x0C, 0x0C, 0x02, 0x02, 0x01, 0x01,
    /* 90h */ 0x01, 0x01, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03,
    /* A0h */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* B0h */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06,
    /* C0h */ 0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x05, 0x05, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x15, 0x15,
    /* D0h */ 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40,
    /* E0h */ 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80,
    /* F0h */ 0x00, 0x00, 0x1E, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

typedef enum {
    XENIUM_MEMORY_STATE_NORMAL,
    XENIUM_MEMORY_STATE_CFI,
    XENIUM_MEMORY_STATE_AUTOSELECT,
    XENIUM_MEMORY_STATE_SECTOR_ERASE,
    XENIUM_MEMORY_STATE_WRITE,
} XeniumMemoryState;

typedef struct XeniumState {
    ISADevice dev;
    SysBusDevice dev_sysbus;
    MemoryRegion io;
    MemoryRegion flash_mem;

    // SPI
    bool sck;
    bool cs;
    bool mosi;
    bool miso_1;    // pin 1
    bool miso_4;    // pin 4

    unsigned char led;              // XXXXXBGR
    unsigned short bank_control;    // determines flash address mask

    bool recovery;  // 0 is active

    char *rom_file;
    XeniumMemoryState flash_state;
    unsigned char flash_cycle;
} XeniumState;

#define XENIUM_DEVICE(obj) \
    OBJECT_CHECK(XeniumState, (obj), "modchip-xenium")

static void xenium_io_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned int size)
{
    XeniumState *s = opaque;

    DPRINTF("%s: Write 0x%llX to IO register 0x%llX\n",
        __func__, val, XENIUM_REGISTER_BASE + addr);

    switch(addr) {
        case XENIUM_REGISTER0:
            assert((val >> 3) == 0);    // un-known/used
            s->led = val;
            DPRINTF("%s: Set LED color(s) to %d\n", __func__, s->led);
        break;
        case XENIUM_REGISTER1:
            assert((val & (1 << 7)) == 0);    // un-known/used
            s->sck = val & (1 << 6);
            s->cs = val & (1 << 5);
            s->mosi = val & (1 << 4);
            s->bank_control = val & 0xF;
            unsigned int flash_size = XeniumBank[s->bank_control].size;
            DPRINTF("%s: Set Bank to %d, Offset: %08x, Size: %d bytes\n", __func__, s->bank_control,
                                                                          XeniumBank[s->bank_control].offset,
                                                                          flash_size);

        break;
        default: assert(false);
    }
}

static uint64_t xenium_io_read(void *opaque, hwaddr addr, unsigned int size)
{
    XeniumState *s = opaque;
    uint32_t val = 0;

    switch(addr) {
        case XENIUM_REGISTER0:
            val = 0x55;     // genuine xenium!
        break;
        case XENIUM_REGISTER1:
            val = (s->recovery << 7) |
                (s->miso_1 << 5) |
                (s->miso_4 << 4) |
                s->bank_control;
        break;
        default: assert(false);
    }

    DPRINTF("%s: Read 0x%X from IO register 0x%llX\n",
        __func__, val, XENIUM_REGISTER_BASE + addr);

    return val;
}

static const MemoryRegionOps xenium_io_ops = {
    .read  = xenium_io_read,
    .write = xenium_io_write,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static uint64_t flash_read(void *opaque, hwaddr offset, unsigned size)
{
    XeniumState *s = opaque;
    if(s->flash_state == XENIUM_MEMORY_STATE_NORMAL) {
        //Handle mirroring
        offset %= XeniumBank[s->bank_control].size;
        //Handle banking
        offset |= XeniumBank[s->bank_control].offset;
        if (size == 1) {
            uint8_t *flash_mem = (uint8_t *)xenium_raw;
            return flash_mem[offset];
        }
        else if (size == 2) {
            uint16_t *flash_mem = (uint16_t *)xenium_raw;
            return flash_mem[offset/2];
        }
        else if (size == 4) {
            uint32_t *flash_mem = (uint32_t *)xenium_raw;
            return flash_mem[offset/4];
        }
        else {
            DPRINTF("%s Unsupported read len %d\n", __FUNCTION__, size);
            assert(0);
        }
    }

    DPRINTF("%s offset: %08x size: %d\n", __FUNCTION__, (uint32_t)offset, size);

    if(s->flash_state == XENIUM_MEMORY_STATE_CFI) {
        return XeniumFlashCFI[(size == 1 ? offset : offset << 1) % sizeof(XeniumFlashCFI)];
    }

    if(s->flash_state == XENIUM_MEMORY_STATE_AUTOSELECT) {
        switch (offset) {
            case 0:
                DPRINTF("%s Sending Manufacturer ID %02x\n", __FUNCTION__, XENIUM_FLASH_MANUF_ID);
                return XENIUM_FLASH_MANUF_ID;
            case 2:
                DPRINTF("%s Sending Device ID %02x\n", __FUNCTION__, XENIUM_FLASH_DEV_ID);
                return XENIUM_FLASH_DEV_ID;
        }
        DPRINTF("%s Invalid Chip ID offset: %08x\n", __FUNCTION__, (uint32_t)offset);
    }

    return 0;
}

static void flash_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned int size)
{
    XeniumState *s = opaque;

    DPRINTF("%s offset: %08x value: %02x size: %d, cycle: %d\n", __FUNCTION__, (uint32_t)offset, (uint8_t)value, size, s->flash_cycle);

    // Reset
    if(offset == 0x00 && value == 0xF0 && size == 1) {
        DPRINTF("%s Flash Reset (Entering Normal flash state)\n", __FUNCTION__);

        s->flash_state = XENIUM_MEMORY_STATE_NORMAL;
        s->flash_cycle = 1;
        return;
    }

    if (s->flash_state == XENIUM_MEMORY_STATE_WRITE)
    {
        DPRINTF("%s Flash Write offset = %08x, value %02x\n", __FUNCTION__, offset, value);

        //Handle mirroring
        offset %= XeniumBank[s->bank_control].size;
        //Handle banking
        offset |= XeniumBank[s->bank_control].offset;
        if (size == 1) {
            uint8_t *flash_mem = (uint8_t *)xenium_raw;
            flash_mem[offset] = (uint8_t)value;
        }
        else if (size == 2) {
            uint16_t *flash_mem = (uint16_t *)xenium_raw;
            flash_mem[offset/2] = (uint16_t)value;
        }
        else if (size == 4) {
            uint32_t *flash_mem = (uint32_t *)xenium_raw;
            flash_mem[offset/4] = (uint32_t)value;
        }
        else {
            DPRINTF("%s Unsupported write len %d\n", __FUNCTION__, size);
            assert(0);
        }

        s->flash_state = XENIUM_MEMORY_STATE_NORMAL;
        s->flash_cycle = 1;
        return;
    }

    switch (s->flash_cycle)
    {
        case 1:
            // Enter CFI Mode
            if(offset == 0xAA && value == 0x98 && size == 1) {
                DPRINTF("%s Entering CFI Mode flash state\n", __FUNCTION__);

                s->flash_state = XENIUM_MEMORY_STATE_CFI;
            }
            else if(offset == 0xAAAA && value == 0xAA && size == 1) {
                s->flash_cycle++;
            }
            else {
                DPRINTF("%s Unimplemented Flash command\n", __FUNCTION__);
            }
            break;
        case 2:
            if(offset == 0x5555 && value == 0x55 && size == 1) {
                s->flash_cycle++;
            }
            else {
                DPRINTF("%s Unimplemented Flash command\n", __FUNCTION__);
            }
            break;
        case 3:
            if(offset == 0xAAAA && value == 0x80 && size == 1) {
                s->flash_cycle++;
            }
            else if(offset == 0xAAAA && value == 0x90 && size == 1) {
                DPRINTF("%s Entering Autoselect Mode flash state\n", __FUNCTION__);

                s->flash_state = XENIUM_MEMORY_STATE_AUTOSELECT;
            }
            else if(offset == 0xAAAA && value == 0xA0 && size == 1) {
                DPRINTF("%s Entering flash write state\n", __FUNCTION__);

                s->flash_state = XENIUM_MEMORY_STATE_WRITE;
            }
            else {
                DPRINTF("%s Unimplemented Flash command\n", __FUNCTION__);
            }
            break;
        case 4:
            if(offset == 0xAAAA && value == 0xAA && size == 1) {
                s->flash_cycle++;
            }
            break;
        case 5:
            if(offset == 0x5555 && value == 0x55 && size == 1) {
                s->flash_cycle++;
            }
            break;
        case 6:
            if(value == 0x30 && size == 1) {
                DPRINTF("%s Entering Sector Erase State, Offset = %04llx\n", __FUNCTION__, offset);
                
                s->flash_state = XENIUM_MEMORY_STATE_SECTOR_ERASE;
            }
            break;
    }
}

static const MemoryRegionOps xenium_flash_ops = {
    .read = flash_read,
    .write = flash_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void xenium_realize(DeviceState *dev, Error **errp)
{
    XeniumState *s = XENIUM_DEVICE(dev);
    ISADevice *isa = ISA_DEVICE(dev);
    Error *err = NULL;

    //Read Xenium Flash Dump (2MB file)
    int fd = qemu_open(s->rom_file, O_RDONLY | O_BINARY, NULL);
    assert(fd >= 0);
    read(fd, xenium_raw, XENIUM_FLASH_SIZE);
    close(fd);

    //Read MCPX Dump (512 bytes)
    const char *bootrom_file =
        object_property_get_str(qdev_get_machine(), "bootrom", NULL);

    if ((bootrom_file != NULL) && *bootrom_file) {
        char *filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bootrom_file);
        assert(filename);

        /* Read in MCPX ROM over last 512 bytes of BIOS data */
        int fd = qemu_open(filename, O_RDONLY | O_BINARY, NULL);
        assert(fd >= 0);
        read(fd, mcpx_raw, MCPX_SIZE);
        close(fd);
        g_free(filename);
    }

    // default state
    s->bank_control = 1;                         // bootloader
    s->recovery = 1;                             // inactive
    s->led = 1;                                  // red
    s->flash_state = XENIUM_MEMORY_STATE_NORMAL; // Default flash state
    s->flash_cycle = 1;                          // Flash command cycle tracker

    #define ROM_END 0xFFFFFFFF
    #define ROM_START 0xFF000000
    #define ROM_AREA (ROM_END - ROM_START - MCPX_SIZE)

    memory_region_init_rom_device(&s->flash_mem, OBJECT(s), &xenium_flash_ops, s, "xenium.bios", ROM_AREA, &err);
    memory_region_rom_device_set_romd(&s->flash_mem, false);

    //Setup memory aliases over the entire Flash ROM mapped region. (0xFF000000 to 0xFFFFFFFF)
    MemoryRegion *mr_bios = g_malloc(sizeof(MemoryRegion));
    assert(mr_bios != NULL);
    memory_region_init_alias(mr_bios, NULL, "xenium.bios.alias", &s->flash_mem, 0, ROM_AREA);
    memory_region_add_subregion(rom_memory__, ROM_START, mr_bios);

    //Add MCPX memory and alias it to 0xFFFFFE00 in Xbox memory
    //FIXME, most of page is not mirrored properly and overlaying the ideal 512bytes is really slow
    unsigned int page_size = 4096;
    MemoryRegion *mr_mcpx = g_malloc(sizeof(MemoryRegion));
    memory_region_init_ram(mr_mcpx, NULL, "xbox.mcpx", page_size, &err);
    void *mcpx_data = memory_region_get_ram_ptr(mr_mcpx);
    memcpy(mcpx_data + page_size - MCPX_SIZE, mcpx_raw, MCPX_SIZE);
    MemoryRegion *mr_mcpx_alias = g_malloc(sizeof(MemoryRegion));
    memory_region_init_alias(mr_mcpx_alias, NULL, "xbox.mcpx.alias", mr_mcpx, 0, page_size);
    memory_region_add_subregion(rom_memory__, -page_size, mr_mcpx_alias);

    //Register Xenium Chip IO
    memory_region_init_io(&s->io, OBJECT(s), &xenium_io_ops, s, "xenium.io", 2);   // 0xEE & 0xEF
    isa_register_ioport(isa, &s->io, XENIUM_REGISTER_BASE);
}

static Property xenium_properties[] = {
    DEFINE_PROP_STRING("rom-path", XeniumState, rom_file),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_xenium = {
    .name = "modchip-xenium",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void xenium_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xenium_realize;
    dc->vmsd = &vmstate_xenium;
    device_class_set_props(dc, xenium_properties);
}

static void xenium_initfn(Object *o)
{
    XeniumState *self = XENIUM_DEVICE(o);
}

static const TypeInfo xenium_type_info = {
    .name          = "modchip-xenium",
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(XeniumState),
    .instance_init = xenium_initfn,
    .class_init    = xenium_class_init,
};

static void xenium_register_types(void)
{
    type_register_static(&xenium_type_info);
}

type_init(xenium_register_types)