## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DFUSE_USE_VERSION=27 \
	-I. -Wall \
	$(VCACHEFS_CFLAGS)			

bin_PROGRAMS = vcachefs

AM_CFLAGS = -std=c99 -g -O0

vcachefs_LDADD = $(VCACHEFS_LIBS)

vcachefs_SOURCES = \
	vcachefs.c
