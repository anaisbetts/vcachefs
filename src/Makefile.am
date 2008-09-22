## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DFUSE_USE_VERSION=27 \
	-I. -Wall \
	$(VCACHEFS_CFLAGS)			

bin_PROGRAMS = vcachefs

AM_CFLAGS = -std=c99 -g -O0

## HACK HACK HACK: We're adding libevent here, without ever actually checking for it!
vcachefs_LDADD = $(VCACHEFS_LIBS) -lgthread-2.0 -lpthread -levent

vcachefs_SOURCES = \
	vcachefs.c \
	stats.c
