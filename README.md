# clutter-symbian

Clutter 1.26.4 and Cogl 1.22.8 (the scene graph and its GPU layer) with json-glib 1.6.6, ported to Phorepoc OS: the library the Hildon desktop's own scene code (hildon-desktop's home, launcher and task switcher) is written on.
A library port, like qt-symbian: nothing in a system image needs it; it is installed from the Catalogue. Status: **feasibility test** (docs `hildon-port.md` 7.33 in the Symbian-ReverseEngineered repository): the libraries build, a test program
(`os/src/cluttertest`) puts a Clutter stage on a WSERV window through Cogl's GLES2 / EGL path.

- `recipes/build_cogl_x32.sh`, `build_jsonglib_x32.sh`, `build_clutter_x32.sh`: build from the tarballs pinned in `recipes/pins.sh` (downloaded into `dl/`, never copied here). The files configure / make would generate (config headers, enum types,
  marshallers) are written by `recipes/gen_cogl.py` and `gen_clutter.py` for the one configuration built: GLES2 over EGL ("null" EGL platform), the stock EGL ("eglnative") Clutter backend with the null input backend.
- The one patch to Cogl: its EGL "null" platform creates the window surface from no window; `phe_cogl_native_window ()` (os/src/cluttertest) hands it the WSERV `RWindow` instead (applied by `build_cogl_x32.sh`).
- `overlay/phe_gl_compat.h`: tokens the 2008 Khronos headers of the Symbian tree lack (GL_BGRA_EXT, EGL_SYNC_FENCE_KHR) and the `__declspec` marker.
- Licences: Clutter, Cogl and json-glib are LGPL-2.1-or-later (`LICENSES/`); our files are BSD-2-Clause (`LICENSE`).
