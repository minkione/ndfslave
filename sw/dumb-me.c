#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#define XFAIL(p) do { if (p) { fprintf(stderr, "%s (%s:%d): operation failed: %s\n", __FUNCTION__, __FILE__, __LINE__, #p); abort(); } } while(0)

#define PAGESZ 8832L
#define KEYSZ 4096
#define NOFFSETS 64
#define NPGS_PER_ITER 64
#define MAXPGS 0x190000
#define VERBOSE

struct offset {
	int n;
	unsigned int page[NOFFSETS];
};

struct keyprob {
	int n;
	unsigned short probs[PAGESZ][0x100];
};

int main(int argc, char **argv) {
	int fd, i, j, nkey;
	int fdk;
	FILE *report;
	unsigned char *buf;
	static struct offset offsets[PAGESZ] = {{0}};
	static struct keyprob keyprob[NOFFSETS] = {{0}};
	unsigned int pn = 0;
	unsigned int pgmax;
	
	if (argc != 3 && argc != 4) {
		printf("usage: %s filename report [key]\n", argv[0]);
		return 1;
	}
	
	XFAIL((fd = open(argv[1], O_RDONLY)) < 0);
	pgmax = lseek64(fd, 0, SEEK_END) / PAGESZ;
	if (pgmax > MAXPGS)
		pgmax = MAXPGS;
	buf = mmap(NULL, pgmax * PAGESZ, PROT_READ, MAP_SHARED, fd, 0);
	XFAIL(buf == MAP_FAILED);
	
	XFAIL((report = fopen(argv[2], "w")) == NULL);
	
	fdk = -1;
	if (argc == 4)
		XFAIL((fdk = creat(argv[3], 0644)) < 0);
	
	printf("Analyzing %s...\n", argv[1]);
	fprintf(report, "Analysis of %s ('h%x pages)\n", argv[1], pgmax);
	fprintf(report, "No needle\n\n");
	fprintf(report, "\n\n");
	
	for (i = 0; i < pgmax; i++) {
		unsigned char *pstart = buf + PAGESZ * i;
		unsigned char *p = pstart;
		
		if ((i % 0x1000) == 0) {
			printf("0x%x / 0x%x...\r", i, pgmax);
			fflush(stdout);
		}
		
		/* If the whole page is the same-ish, this is really not it. */
		for (j = 0; j < 512; j++)
			if (pstart[0] != pstart[j])
				break;
		if (j == 512) /* Looks like we didn't find a difference in the first 512 bytes. */
			continue;

		keyprob[i % NOFFSETS].n++;

		for (j = 0; j < PAGESZ; j++)
			keyprob[i % NOFFSETS].probs[j][pstart[j]]++;
	}
	printf("\n");
	
	printf("Generating key probability statistics...\n");
	fprintf(report, "\n===== Key probability statistics =====\n\n");
	for (nkey = 0; nkey < NOFFSETS; nkey++) {
		/* Make sure we have enough to form an opinion with. */
		if (keyprob[nkey].n < 3)
		{
			if (fdk >= 0) {
				unsigned char cs[PAGESZ] = {0};
				write(fdk, cs, PAGESZ);
			}
			continue;
		}
		
		fprintf(report, "Probability at row 'h%02x:\n", nkey);
		for (i = 0; i < PAGESZ; i++) {
			int c;
			int bn;
			int tot = 0;
			
			unsigned char best[4] = {0};
			int nfound = 0;
			
			for (c = 0; c < 0x100; c++) {
				for (bn = 0; bn < sizeof(best); bn++)
					if ((keyprob[nkey].probs[i][c] > keyprob[nkey].probs[i][best[bn]]) || (bn >= nfound)) {
						memmove(best+bn+1, best+bn, sizeof(best) - bn - 1);
						best[bn] = c;
						nfound = bn + 1;
						break;
					}
				tot += keyprob[nkey].probs[i][c];
			}
			
			float confidence = keyprob[nkey].probs[i][best[0]] * 100.0f / (float)tot;
#ifdef VERBOSE			
			if (confidence > 95.0)
				fprintf(report, "%4d: 0x%02x (95%%+ confidence)\n", i, best[0]);
			else {
				fprintf(report, "%4d: ", i);
				for (bn = 0; bn < sizeof(best); bn++)
					fprintf(report, "  %6.2f%: 0x%02x  ", (float)keyprob[nkey].probs[i][best[bn]] * 100.0f / (float)tot, best[bn]);
				fprintf(report, "\n");
			}
#else
			fprintf(report, "0x%02x%c%c", best[0],
				confidence < 95.0 ? '?' : ' ',
				confidence < 90.0 ? '?' : ' ');
			if ((i % 8) == 7)
				fprintf(report, "  ");
			if ((i % 16) == 15)
				fprintf(report, "\n");
#endif
			if (fdk >= 0)
				write(fdk, best, 1);
		}
		fprintf(report, "\n");
	}
		
	fclose(report);
	
	return 0;
}
