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
	int i, nr_words, val, *val_ptr;
	int fp;
	char *line;
	size_t len;
	ssize_t nread;
	struct stat fs_stat;

	bool p_idcode = false;

	if (argc != 3) {
		printf("Usage: ./parse bitstream.msk nr_icap_words\n");
		exit(-1);
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

	i = 0;
	nr_words /= 4;

	/*
	 * we need to have 2B shifts
	 */
	val_ptr = line + 2;

	while (i < nr_words) {
		val = *val_ptr++;
		val = htonl(val);
		if (1) {
			printf("[%10d] %08x ", i, val);

			if (val == 0xaa995566)
				printf(" SYNC\n");
			else if (val == 0x000000BB)
				printf("Bus Width Sync\n");
			else if (val == 0x30002001)
				printf("Write to FAR\n");
			else if (val == 0x28006000)
				printf("Read from FDRO\n");
			else if (val == 0x30000001)
				printf("Write to CRC\n");
			else if (val == 0x30018001) {
				printf("Write to IDCODE\n");
				p_idcode = true;
			} else if (val == 0x11220044)
				printf("Bus Width Detect\n");
			else if ((val & 0xf0000000) == 0x30000000) {
				int regs;

				regs = val & 0x0003E000;
				regs = regs >> 13;
				printf("Write to regs %d\n", regs);
			} else if (p_idcode) {
				printf("IDCODE=%x\n", val & 0x0FFFFFFF);
				p_idcode = false;
			} else
				printf("\n");
		}
		i++;
	}

done:
	return 0;
}
