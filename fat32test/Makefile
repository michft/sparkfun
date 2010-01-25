all: setforlinux testfat

CFLAGS=-g -O2 -Wall $(DFLAGS)
CC=gcc

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

MAP=-Wl,"-Map" -Wl,"$@.map" -Wl,"--cref"

setforlinux:
	make -f Makefile clean
	touch setforlinux
	rm -f setforavr

clean:
	rm -f *.hex *.eep *.elf *.lss *.sym *.dsm *.o *.a *.d

mount.o fat32data.o opendir.o creat.o deltrunc.o write.o read.o seek.o: fat32.h fat32private.h

linuxmain.o: fat32.h sdhc.h

liblinfat32.a: mount.o fat32data.o opendir.o creat.o deltrunc.o write.o read.o seek.o
	ar r $@ $^

testfat: main.o dosmunge.o sdhcemu.o liblinfat32.a
	$(CC) -o $@ dosmunge.o main.o sdhcemu.o -L. -llinfat32
