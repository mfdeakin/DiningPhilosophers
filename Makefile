
CC=gcc
CFLAGS=-g -Wall -std=gnu1x
OBJCOPY=objcopy
OLDSYMNAME=_binary_usage_txt_start
NEWSYMNAME=semusage
OBJCOPYFLAGS=-I binary -O elf64-x86-64 -B i386 --redefine-sym $(OLDSYMNAME)=$(NEWSYMNAME)

all: dining

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

dining: dining.o
	$(CC) $(CFLAGS) -o $@ $<