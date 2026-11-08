#!/usr/bin/env bash
# build_clutter_x32.sh -- IN phorepoc-x86 (volume at /wk, repo :ro at /repo, this repo at /h, siblings at /glib /gtk /dbus): Clutter 1.26.4 (LGPL-2.1+) for x32 over Open C, on Cogl (build_cogl_x32.sh, run first) and
# GLES2/EGL: the stock EGL ("eglnative") backend with the null input backend (the WSERV window is handed to Cogl by os/src/cluttersym), json-glib 1.6.6 for ClutterScript.
# Out: /wk/clutter-x32/*.o, /wk/out/libclutter.a /wk/out/libjson-glib.a
set -eu
. /glib/recipes/pins.sh; . /gtk/recipes/pins.sh; . /h/recipes/pins.sh
CC=/wk/lsc/bin/pp-gcc
W=/wk/clutter-src; O=/wk/clutter-x32; mkdir -p $W $O /wk/out
[ -d $W/clutter-$CLUTTER_VER ] || tar xf /h/dl/clutter-$CLUTTER_VER.tar.xz -C $W
S=$W/clutter-$CLUTTER_VER; I=/wk/cogl-inc; CG=/wk/cairo-1.16.0/util/cairo-gobject
python3 /h/recipes/gen_clutter.py $S $O /wk/nat/gobject/glib-mkenums /wk/nat/gobject/glib-genmarshal
GLH=/repo/baseline/symbian-2010/oss.API_REF.Public_API/epoc32/include
X32="-I/wk/x32 -I/wk/x32/glib -I/wk/glib-2.56.4 -I/wk/glib-2.56.4/glib -I/wk/glib-2.56.4/gobject -I/wk/glib-2.56.4/gmodule -I/wk/glib-2.56.4/gio -I/wk/ffi-x32/include"
JS=$W/json-glib-$JSONGLIB_VER
[ -d $JS ] || tar xf /h/dl/json-glib-$JSONGLIB_VER.tar.xz -C $W
mkdir -p $O/inc; ln -sfn $S/clutter $O/inc/clutter; ln -sfn $JS/json-glib $O/inc/json-glib
FF="-O2 -g0 -w -fno-pic -I/glib/overlay -include /glib/overlay/phe_glib_compat.h -include /wk/gdk-x32/gtkcompat.h -include glib-object.h -include /h/overlay/phe_gl_compat.h -DHAVE_CONFIG_H -DCLUTTER_COMPILATION -DCAIRO_HAS_GOBJECT_FUNCTIONS=1 -DCLUTTER_DISABLE_DEPRECATION_WARNINGS -DG_LOG_DOMAIN=\"Clutter\" \
 -DCOGL_ENABLE_EXPERIMENTAL_API -DCOGL_ENABLE_EXPERIMENTAL_2_0_API -I$O -I$O/clutter -I$S -I$S/clutter -I$S/clutter/cogl -I$S/clutter/egl -I$S/clutter/cally -I$S/clutter/cally/.. -I$CG -I$O/inc -I$I -I$I/cogl -I$I/cogl-pango -I$I/cogl-path -I/h/overlay -I$GLH $X32 \
 -I/wk/cairo-1.16.0/src -I/wk/cairo-x32/src -I/wk/pixman-0.42.2/pixman -I/wk/pango-1.40.14 -I/wk/pango-x32 -I/wk/harfbuzz-$HARFBUZZ_VER/src -I/wk/fontconfig-2.14.2 -I/wk/freetype-2.13.3/include -I/wk/gdk-pixbuf-2.36.12 -I/wk/pixbuf-x32 -I/wk/atk-$ATK_VER"
{ ls $S/clutter/*.c | grep -v "clutter-x11\|clutter-gdk\|clutter-wayland\|clutter-win32\|clutter-osx\|clutter-mir\|clutter-evdev\|clutter-glx"; ls $S/clutter/deprecated/*.c; ls $S/clutter/cogl/*.c; ls $S/clutter/egl/*.c; ls $S/clutter/cally/*.c; ls $CG/*.c;
  echo $O/clutter/clutter-enum-types.c; echo $O/clutter/clutter-marshal.c; } > $O/sources.txt
rm -f $O/*.o $O/errs.*; export CC FF O
one() { f=$1; b=$(echo $f | tr / _ | sed 's/\.c$//'); $CC $FF -c $f -o $O/$b.o 2> $O/errs.$b || echo "FAIL $f"; }
export -f one
fails=$(xargs -P "${JOBS:-8}" -I{} bash -c 'one {}' < $O/sources.txt | tee $O/failed.txt | wc -l)
cat $O/errs.* > $O/errors.txt 2>/dev/null || true; rm -f $O/errs.*
echo "clutter: $(ls $O/*.o | wc -l) objects, $fails failed"; head -30 $O/failed.txt | tr '\n' ' '; echo
grep -a "error" $O/errors.txt | sed "s|$S/||" | cut -c1-200 | head -${ERRN:-25}
rm -f /wk/out/libclutter.a; ar rcs /wk/out/libclutter.a $O/*.o
# the feasibility test's C half, and the archives where the emitter finds them
$CC $FF -c /h/os/src/cluttertest/cluttertest_main.c -o $O/cluttertest_main.o 2> $O/errs.test || { grep -a error $O/errs.test | head; echo "TEST COMPILE FAIL"; }
rm -f /wk/out/libcluttertest.a; ar rcs /wk/out/libcluttertest.a $O/cluttertest_main.o
for a in clutter cogl cogl-pango cogl-path json-glib cluttertest; do cp /wk/out/lib$a.a /o/lib$a.a; done; echo "archives copied to the build dir"
