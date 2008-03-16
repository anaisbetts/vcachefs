#!/bin/sh

aclocal
libtoolize --force --copy
automake -a
autoconf
./configure --enable-maintainer-mode $*
