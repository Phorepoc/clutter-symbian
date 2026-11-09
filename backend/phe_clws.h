/* phe_clws.h -- the WSERV layer of clutter-symbian (BSD-2-Clause, Phorepoc): one RWsSession owned by the thread that runs Clutter (the GL thread: Mesa's EGL window surfaces use the
 * window's session from that thread), the windows of Cogl's onscreens, the WSERV events, and a GLib poll function that waits for WSERV and for the main loop's file descriptors at once
 * (the model of Qt's Symbian event dispatcher: a helper thread does the poll(), the GL thread waits on its request semaphore).  Used by Cogl's "symbian" EGL platform
 * (overlay/cogl-winsys-egl-symbian.c) and by Clutter's "symbian" backend (backend/clutter-backend-symbian.c). */
#ifndef PHE_CLWS_H
#define PHE_CLWS_H
#ifdef __cplusplus
extern "C" {
#endif

/* event types */
enum { PHE_CLWS_NONE = 0, PHE_CLWS_KEY_DOWN = 1, PHE_CLWS_KEY = 2, PHE_CLWS_KEY_UP = 3, PHE_CLWS_POINTER = 4, PHE_CLWS_REDRAW = 5, PHE_CLWS_SCREEN_CHANGED = 6,
       PHE_CLWS_FOCUS_IN = 7, PHE_CLWS_FOCUS_OUT = 8,
       PHE_CLWS_WATCHERS = 9 /* the window registry's watcher count changed: x = the count (C4) */ };
/* pointer types = TPointerEvent::TType: 0 button1 down, 1 button1 up, 2 button2 down, 3 up, 4 button3 down, 5 up, 6 drag, 7 move */
typedef struct {
  int type;
  void *window;              /* the window the event is for (pointer, redraw), NULL for keys / focus / screen */
  int x, y, w, h;            /* pointer position in the window, or the redraw rectangle, or the new screen size */
  int ptype;                 /* pointer type */
  int code, scancode, repeats;
  unsigned modifiers;        /* TEventModifier bits */
  unsigned time_ms;
} PheClwsEvent;

/* the same layout as GLib's GPollFD (gint fd; gushort events, revents) */
typedef struct { int fd; unsigned short events, revents; } PheClwsPollFD;
typedef int (*PheClwsPollFunc) (PheClwsPollFD *fds, unsigned nfds, int timeout);
#define PHE_CLWS_POLL_FD (-0x5753)   /* the pseudo descriptor of the WSERV event queue in a GSource's poll set (as G_WIN32_MSG_HANDLE in GLib's win32 poll) */

int  phe_clws_init (void);                                  /* connect (idempotent); 0 or a Symbian error */
void phe_clws_screen_size (int *width, int *height);
void *phe_clws_window_new (int x, int y, int width, int height, int visible);
void phe_clws_window_set_extent (void *window, int x, int y, int width, int height);
void phe_clws_window_size (void *window, int *width, int *height);
void phe_clws_window_set_visible (void *window, int visible);
void phe_clws_window_raise (void *window, int priority);    /* the window in front of the app's other windows, the app's window group to the front with that priority (and the keyboard focus) */
void phe_clws_window_destroy (void *window);
int  phe_clws_pending (void);                               /* a WSERV event or redraw is waiting */
int  phe_clws_next_event (PheClwsEvent *ev);                /* 1 = an event was taken */
int  phe_clws_poll (PheClwsPollFD *fds, unsigned nfds, int timeout);   /* the GMainContext poll function */
void phe_clws_set_inner_poll (PheClwsPollFunc inner);       /* GLib's own poll (g_poll), used by the helper thread for the real descriptors */
void phe_clws_log (const char *msg);

/* the window registry (hildon-ui phewinreg, hildon-port.md C4): a stage's metadata and content for the frame process.
   type = TPheWinType, state = EPheWin* flags of phewinreg.h. reg_window returns the id (> 0) or an error (< 0: no registry).
   reg_content copies w x h premultiplied BGRA pixels (stride bytes per row) into the window's shared bitmap and returns
   the number of watchers (0: nobody imports it, the caller may skip copying until one appears) or an error. */
int phe_clws_reg_window (const char *title, int type, int x, int y, int w, int h, unsigned state);
int phe_clws_reg_update (int id, const char *title, int type, int x, int y, int w, int h, unsigned state);
int phe_clws_reg_content (int id, int w, int h, const void *pixels, int stride, int dx, int dy, int dw, int dh);
/* the GPU path (hildon-port.md 7.47): the content is the exported texture `token` (EGL_SYMBIAN_SVGA_SHARE_PH) of w x h */
int phe_clws_reg_content_gpu (int id, int token, int w, int h, int dx, int dy, int dw, int dh);
void phe_clws_reg_remove (int id);

#ifdef __cplusplus
}
#endif
#endif
