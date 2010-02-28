#!/bin/sh

export PKG_CONFIG_PATH="/usr/lib/pkgconfig:/usr/local/lib/pkgconfig:/opt/local/lib/pkgconfig"

/opt/local/bin/aclocal -I /opt/local/share/aclocal
/opt/local/bin/glibtoolize --force --copy
/opt/local/bin/automake --add-missing --gnu --include-deps
/opt/local/bin/autoconf
/opt/local/bin/autoheader
./configure --enable-maintainer-mode $*
