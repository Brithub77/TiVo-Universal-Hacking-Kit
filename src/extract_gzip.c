/*
 * extract-gzip.c
 * QnD. I needed to extract the initrd image from a kernel.
 * What else can I say....
 *
 *
 * Created by Steve White on Fri Nov 15 2002
 * Copyright (c) 2002 Steve White. All rights reserved.
 *
 * Hacked by Pent Thu Feb 6, 2003
 */

#include <stdio.h>

int main(int argc, char *argv[]) {
  FILE *kernel;
  FILE *output;
  FILE *stub;

  int ch, found = 0;

  if (argc != 4) {
	printf("usage: %s <input file> <output file> <stub file>\n", argv[0]);
	printf(" i.e: %s vmlinux.px initrd.img.gz kernel.stub\n", argv[0]);
	exit(1);
  }

  kernel = fopen(argv[1], "rb");
  if (kernel == NULL) {
	printf("error opening %s\n", argv[1]);
	exit(1);
  }

  stub = fopen(argv[3], "wb");

  while (1) {
	ch = fgetc(kernel);
	if (ch == EOF)
	  break;
	if (!found) {
	  if (ch == 0x1f) {
		ch = fgetc(kernel);

		if (ch == EOF){
		  fputc(0x1f, stub);
		  break;
		}
		if (ch == 0x8b) {
		  found = 1;
		  output = fopen(argv[2], "wb");
		  fputc(0x1f, output);
		  fputc(0x8b, output);
		  printf("BinkI!"); 
		}
		if (!found) fputc(0x1f, stub);
	  }
	  if (!found) fputc(ch, stub);
	} else {
	  fputc(ch, output);
	}
  }

  fclose(kernel);
  fclose(stub);
  if (found)
	fclose(output);
  else
	printf("Didn't find anything!\n");
}
