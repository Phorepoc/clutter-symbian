#!/usr/bin/env bash
# build_jsonglib_x32.sh -- IN phorepoc-x86 (volume at /wk, repo :ro at /repo, this repo at /h, siblings at /glib /gtk): json-glib 1.6.6 (LGPL-2.1+; ClutterScript needs it) for x32 over Open C, from the pinned tarball:
# the meson build's generated files (json-version.h, json-enum-types.[ch]) are made here, the sources are the ones json-glib/meson.build lists.  Out: /wk/jsonglib-x32/*.o, /wk/out/libjson-glib.a
set -eu
. /glib/recipes/pins.sh; . /h/recipes/pins.sh
CC=/wk/lsc/bin/pp-gcc
W=/wk/clutter-src; O=/wk/jsonglib-x32; mkdir -p $W $O /wk/out
[ -d $W/json-glib-$JSONGLIB_VER ] || tar xf /h/dl/json-glib-$JSONGLIB_VER.tar.xz -C $W
S=$W/json-glib-$JSONGLIB_VER/json-glib
IFS=. read MA MI MU <<< "$JSONGLIB_VER"
sed "s/@JSON_MAJOR_VERSION@/$MA/; s/@JSON_MINOR_VERSION@/$MI/; s/@JSON_MICRO_VERSION@/$MU/; s/@JSON_VERSION@/$JSONGLIB_VER/" $S/json-version.h.in > $S/json-version.h
HDRS="json-builder.h json-generator.h json-gobject.h json-gvariant.h json-parser.h json-path.h json-reader.h json-types.h json-utils.h json-version-macros.h"
(cd $S && /wk/nat/gobject/glib-mkenums --template json-enum-types.h.in $HDRS > $O/json-enum-types.h && /wk/nat/gobject/glib-mkenums --template json-enum-types.c.in $HDRS > $O/json-enum-types.c)
echo '#define GETTEXT_PACKAGE "json-glib-1.0"
#define JSON_LOCALEDIR "z:/resource/locale"
#define HAVE_UNISTD_H 1' > $O/config.h
X32="-I/wk/x32 -I/wk/x32/glib -I/wk/glib-2.56.4 -I/wk/glib-2.56.4/glib -I/wk/glib-2.56.4/gobject -I/wk/glib-2.56.4/gmodule -I/wk/glib-2.56.4/gio -I/wk/ffi-x32/include"
FF="-O2 -g0 -w -fno-pic -I/glib/overlay -include /glib/overlay/phe_glib_compat.h -include /wk/gdk-x32/gtkcompat.h -DHAVE_CONFIG_H -DJSON_COMPILATION -DG_LOG_DOMAIN=\"Json\" -I$O -I$S -I$S/.. $X32"
rm -f $O/*.o $O/errs.*
for f in json-array json-builder json-debug json-gboxed json-generator json-gobject json-gvariant json-node json-object json-parser json-path json-reader json-scanner json-serializable json-utils json-value; do
  $CC $FF -c $S/$f.c -o $O/$f.o 2> $O/errs.$f || echo "FAIL $f"
done
$CC $FF -c $O/json-enum-types.c -o $O/json-enum-types.o 2> $O/errs.enum || echo "FAIL enum"
cat $O/errs.* | grep -a "error" | cut -c1-200 | head -10
rm -f /wk/out/libjson-glib.a; ar rcs /wk/out/libjson-glib.a $O/*.o; echo "libjson-glib.a: $(ar t /wk/out/libjson-glib.a | wc -l) members"
# the include tree: <json-glib/json-glib.h>
mkdir -p /wk/clutter-x32/inc 2>/dev/null; ln -sfn $S /wk/clutter-x32/inc/json-glib; cp $O/json-enum-types.h $S/json-enum-types.h
