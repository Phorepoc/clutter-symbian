#!/usr/bin/env bash
# build_cogl_x32.sh -- IN phorepoc-x86 (volume at /wk, repo :ro at /repo, this repo at /h, siblings at /glib /gtk /dbus): Cogl 1.22.8 (LGPL-2.1+, the GPU layer of Clutter 1.26) for x32 over Open C: OpenGL ES 2.0
# through EGL (the "symbian" EGL platform: overlay/cogl-winsys-egl-symbian.c), GLib/GType/Pango support, no X11. From the pinned tarball (pins.sh); the generated files (config, defines, enum types) are
# written by recipes/gen_cogl.py; the windows come from backend/phe_clws.cpp -- see README.
# Out: /wk/cogl-x32/*.o, /wk/out/libcogl.a /wk/out/libcogl-pango.a /wk/out/libcogl-path.a
set -eu
. /glib/recipes/pins.sh; . /gtk/recipes/pins.sh; . /h/recipes/pins.sh
CC=/wk/lsc/bin/pp-gcc
W=/wk/clutter-src; O=/wk/cogl-x32; G=/wk/gtk+-$GTK_VER; mkdir -p $W $O /wk/out
[ -d $W/cogl-$COGL_VER ] || tar xf /h/dl/cogl-$COGL_VER.tar.xz -C $W
S=$W/cogl-$COGL_VER
# the Symbian EGL platform (overlay/cogl-winsys-egl-symbian.c: a WSERV window per onscreen through backend/phe_clws.h) is registered the way Cogl registers its platforms: a winsys id
# at the end of CoglWinsysID and its vtable getter in the renderer's list (the "null" platform is no longer built)
grep -q COGL_WINSYS_ID_EGL_SYMBIAN $S/cogl/cogl-renderer.h || sed -i 's|^  COGL_WINSYS_ID_SDL$|  COGL_WINSYS_ID_SDL,\n  COGL_WINSYS_ID_EGL_SYMBIAN|' $S/cogl/cogl-renderer.h
R=$S/cogl/cogl-renderer.c
grep -q egl_symbian $R || { sed -i '/^  _cogl_winsys_egl_null_get_vtable,$/{n;s|^#endif$|#endif\n#ifdef COGL_HAS_EGL_PLATFORM_SYMBIAN_SUPPORT\n  _cogl_winsys_egl_symbian_get_vtable,\n#endif|}' $R;
  sed -i 's|^#include "cogl-winsys-stub-private.h"$|&\nconst CoglWinsysVtable *_cogl_winsys_egl_symbian_get_vtable (void);|' $R; }
echo "cogl: symbian platform registered ($(grep -c egl_symbian $R) lines)"
python3 /h/recipes/gen_cogl.py $S $O /wk/nat/gobject/glib-mkenums
GLH=/repo/baseline/symbian-2010/oss.API_REF.Public_API/epoc32/include
X32="-I/wk/x32 -I/wk/x32/glib -I/wk/glib-2.56.4 -I/wk/glib-2.56.4/glib -I/wk/glib-2.56.4/gobject -I/wk/glib-2.56.4/gmodule -I/wk/glib-2.56.4/gio -I/wk/ffi-x32/include"
FF="-O2 -g0 -w -fno-pic -I/glib/overlay -include /glib/overlay/phe_glib_compat.h -include /wk/gdk-x32/gtkcompat.h -include /h/overlay/phe_gl_compat.h -DHAVE_CONFIG_H -DCOGL_COMPILATION -DG_LOG_DOMAIN=\"Cogl\" -DCOGL_ENABLE_EXPERIMENTAL_API -DCOGL_ENABLE_EXPERIMENTAL_2_0_API \
 -I$O -I$O/cogl -I$S -I$S/cogl -I$S/cogl/winsys -I$S/cogl/deprecated -I$S/cogl/driver/gl -I$GLH -I/h/overlay -I/h/backend $X32 -I/wk/cairo-1.16.0/src -I/wk/cairo-x32/src -I/wk/pixman-0.42.2/pixman -I/wk/pango-1.40.14 -I/wk/pango-x32 \
 -I/wk/harfbuzz-$HARFBUZZ_VER/src -I/wk/fontconfig-2.14.2 -I/wk/freetype-2.13.3/include -I/wk/gdk-pixbuf-2.36.12 -I/wk/pixbuf-x32"
# the translation units: Cogl proper (no SDL / Xlib / GLX / WGL / Wayland / KMS), the deprecated API, the GL driver files and the GLES variants, the EGL winsys and its null platform
{ ls $S/cogl/*.c | grep -v "cogl-sdl\|cogl-xlib\|cogl-x11\|cogl-wayland\|cogl-kms\|cogl-mir\|cogl-glx\|cogl-wgl\|cogl-win32"; ls $S/cogl/deprecated/*.c | grep -v "xlib\|x11";
  ls $S/cogl/driver/nop/*.c; echo $S/cogl/winsys/cogl-winsys-stub.c; ls $S/cogl/driver/gl/*.c; ls $S/cogl/driver/gl/gles/*.c; echo $S/cogl/winsys/cogl-winsys.c; echo $S/cogl/winsys/cogl-winsys-egl.c; echo /h/overlay/cogl-winsys-egl-symbian.c; echo $O/cogl/cogl-enum-types.c; } > $O/sources.txt
rm -f $O/*.o $O/errs.*; export CC FF O
one() { f=$1; b=$(echo $f | tr / _ | sed 's/\.c$//'); $CC $FF -c $f -o $O/$b.o 2> $O/errs.$b || echo "FAIL $f"; }
export -f one
fails=$(xargs -P "${JOBS:-8}" -I{} bash -c 'one {}' < $O/sources.txt | tee $O/failed.txt | wc -l)
cat $O/errs.* > $O/errors.txt 2>/dev/null || true; rm -f $O/errs.*
echo "cogl: $(ls $O/*.o | wc -l) objects, $fails failed"; head -30 $O/failed.txt | tr '\n' ' '; echo
grep -a "error" $O/errors.txt | sed "s|$S/||" | cut -c1-200 | head -${ERRN:-25}
rm -f /wk/out/libcogl.a; ar rcs /wk/out/libcogl.a $O/*.o
# the include tree Clutter and the other layers see: <cogl/cogl.h>, <cogl-pango/cogl-pango.h>, <cogl-path/cogl-path.h> (headers of the tarball + the generated ones)
I=/wk/cogl-inc; rm -rf $I; mkdir -p $I/cogl $I/cogl-pango $I/cogl-path
(cd $S/cogl && find . -name "*.h" -exec cp --parents {} $I/cogl \;); cp $O/cogl/*.h $I/cogl/; cp $S/cogl-pango/*.h $I/cogl-pango/; cp $S/cogl-path/*.h $I/cogl-path/; cp $O/cogl-path/*.h $I/cogl-path/
FFX="$FF -I$I -I$S/cogl-pango -I$S/cogl-path -I$S/cogl-path/tesselator -I$O/cogl-path"   # cogl-path/tesselator = the SGI libtess (the GLU tessellator, SGI Free Software License B) Cogl carries: gluNewTess ... compiled in, no stubs
for d in cogl-pango cogl-path; do
  for f in $S/$d/*.c $( [ $d = cogl-path ] && echo $O/cogl-path/cogl-path-enum-types.c $S/cogl-path/tesselator/*.c ); do b=$d-$(basename $f .c); $CC $FFX -c $f -o $O/x_$b.o 2> $O/errs.$b || echo "FAIL $f"; done
  rm -f /wk/out/lib$d.a; ar rcs /wk/out/lib$d.a $O/x_$d-*.o; echo "lib$d.a: $(ar t /wk/out/lib$d.a | wc -l) members"
done
cat $O/errs.* 2>/dev/null | grep -a "error" | cut -c1-200 | head -10
echo "cogl headers for clutter: $I"
