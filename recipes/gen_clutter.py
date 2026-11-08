# gen_clutter.py SRC OUT MKENUMS GENMARSHAL -- what Clutter 1.26.4's configure and make generate, for the configuration of this port: the EGL ("eglnative") backend over Cogl's GLES2/EGL null platform, the
# null input backend, no X11 / GDK / Wayland / evdev.  OUT/config.h, OUT/clutter/clutter-config.h, clutter-enum-types.[ch], clutter-marshal.[ch]
import glob
import os
import re
import subprocess
import sys

src, out, mkenums, genmarshal = sys.argv[1:5]
os.makedirs(out + '/clutter', exist_ok=True)
open(out + '/config.h', 'w').write('''#define GETTEXT_PACKAGE "clutter-1.0"
#define CLUTTER_LOCALEDIR "z:/resource/locale"
#define HAVE_COGL_GLES2 1
#define HAVE_CLUTTER_EGL 1
#define COGL_HAS_EGL_SUPPORT 1
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_POLL_H 1
#define HAVE_MEMMEM 0
#define CLUTTER_ENABLE_DEBUG 0
#define CLUTTER_SYSCONFDIR "z:/resource/clutter"
#define CLUTTER_DRIVERS "gles2"
''')
t = open(src + '/clutter/clutter-config.h.in').read().replace('@CLUTTER_CONFIG_DEFINES@', '#define CLUTTER_WINDOWING_EGL "eglnative"\n#define CLUTTER_INPUT_NULL "null"')
open(out + '/clutter/clutter-config.h', 'w').write(t)
skip = re.compile(r'(-private\.h$|clutter-config|clutter-enum-types|clutter-marshal|clutter-x11|clutter-gdk|clutter-wayland|clutter-win32|clutter-osx|clutter-mir|clutter-evdev|clutter-egl|clutter-glx|clutter-cex|clutter-version|clutter-stage-cogl|clutter-backend-)')
hdrs = sorted(os.path.basename(h) for h in glob.glob(src + '/clutter/*.h') if not skip.search(os.path.basename(h)))
hdrs += ['deprecated/' + os.path.basename(h) for h in sorted(glob.glob(src + '/clutter/deprecated/*.h')) if not skip.search(os.path.basename(h))]
for kind in ('h', 'c'):
    r = subprocess.run([mkenums, '--template', src + '/clutter/clutter-enum-types.%s.in' % kind] + hdrs, cwd=src + '/clutter', capture_output=True, text=True, check=True)
    open(out + '/clutter/clutter-enum-types.' + kind, 'w').write(r.stdout)
for kind, flag in (('h', '--header'), ('c', '--body')):
    extra = ['--prefix=_clutter_marshal']
    r = subprocess.run([genmarshal] + extra + [flag, src + '/clutter/clutter-marshal.list'], capture_output=True, text=True, check=True)
    body = r.stdout
    if kind == 'c':
        body = '#include "config.h"\n#include "clutter-marshal.h"\n' + body
    open(out + '/clutter/clutter-marshal.' + kind, 'w').write(body)
print('clutter generated: %d headers for the enum types' % len(hdrs))
