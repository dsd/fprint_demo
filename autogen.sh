#!/bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

pushd $srcdir

aclocal || exit 1
autoconf || exit 1
automake -a -c || exit 1

popd

if test -z "$NOCONFIGURE"; then
	$srcdir/configure --enable-maintainer-mode $*
fi
