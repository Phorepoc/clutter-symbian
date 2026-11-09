# gen_cogl.py SRC OUT -- the files Cogl 1.22.8's configure and make generate, written by hand for the one configuration this port builds: OpenGL ES 2.0 over EGL, the "symbian" EGL platform
# (overlay/cogl-winsys-egl-symbian.c: a WSERV window per onscreen), GLib / GType / Pango support, no X11, no SDL, no Wayland, no KMS.
#   OUT/config.h, OUT/cogl/cogl-defines.h, OUT/cogl/cogl-gl-header.h, OUT/cogl/cogl-egl-defines.h, OUT/cogl/cogl-enum-types.[ch], OUT/cogl-path/cogl-path-enum-types.[ch]
import glob
import os
import re
import subprocess
import sys

src, out = sys.argv[1], sys.argv[2]
mkenums = sys.argv[3] if len(sys.argv) > 3 else 'glib-mkenums'
os.makedirs(out + '/cogl', exist_ok=True)
os.makedirs(out + '/cogl-path', exist_ok=True)

symbols = ['COGL_HAS_GLIB_SUPPORT', 'COGL_HAS_GTYPE_SUPPORT', 'COGL_HAS_COGL_PATH_SUPPORT', 'COGL_HAS_GLES', 'CLUTTER_COGL_HAS_GLES', 'COGL_HAS_GLES2', 'COGL_HAS_EGL_SUPPORT',
           'COGL_HAS_EGL_PLATFORM_SYMBIAN_SUPPORT', 'COGL_HAS_POLL_SUPPORT']
defs = ''.join('#define %s 1\n' % s for s in symbols)
t = open(src + '/cogl/cogl-defines.h.in').read()
t = t.replace('@COGL_DEFINES@', defs.rstrip('\n')).replace('@COGL_1_MINOR_VERSION@', '22').replace('@COGL_1_MICRO_VERSION@', '8').replace('@COGL_1_VERSION@', '1.22.8')
poll_defs = ''.join('#define COGL_SYSDEF_%s %d\n' % (n, v) for n, v in (('POLLIN', 1), ('POLLPRI', 2), ('POLLOUT', 4), ('POLLERR', 8), ('POLLHUP', 16), ('POLLNVAL', 32)))      # Open C's values (poll.h)
t = t.replace('#define COGL_VERSION_STRING_INTERNAL', poll_defs + '#define COGL_VERSION_STRING_INTERNAL', 1)
t = re.sub(r'@[A-Z_0-9]+@', '', t)
open(out + '/cogl/cogl-defines.h', 'w').write(t)

t = open(src + '/cogl/cogl-gl-header.h.in').read().replace('@COGL_GL_HEADER_INCLUDES@', '#include <GLES2/gl2.h>\ntypedef char GLchar;\n#include <GLES2/gl2ext.h>')
open(out + '/cogl/cogl-gl-header.h', 'w').write(t)
t = open(src + '/cogl/cogl-egl-defines.h.in').read().replace('@COGL_EGL_INCLUDES@', '#include <EGL/egl.h>\n#include <EGL/eglext.h>')
open(out + '/cogl/cogl-egl-defines.h', 'w').write(t)

open(out + '/config.h', 'w').write('''#define GETTEXT_PACKAGE "cogl"
#define HAVE_COGL_GLES2 1
#define HAVE_EGL_EGL_H 1
#define HAVE_FLOAT_H 1
#define HAVE_MEMMEM 0
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_ALLOCA_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_POLL_H 1
#define HAVE_SYS_POLL_H 1
#define COGL_ENABLE_EXPERIMENTAL_API 1
#define COGL_ENABLE_EXPERIMENTAL_2_0_API 1
#define COGL_GL_LIBNAME "libGLESv2.dll"
#define COGL_GLES2_LIBNAME "libGLESv2.dll"
#define COGL_GLES1_LIBNAME "libGLES_CM.dll"
#define COGL_GLES_LIBNAME "libGLES_CM.dll"
#define COGL_EGL_LIBNAME "libEGL.dll"
#define COGL_WEBGL_LIBNAME ""
#define COGL_GLES1_LIBNAME2 ""
''')

skip = re.compile(r'(-private\.h$|cogl-defines|cogl-gl-header|cogl-egl-defines|cogl-enum-types|xlib|x11|sdl|wayland|kms|mir|glx|wgl|win32|android|gdl|emscripten|webgl|texture-pixmap|cogl-x11)')
hdrs = sorted(os.path.basename(h) for h in glob.glob(src + '/cogl/*.h') if not skip.search(os.path.basename(h)))
hdrs += ['deprecated/' + os.path.basename(h) for h in sorted(glob.glob(src + '/cogl/deprecated/*.h')) if not skip.search(os.path.basename(h))]


def run(template, files, cwd, dest):
    r = subprocess.run([mkenums, '--template', template] + files, cwd=cwd, capture_output=True, text=True, check=True)
    open(dest, 'w').write(r.stdout)


run(src + '/cogl/cogl-enum-types.h.in', hdrs, src + '/cogl', out + '/cogl/cogl-enum-types.h')
run(src + '/cogl/cogl-enum-types.c.in', hdrs, src + '/cogl', out + '/cogl/cogl-enum-types.c')
ph = sorted(os.path.basename(h) for h in glob.glob(src + '/cogl-path/*.h') if not re.search(r'private|enum-types', h))
run(src + '/cogl-path/cogl-path-enum-types.h.in', ph, src + '/cogl-path', out + '/cogl-path/cogl-path-enum-types.h')
run(src + '/cogl-path/cogl-path-enum-types.c.in', ph, src + '/cogl-path', out + '/cogl-path/cogl-path-enum-types.c')
print('cogl generated: %d headers for the enum types' % len(hdrs))
