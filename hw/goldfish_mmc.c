/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/
#include "vl.h"
#include "goldfish_device.h"
#include "mmc.h"
#include "sd.h"

enum {
    /* status register */
    MMC_INT_STATUS          = 0x00,
    /* set this to enable IRQ */
    MMC_INT_ENABLE          = 0x04,
    /* set this to specify buffer address */
    MMC_SET_BUFFER          = 0x08,

    /* MMC command number */
    MMC_CMD                 = 0x0C,

    /* MMC argument */
    MMC_ARG                 = 0x10,

    /* MMC response (or R2 bits 0 - 31) */
    MMC_RESP_0              = 0x14,

    /* MMC R2 response bits 32 - 63 */
    MMC_RESP_1              = 0x18,

    /* MMC R2 response bits 64 - 95 */
    MMC_RESP_2              = 0x1C,

    /* MMC R2 response bits 96 - 127 */
    MMC_RESP_3              = 0x20,

    MMC_BLOCK_LENGTH        = 0x24,
    MMC_BLOCK_COUNT         = 0x28,

    /* MMC state flags */
    MMC_STATE               = 0x2C,

    /* MMC_INT_STATUS bits */

    MMC_STAT_END_OF_CMD     = 1U << 0,
    MMC_STAT_END_OF_DATA    = 1U << 1,
    MMC_STAT_STATE_CHANGE   = 1U << 2,

    /* MMC_STATE bits */
    MMC_STATE_INSERTED     = 1U << 0,
    MMC_STATE_READ_ONLY     = 1U << 1,
};


struct goldfish_mmc_state {
    struct goldfish_device dev;
    BlockDriverState *bs;
    // pointer to our buffer
    uint8_t* buffer;
    // offsets for read and write operations
    uint32_t read_offset, write_offset;
    // buffer status flags
    uint32_t int_status;
    // irq enable mask for int_status
    uint32_t int_enable;

    // MMC command argument
    uint32_t arg;
    uint32_t resp[4];

    uint32_t block_length;
    uint32_t block_count;
    int is_SDHC;
};

#define  GOLDFISH_MMC_SAVE_VERSION  1
#define  QFIELD_STRUCT  struct goldfish_mmc_state
QFIELD_BEGIN(goldfish_mmc_fields)
    QFIELD_INT32(read_offset),
    QFIELD_INT32(write_offset),
    QFIELD_INT32(int_status),
    QFIELD_INT32(int_enable),
    QFIELD_INT32(arg),
    QFIELD_INT32(resp[0]),
    QFIELD_INT32(resp[1]),
    QFIELD_INT32(resp[2]),
    QFIELD_INT32(resp[3]),
    QFIELD_INT32(block_length),
    QFIELD_INT32(block_count),
    QFIELD_INT32(is_SDHC),
QFIELD_END

static void  goldfish_mmc_save(QEMUFile*  f, void*  opaque)
{
    struct goldfish_mmc_state*  s = opaque;

    qemu_put_be32(f, s->buffer - phys_ram_base);
    qemu_put_struct(f, goldfish_mmc_fields, s);
}

static int  goldfish_mmc_load(QEMUFile*  f, void*  opaque, int  version_id)
{
    struct goldfish_mmc_state*  s = opaque;

    if (version_id != GOLDFISH_MMC_SAVE_VERSION)
        return -1;

    s->buffer = qemu_get_be32(f) + phys_ram_base;
    return qemu_get_struct(f, goldfish_mmc_fields, s);
}

struct mmc_opcode {
    const char* name;
    int cmd;
} mmc_opcodes[] = {
    { "MMC_GO_IDLE_STATE",         0  },
    { "MMC_SEND_OP_COND",          1  },
    { "MMC_ALL_SEND_CID",          2  },
    { "MMC_SET_RELATIVE_ADDR",     3  },
    { "MMC_SET_DSR",               4  },
    { "MMC_SWITCH",                6  },
    { "MMC_SELECT_CARD",           7  },
    { "MMC_SEND_EXT_CSD",          8  },
    { "MMC_SEND_CSD",              9  },
    { "MMC_SEND_CID",             10  },
    { "MMC_READ_DAT_UNTIL_STOP",  11  },
    { "MMC_STOP_TRANSMISSION",    12  },
    { "MMC_SEND_STATUS",          13  },
    { "MMC_GO_INACTIVE_STATE",    15  },
    { "MMC_SET_BLOCKLEN",         16  },
    { "MMC_READ_SINGLE_BLOCK",    17  },
    { "MMC_READ_MULTIPLE_BLOCK",  18  },
    { "MMC_WRITE_DAT_UNTIL_STOP", 20  },
    { "MMC_SET_BLOCK_COUNT",      23  },
    { "MMC_WRITE_BLOCK",          24  },
    { "MMC_WRITE_MULTIPLE_BLOCK", 25  },
    { "MMC_PROGRAM_CID",          26  },
    { "MMC_PROGRAM_CSD",          27  },
    { "MMC_SET_WRITE_PROT",       28  },
    { "MMC_CLR_WRITE_PROT",       29  },
    { "MMC_SEND_WRITE_PROT",      30  },
    { "MMC_ERASE_GROUP_START",    35  },
    { "MMC_ERASE_GROUP_END",      36  },
    { "MMC_ERASE",                38  },
    { "MMC_FAST_IO",              39  },
    { "MMC_GO_IRQ_STATE",         40  },
    { "MMC_LOCK_UNLOCK",          42  },
    { "MMC_APP_CMD",              55  },
    { "MMC_GEN_CMD",              56  },
    { "SD_APP_OP_COND",           41  },
    { "SD_APP_SEND_SCR",          51  },
    { "UNKNOWN"                   -1  }
};

static const char* get_command_name(int command)
{
    struct mmc_opcode* opcode = mmc_opcodes;

    while (opcode->cmd != command && opcode->cmd != -1) opcode++;
    return opcode->name;
}

static void goldfish_mmc_do_command(struct goldfish_mmc_state *s, uint32_t cmd, uint32_t arg)
{
    int result;
    int new_status = MMC_STAT_END_OF_CMD;
    int opcode = cmd & 63;

// fprintf(stderr, "goldfish_mmc_do_command opcode: %s (0x%04X), arg: %d\n", get_command_name(opcode), cmd, arg);

    s->resp[0] = 0;
    s->resp[1] = 0;
    s->resp[2] = 0;
    s->resp[3] = 0;

#define SET_R1_CURRENT_STATE(s)    ((s << 9) & 0x00001E00) /* sx, b (4 bits) */

    switch (opcode) {
        case MMC_SEND_CSD: {
            int64_t sector_count = 0;
            uint64_t capacity;
            uint8_t exponent;
            uint32_t m;

            bdrv_get_geometry(s->bs, &sector_count);
            capacity = sector_count * 512;
            if (capacity > 2147483648U) {
                // if storages is > 2 gig, then emulate SDHC card
                s->is_SDHC = 1;

                // CSD bits borrowed from a real SDHC card, with capacity bits zeroed out
                s->resp[3] = 0x400E0032;
                s->resp[2] = 0x5B590000;
                s->resp[1] = 0x00007F80;
                s->resp[0] = 0x0A4040DF;

                // stuff in the real capacity
                // m = UNSTUFF_BITS(resp, 48, 22);
                m = (uint32_t)(capacity / (512*1024)) - 1;
                // m must fit into 22 bits
                if (m & 0xFFC00000) {
                    fprintf(stderr, "SD card too big (%lld bytes).  Maximum SDHC card size is 128 gigabytes.\n", capacity);
                    abort();
                }

                // low 16 bits go in high end of resp[1]
                s->resp[1] |= ((m & 0x0000FFFF) << 16);
                // high 6 bits go in low end of resp[2]
                s->resp[2] |= (m >> 16);
            } else {
                // emulate standard SD card
                s->is_SDHC = 0;

                // CSD bits borrowed from a real SD card, with capacity bits zeroed out
                s->resp[3] = 0x00260032;
                s->resp[2] = 0x5F5A8000;
                s->resp[1] = 0x3EF84FFF;
                s->resp[0] = 0x928040CB;

                // stuff in the real capacity
                // e = UNSTUFF_BITS(resp, 47, 3);
                // m = UNSTUFF_BITS(resp, 62, 12);
                // csd->capacity = (1 + m) << (e + 2);
                // need to reverse the formula and calculate e and m
                exponent = 0;
                capacity = sector_count * 512;
                if (capacity > 2147483648U) {
                    fprintf(stderr, "SD card too big (%lld bytes).  Maximum SD card size is 2 gigabytes.\n", capacity);
                    abort();
                }
                capacity >>= 10; // convert to Kbytes
                while (capacity > 4096) {
                    // (capacity - 1) must fit into 12 bits
                    exponent++;
                    capacity >>= 1;
                }
                capacity -= 1;
                exponent -= 2;
                if (exponent > 7)
                    cpu_abort(cpu_single_env, "exponent %d too big\n", exponent);

                s->resp[2] |= (((uint32_t)capacity >> 2) & 0x3FF);  // high 10 bits to bottom of resp[2]
                s->resp[1] |= (((uint32_t)capacity & 3) << 30);    // low 2 bits to top of resp[1]
                s->resp[1] |= (exponent << (47 - 32));
            }
            break;
        }

        case MMC_SEND_EXT_CSD:
            s->resp[0] = arg;
            break;

        case MMC_APP_CMD:
            s->resp[0] = SET_R1_CURRENT_STATE(4) | R1_READY_FOR_DATA | R1_APP_CMD; //2336
            break;

        case SD_APP_OP_COND:
            s->resp[0] = 0x80FF8000;
            break;

        case SD_APP_SEND_SCR:
        {
            uint32_t* scr = (uint32_t*)s->buffer;
            scr[0] = 0x00002502;
            scr[1] = 0x00000000;
            s->resp[0] = SET_R1_CURRENT_STATE(4) | R1_READY_FOR_DATA | R1_APP_CMD; //2336
            new_status |= MMC_STAT_END_OF_DATA;
            break;
        }
        case MMC_SET_RELATIVE_ADDR:
            s->resp[0] = -518519520;
            break;

        case MMC_ALL_SEND_CID:
            s->resp[3] = 55788627;
            s->resp[2] = 1429221959;
            s->resp[1] = -2147479692;
            s->resp[0] = -436179883;
            break;

        case MMC_SELECT_CARD:
            s->resp[0] = SET_R1_CURRENT_STATE(3) | R1_READY_FOR_DATA; // 1792
            break;

         case MMC_SWITCH:
            if (arg == 0x00FFFFF1 || arg == 0x80FFFFF1) {
                uint8_t* switchbuf = s->buffer;
                memset(switchbuf, 0, 64);
                switchbuf[13] = 2;
                new_status |= MMC_STAT_END_OF_DATA;
            }
            s->resp[0] = SET_R1_CURRENT_STATE(4) | R1_READY_FOR_DATA | R1_APP_CMD; //2336
            break;

         case MMC_SET_BLOCKLEN:
            s->block_length = arg;
            s->resp[0] = SET_R1_CURRENT_STATE(4) | R1_READY_FOR_DATA; // 2304
            break;

        case MMC_READ_SINGLE_BLOCK:
            s->block_count = 1;
            // fall through
        case MMC_READ_MULTIPLE_BLOCK: {
            if (s->is_SDHC) {
                // arg is block offset
            } else {
                // arg is byte offset
                if (arg & 511) fprintf(stderr, "offset %d is not multiple of 512 when reading\n", arg);
                arg /= s->block_length;
            }
            result = bdrv_read(s->bs, arg, s->buffer, s->block_count);
            new_status |= MMC_STAT_END_OF_DATA;
            s->resp[0] = SET_R1_CURRENT_STATE(4) | R1_READY_FOR_DATA; // 2304
            break;
        }

        case MMC_WRITE_BLOCK:
            s->block_count = 1;
            // fall through
        case MMC_WRITE_MULTIPLE_BLOCK: {
            if (s->is_SDHC) {
                // arg is block offset
            } else {
                // arg is byte offset
                if (arg & 511) fprintf(stderr, "offset %d is not multiple of 512 when writing\n", arg);
                arg /= s->block_length;
            }
            // arg is byte offset
            result = bdrv_write(s->bs, arg, s->buffer, s->block_count);
//            bdrv_flush(s->bs);
            new_status |= MMC_STAT_END_OF_DATA;
            s->resp[0] = SET_R1_CURRENT_STATE(4) | R1_READY_FOR_DATA; // 2304
            break;
        }

        case MMC_STOP_TRANSMISSION:
            s->resp[0] = SET_R1_CURRENT_STATE(5) | R1_READY_FOR_DATA; // 2816
            break;

        case MMC_SEND_STATUS:
            s->resp[0] = SET_R1_CURRENT_STATE(4) | R1_READY_FOR_DATA; // 2304
            break;
     }

    s->int_status |= new_status;

    if ((s->int_status & s->int_enable)) {
        goldfish_device_set_irq(&s->dev, 0, (s->int_status & s->int_enable));
    }
}

static uint32_t goldfish_mmc_read(void *opaque, target_phys_addr_t offset)
{
    uint32_t ret;
    struct goldfish_mmc_state *s = opaque;

    offset -= s->dev.base;
    switch(offset) {
        case MMC_INT_STATUS:
            // return current buffer status flags
            return s->int_status & s->int_enable;
        case MMC_RESP_0:
            return s->resp[0];
        case MMC_RESP_1:
            return s->resp[1];
        case MMC_RESP_2:
            return s->resp[2];
        case MMC_RESP_3:
            return s->resp[3];
        case MMC_STATE: {
            ret = MMC_STATE_INSERTED;
            if (bdrv_is_read_only(s->bs)) {
                ret |= MMC_STATE_READ_ONLY;
            }
            return ret;
        }
        default:
            cpu_abort(cpu_single_env, "goldfish_mmc_read: Bad offset %x\n", offset);
            return 0;
    }
}

static void goldfish_mmc_write(void *opaque, target_phys_addr_t offset, uint32_t val)
{
    struct goldfish_mmc_state *s = opaque;
    int status, old_status;

    offset -= s->dev.base;

    switch(offset) {

        case MMC_INT_STATUS:
            status = s->int_status;
            old_status = status;
            status &= ~val;
            s->int_status = status;
            if(status != old_status) {
                goldfish_device_set_irq(&s->dev, 0, status);
            }
            break;

        case MMC_INT_ENABLE:
            /* enable buffer interrupts */
            s->int_enable = val;
            s->int_status = 0;
            goldfish_device_set_irq(&s->dev, 0, (s->int_status & s->int_enable));
            break;
        case MMC_SET_BUFFER:
            /* save pointer to buffer 1 */
            s->buffer = phys_ram_base + val;
            break;
        case MMC_CMD:
            goldfish_mmc_do_command(s, val, s->arg);
            break;
        case MMC_ARG:
            s->arg = val;
            break;
        case MMC_BLOCK_LENGTH:
            s->block_length = val + 1;
            break;
        case MMC_BLOCK_COUNT:
            s->block_count = val + 1;
            break;

        default:
            cpu_abort (cpu_single_env, "goldfish_mmc_write: Bad offset %x\n", offset);
    }
}

static CPUReadMemoryFunc *goldfish_mmc_readfn[] = {
   goldfish_mmc_read,
   goldfish_mmc_read,
   goldfish_mmc_read
};

static CPUWriteMemoryFunc *goldfish_mmc_writefn[] = {
   goldfish_mmc_write,
   goldfish_mmc_write,
   goldfish_mmc_write
};

void goldfish_mmc_init(uint32_t base, int id, BlockDriverState* bs)
{
    struct goldfish_mmc_state *s;

    s = (struct goldfish_mmc_state *)qemu_mallocz(sizeof(*s));
    s->dev.name = "goldfish_mmc";
    s->dev.id = id;
    s->dev.base = base;
    s->dev.size = 0x1000;
    s->dev.irq_count = 1;
    s->bs = bs;

    goldfish_device_add(&s->dev, goldfish_mmc_readfn, goldfish_mmc_writefn, s);

    register_savevm( "goldfish_mmc", 0, GOLDFISH_MMC_SAVE_VERSION,
                     goldfish_mmc_save, goldfish_mmc_load, s);
}

