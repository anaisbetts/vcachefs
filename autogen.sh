#!/bin/sh

aclocal
libtoolize --force --copy
automake -a
autoconf
autoheader
./configure --enable-maintainer-mode $*
