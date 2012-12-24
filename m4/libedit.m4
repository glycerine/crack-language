# Copyright 2009-2011 Google Inc.
# Copyright 2011 Shannon Weyrick <weyrick@mozek.us>
# 
#   This Source Code Form is subject to the terms of the Mozilla Public
#   License, v. 2.0. If a copy of the MPL was not distributed with this
#   file, You can obtain one at http://mozilla.org/MPL/2.0/.
# 
# convert LLVM config options to macros usable from autoconf
# "jit" and "native" are required for jitting, 
# 'instrumentation' is required for profiling support
# 'ipo' is required for optimizations
# 'linker', 'native', 'bitwriter' are for native binary support

AC_DEFUN([AM_LIBEDIT],
[
    LIBEDIT_CFLAGS=`pkg-config --cflags libedit`
    LIBEDIT_LDFLAGS=`pkg-config --libs-only-L libedit`
    LIBEDIT_LIBS=`pkg-config --libs-only-l libedit`
    AC_SUBST(LIBEDIT_CFLAGS)
    AC_SUBST(LIBEDIT_LDFLAGS)
    AC_SUBST(LIBEDIT_LIBS)
])
