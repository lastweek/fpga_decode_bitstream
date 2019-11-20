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

#define ARRAY_SIZE(x)		(sizeof(x) / sizeof((x)[0]))

#define NR_BYTES_OF_ICAP	(4)

#define REG_CRC		0
#define REG_FAR		1
#define REG_FDRI	2

/*
 * 30002001 write to FAR
 * 28006000 Read from FDRO
 */
int main(int argc, char **argv)
{
	char *fname, *str_nr_words_to_parse;
	int ret;
	int i, nr_words, val;
	FILE *fp;
	char *line;
	size_t len;
	ssize_t nread;

	bool p_idcode = false;

	if (argc != 3) {
		printf("Usage: ./parse bitstream.rba nr_icap_words\n");
		exit(-1);
	}

	fname = argv[1];
	str_nr_words_to_parse = argv[2];

	fp = fopen(fname, "r"); 
	if (!fp) {
		printf("Fail to open: %s\n", fname);
		exit(-1);
	}

	nr_words = atoi(str_nr_words_to_parse);
	printf("fname: %s nr_words: %d\n", fname, nr_words);

	if (nr_words == -1)
		nr_words = INT_MAX;

	len = 512;
	line = malloc(len);

	i = 0;
	while (i < nr_words) {
		nread = getline(&line, &len, fp);
		if (nread == -1)
			goto done;

		if (line[0] != '0' && line[0] != '1')
			continue;

		val = strtol(line, NULL, 2);

		//if (val != 0) {
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
