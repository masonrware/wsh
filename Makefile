ifndef TOOLPREFIX
TOOLPREFIX := $(shell if i386-jos-elf-objdump -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
then echo 'i386-jos-elf-'; \
elif objdump -i 2>&1 | grep 'elf32-i386' >/dev/null 2>&1; \
then echo ''; \
else echo "***" 1>&2; \
echo "*** Error: Couldn't find an i386-*-elf version of GCC/b    inutils." 1>&2; \
echo "*** Is the directory with i386-jos-elf-gcc in your PATH    ?" 1>&2; \
echo "*** If your i386-*-elf toolchain is installed with a co    mmand" 1>&2; \
echo "*** prefix other than 'i386-jos-elf-', set your TOOLPRE    FIX" 1>&2; \
echo "*** environment variable to that prefix and run 'make'     again." 1>&2; \
echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'    ." 1>&2; \
echo "***" 1>&2; exit 1; fi)
endif

CC = $(TOOLPREFIX)gcc
CFLAGS = -Wall -MD -ggdb -m32 -Werror -pedantic -std=gnu18
LOGIN = mware
SUBMITPATH = ~cs537-1/handin/$(LOGIN)/P3

all: wsh

wsh: wsh.c wsh.h
	$(CC) $(CFLAGS) wsh.c -o wsh

run: wsh
	./wsh

clean:
	rm wsh

pack:
	rm -rf /tmp/wsh
	mkdir -p /tmp/wsh
	(cd /tmp; tar cf - wsh) | gzip >$(LOGIN).tar.gz

submit: pack
	cp $(LOGIN).tar.gz $(SUBMITPATH)

.PHONY: all
