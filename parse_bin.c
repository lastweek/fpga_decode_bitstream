/*
 * Copyright (c) 2019. Yizhou Shan. All rights reserved.
 * Contact: syzwhat@gmail.com
 *
 * This file can parse the ultrascale+ bitstream files.
 */
#include <arpa/inet.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>

#define ARRAY_SIZE(x)		(sizeof(x) / sizeof((x)[0]))

#define NR_BYTES_OF_ICAP	(4)

#define REG_CRC		0
#define REG_FAR		1
#define REG_FDRI	2

enum OPS {
    OP_NOP = 0,
    OP_R = 1,
    OP_W = 2,
    OP_RW = 3,
};

struct bitstream_op {
    void (*init)(uint32_t val);
    void (*hndl)(uint32_t val);
    const char *name;
    enum OPS allowed_ops;
    int address;
    const char *desc;
};

static void dummy_init(uint32_t val)
{
    (void)val;
}
static void dummy_hndl(uint32_t val)
{
    (void)val;
}
static void init_fdri(uint32_t val);
static void hndl_cmd(uint32_t val);
static void hndl_mask(uint32_t val);
static void hndl_bspi(uint32_t val);

static const struct bitstream_op dummy_op_ = { dummy_init, dummy_hndl, "Dummy", OP_NOP, 0xffff, "Dummy" };
static const struct bitstream_op *dummy_op = &dummy_op_;
static const struct bitstream_op bitstream_ops[] = {
    { dummy_init, dummy_hndl, "CRC", OP_RW, 0b00000, "CRC Register" },
    { dummy_init, dummy_hndl, "FAR", OP_RW, 0b00001, "Frame Address Register" },
    { init_fdri,  dummy_hndl, "FDRI", OP_W, 0b00010, "Frame Data Register, Input Register (write configuration data)" },
    { dummy_init, dummy_hndl, "FDRO", OP_R, 0b00011, "Frame Data Register, Output Register (read configuration data)" },
    { dummy_init, hndl_cmd,   "CMD", OP_RW, 0b00100, "Command Register" },
    { dummy_init, dummy_hndl, "CTL0", OP_RW, 0b00101, "Control Register 0" },
    { dummy_init, hndl_mask,  "MASK", OP_RW, 0b00110, "Masking Register for CTL0 and CTL1" },
    { dummy_init, dummy_hndl, "STAT", OP_R, 0b00111, "Status Register" },
    { dummy_init, dummy_hndl, "LOUT", OP_W, 0b01000, "Legacy Output Register for daisy chain" },
    { dummy_init, dummy_hndl, "COR0", OP_RW, 0b01001, "Configuration Option Register 0" },
    { dummy_init, dummy_hndl, "MFWR", OP_W, 0b01010, "Multiple Frame Write Register" },
    { dummy_init, dummy_hndl, "CBC", OP_W, 0b01011, "Initial CBC Value Register" },
    { dummy_init, dummy_hndl, "IDCODE", OP_RW, 0b01100, "Device ID Register" },
    { dummy_init, dummy_hndl, "AXSS", OP_RW, 0b01101, "User Access Register" },
    { dummy_init, dummy_hndl, "COR1", OP_RW, 0b01110, "Configuration Option Register 1" },
    { dummy_init, dummy_hndl, "WBSTAR", OP_RW, 0b10000, "Warm Boot Start Address Register" },
    { dummy_init, dummy_hndl, "TIMER", OP_RW, 0b10001, "Watchdog Timer Register" },
    /** CRC Register is not documented in UG470, but mentioned in Table 5-19 "Sample XC7K325T Bitstream" */
    { dummy_init, dummy_hndl, "CRC", OP_RW, 0b10011, "CRC Register" },
    { dummy_init, dummy_hndl, "BOOTSTS", OP_R, 0b10110, "Boot History Status Register" },
    { dummy_init, dummy_hndl, "CTL1", OP_RW, 0b11000, "Control Register 1" },
    { dummy_init, hndl_bspi,  "BSPI", OP_RW, 0b11111, "BPI/SPI Configuration Options Register" },
};

static struct {
    uint32_t mask;
    int num;
} globals;

static void init_fdri(uint32_t val)
{
    if (val == 0x30004000) {
        globals.num = 121;
    }
}

static void hndl_mask(uint32_t val)
{
    globals.mask = val;
}

static void hndl_bspi(uint32_t val)
{
    uint32_t opcode = val & 0xff;
    uint32_t spi_buswidth = (val & 0x300) >> 8;

    printf("Buswidth: %d | Opcode: ", spi_buswidth);
    struct {
        const char *instruction;
        uint32_t opcode;
    } spi_instructions[] = {
        { "Fast Read x1", 0x0B },
        { "Dual Output Fast Read", 0x3B },
        { "Quad Output Fast Read", 0x6B },
        { "Fast Read, 32-bit address", 0x0C },
        { "Dual Output Fast Read, 32-bit address", 0x3C },
        { "Quad Output Fast Read, 32-bit address", 0x6C },
    };

    for (size_t n = 0; n < sizeof(spi_instructions) / sizeof(spi_instructions[0]); ++n) {
        if (opcode == spi_instructions[n].opcode) {
            printf("%s", spi_instructions[n].instruction);
            return;
        }
    }

    printf("Unknown opcode 0x%02x          <==", opcode);
}

static void hndl_cmd(uint32_t val)
{
    struct cmd_val {
        const char *name;
        uint32_t val;
        const char *desc;
    };
    struct cmd_val cmd_vals[] = {
        { "NULL", 0b00000, "Null command, does nothing." },
        { "WCFG", 0b00001, "Writes Configuration Data: used prior to writing configuration data to the FDRI." },
        {
            "MFW", 0b00010, "Multiple Frame Write: used to perform a write of a single frame"
            " data to multiple frame addresses."
        },
        {
            "DGHIGH/LFRM", 0b00011, "Last Frame: Deasserts the GHIGH_B signal, activating all"
            " interconnects. The GHIGH_B signal is asserted with the AGHIGH"
            " command."
        },
        {
            "RCFG", 0b00100, "Reads Configuration Data: used prior to reading configuration data"
            " from the FDRO."
        },
        {
            "START", 0b00101, "Begins the Startup Sequence: The startup sequence begins after a"
            " successful CRC check and a DESYNC command are performed."
        },
        {
            "RCAP", 0b00110, "Resets the CAPTURE signal after performing readback-capture in"
            " single-shot mode."
        },
        { "RCRC", 0b00111, "Resets CRC: Resets the CRC register." },
        {
            "AGHIGH", 0b01000, "Asserts the GHIGH_B signal: places all interconnect in a High-Z"
            " state to prevent contention when writing new configuration data."
            " This command is only used in shutdown reconfiguration."
            " Interconnect is reactivated with the LFRM command."
        },
        {
            "SWITCH", 0b01001, "Switches the CCLK frequency: updates the frequency of the master"
            " CCLK to the value specified by the OSCFSEL bits in the COR0"
            " register."
        },
        {
            "GRESTORE", 0b01010, "Pulses the GRESTORE signal: sets/resets (depending on user"
            " configuration) IOB and CLB flip-flops."
        },
        {
            "SHUTDOWN", 0b01011, "Begin Shutdown Sequence: Initiates the shutdown sequence,"
            " disabling the device when finished. Shutdown activates on the next"
            " successful CRC check or RCRC instruction (typically an RCRC"
            " instruction)."
        },
        {
            "GCAPTURE", 0b01100, "Pulses GCAPTURE: Loads the capture cells with the current"
            " register states."
        },
        {
            "DESYNC", 0b01101, "Resets the DALIGN signal: Used at the end of configuration to"
            " desynchronize the device. After desynchronization, all values on"
            " the configuration data pins are ignored."
        },
        { "Reserved", 0b01110, "Reserved." },
        { "IPROG", 0b01111, "Internal PROG for triggering a warm boot." },
        {
            "CRCC", 0b10000, "When readback CRC is selected, the configuration logic recalculates"
            " the first readback CRC value after reconfiguration. Toggling"
            " GHIGH has the same effect. This command can be used when"
            " GHIGH is not toggled during the reconfiguration case."
        },
        { "LTIMER", 0b10001, "Reload Watchdog timer." },
        { "BSPI_READ", 0b10010, "BPI/SPI re-initiate bitstream read." },
        {
            "FALL_EDGE", 0b10011, "Switch to negative-edge clocking (configuration data capture on"
            " falling edge)."
        },
    };

    for (size_t n = 0; n < sizeof(cmd_vals) / sizeof(cmd_vals[0]); ++n) {
        if (cmd_vals[n].val == val) {
            printf("%s", cmd_vals[n].name);
            return;
        }
    }

    printf("Unknown CMD 0x%02x            <==", val);
}

/*
 * 30002001 write to FAR
 * 28006000 Read from FDRO
 */

/*
 * .msk file is a binary file.
 * it has some text headers and its not 4B aligned.
 */
int main(int argc, char **argv)
{
    char *fname, *str_nr_words_to_parse;
    int ret;
    int i, nr_words;
    uint32_t val, prev, *val_ptr;
    int fp;
    uint8_t *line;
    size_t len, ffs;
    ssize_t nread;
    struct stat fs_stat;
    const struct bitstream_op *last_op;

    bool p_idcode = false;
    int nr_bytes_to_skip;

    if (argc != 3 && argc != 4) {
        printf("Usage: ./parse_bin.o binary_file nr_words_to_parse [nr_bytes_to_skip]\n"
               "Examples:\n"
               "       ./parse_bin.o foo.msk -1          Parse the whole file\n"
               "       ./parse_bin.o foo.msk -1 120       Parse the whole file, skip the first 120 bytes.\n"
              );
        return 0;
    }

    fname = argv[1];
    str_nr_words_to_parse = argv[2];

    fp = open(fname, O_RDONLY);

    if (fp < 0) {
        printf("Fail to open: %s\n", fname);
        exit(-1);
    }

    nr_words = atoi(str_nr_words_to_parse);

    if (nr_words == -1) {
        stat(fname, &fs_stat);
        nr_words = fs_stat.st_size;
    }

    line = mmap(NULL, nr_words, PROT_READ, MAP_PRIVATE | MAP_FILE, fp, 0);

    if (line == MAP_FAILED) {
        printf("Fail to mmap\n");
        return 0;
    }

    if (argc == 4) {
        nr_bytes_to_skip = atoi(argv[3]);
    }
    else {
        nr_bytes_to_skip = 0;
    }

    printf("File Name: %s nr_bytes_to_skip: %d\n", fname, nr_bytes_to_skip);

    /*
     * If the result does not look good.
     * use hexdump to check file first.
     * Check where does the file header ASCII stuff ends.
     * You need to apply that shift here.
     */
    val_ptr = (uint32_t *)(line + nr_bytes_to_skip);

    i = 0;
    nr_words /= 4;

    uint8_t state = 0;
    bool invert = false;

    prev = 0;

    while (i < nr_words) {
        val = *val_ptr++;
        val = htonl(val);

        void reverse(int *val) {
            /* https://stackoverflow.com/a/2602885 */
            uint8_t reverse_byte(uint8_t b) {
                b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
                b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
                b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
                return b;
            }
            uint8_t *p_val = (void *)val;

            for (size_t n = 0; n < 4; ++n) {
                *p_val = reverse_byte(*p_val);
                p_val++;
            }
        }

        if (state == 0x3) {
            reverse(&val);
        }

        if (1) {
            if (val != prev && prev == 0xffffffff) {
                if (ffs > 2) {
                    printf("[...]\n[%10d] ffffffff -\n", i - 1);
                }

                ffs = 0;
            }

            if (val == 0xffffffff) {
                ffs++;

                if (val == prev) {
                    i++;
                    continue;
                }
            }

            prev = val;
            printf("[%10d] %08x ", i, val);
            //			printf("%08x ", val);
retry_once:

            /*
             * Don't bother.
             * Just keep it ugly.
             */
            if (globals.num) {
                last_op->hndl(val);
                globals.num--;

                if (globals.num == 0) {
                    last_op = dummy_op;
                }

                printf("\n");
            }
            else if ((val & 0xf0000000) == 0x30000000) {
                int regs = val & 0x0003E000;
                regs = regs >> 13;
                globals.num = val & 0x7ff;
            const struct bitstream_op *print_message(int regs) {
                    for (size_t n = 0; n < sizeof(bitstream_ops) / sizeof(bitstream_ops[0]); ++n) {
                        if (bitstream_ops[n].address == regs) {
                            bitstream_ops[n].init(val);
                            printf("Write %d words to %s (next will be %d)\n", globals.num, bitstream_ops[n].name, globals.num + i + 1);
                            return &bitstream_ops[n];
                        }
                    }

                    printf("Write %d words to reg %d        <==\n", globals.num, regs);
                    return dummy_op;
                }

                last_op = print_message(regs);
            }
            else if (val == 0x000000BB) {
                if (state == 0) {
                    printf("Bus Width Sync\n");
                }
                else {
                    printf("Bus Width Sync - Detected bit-swapped encoding, using that from now on!\n");
                }

                state |= 0x2;
            }
            else if (val == 0x11220044) {
                printf("Bus Width Detect\n");
            }
            else if (val == 0xaa995566) {
                printf(" SYNC\n");
            }
            else if (val == 0x20000000) {
                printf("NOP\n");
            }
            else if (val == 0x30002001) {
                printf("Write to FAR\n");
            }
            else if (val == 0x28006000) {
                printf("Read from FDRO\n");
            }
            else if (val == 0x30000001) {
                printf("Write to CRC\n");
            }
            else if (val == 0x30018001) {
                printf("Write to IDCODE\n");
                p_idcode = true;
            }
            else if (val == 0x30004000) {
                printf("Write to FDRI\n");
            }
            else if (val == 0x30008001) {
                printf("Write to CMD\n");
            }
            else if (p_idcode) {
                printf("IDCODE=%x\n", val & 0x0FFFFFFF);
                p_idcode = false;
            }
            else {
                if (state == 0 && val != 0xffffffff) {
                    state = 0x1;
                    reverse(&val);
                    goto retry_once;
                }

                printf("-\n");
            }
        }

        i++;
    }

done:
    return 0;
}
