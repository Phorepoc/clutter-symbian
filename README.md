# clutter-symbian

Clutter 1.26.4 and Cogl 1.22.8 (the scene graph and its GPU layer) with json-glib 1.6.6, ported to Phorepoc OS: the library the Hildon desktop's own scene code (hildon-desktop's home, launcher and task switcher) is written on.
A library port, like qt-symbian: nothing in a system image needs it; it is installed from the Catalogue. Status: **feasibility test** (docs `hildon-port.md` 7.33 in the Symbian-ReverseEngineered repository): the libraries build, a test program
(`os/src/cluttertest`) puts a Clutter stage on a WSERV window through Cogl's GLES2 / EGL path.

- `recipes/build_cogl_x32.sh`, `build_jsonglib_x32.sh`, `build_clutter_x32.sh`: build from the tarballs pinned in `recipes/pins.sh` (downloaded into `dl/`, never copied here). The files configure / make would generate (config headers, enum types,
  marshallers) are written by `recipes/gen_cogl.py` and `gen_clutter.py` for the one configuration built: GLES2 over EGL with Cogl's "symbian" EGL platform and Clutter's "symbian" backend.
- **The Symbian backend** (no application-supplied window, no null input):
  - `backend/phe_clws.cpp` (BSD-2-Clause): the WSERV session, window group and windows, owned by the thread that runs Clutter (Mesa's EGL window surface uses the window's session from the
    rendering thread); the WSERV event and redraw requests; and the GMainContext poll function that waits for WSERV and the main loop's descriptors together (the model of Qt 4's
    Symbian dispatcher: a helper thread runs GLib's poll(), the Clutter thread waits on its request semaphore and interrupts the helper through a pipe when WSERV comes first).
  - `overlay/cogl-winsys-egl-symbian.c` (MIT, as Cogl): Cogl's EGL platform for WSERV, derived from the null / X11 / Wayland platforms: a window and EGL surface per onscreen, a hidden 1x1
    dummy surface for the context, visibility, `cogl_symbian_onscreen_resize`. Registered by `build_cogl_x32.sh` as Cogl registers its platforms (a `CoglWinsysID` and the renderer's list).
  - `backend/clutter-backend-symbian.c` (LGPL-2.1+, as Clutter): the "symbian" backend, derived from Clutter's win32 and EGL native backends: a `ClutterStageCogl` subclass whose window
    follows the stage (size, full screen in the current screen mode, the window group to the front on show), a device manager with the core pointer and keyboard, and a GSource whose
    poll descriptor is the WSERV queue: pointer (buttons 1-3, drag, move, enter), keys (WSERV's key-down / key / key-up into press / release with keysyms, modifiers, auto-repeat),
    redraws, focus (stage activation) and screen-mode changes (rotation: full-screen stages follow). Registered by `build_clutter_x32.sh` in clutter-backend.c's list.
- `overlay/phe_gl_compat.h`: tokens the 2008 Khronos headers of the Symbian tree lack (GL_BGRA_EXT, EGL_SYNC_FENCE_KHR) and the `__declspec` marker.
- Licences: Clutter and json-glib are LGPL-2.1-or-later, Cogl is MIT (`LICENSES/`); our files are BSD-2-Clause (`LICENSE`).
