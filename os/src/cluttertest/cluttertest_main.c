/* cluttertest_main.c -- the Clutter feasibility test (docs hildon-port.md 7.33). BSD-2-Clause (Phorepoc).
 * One full-screen stage (the "symbian" backend's own window; presses move the turning actor, keys are logged, arrows move it), a textured actor rotating around its centre, a second textured actor with a ClutterBlurEffect (the offscreen + GLSL path hildon-desktop's task switcher uses), both turning for good;
 * every two seconds "[cluttertest] N frames in T ms = F fps" (frames counted at the stage's after-paint). CLUTTER_UNCAPPED: the master clock runs as fast as it can (frame rate 1000). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <clutter/clutter.h>

extern void phe_log (const char *msg);
#define LOG(...) do { char m_[300]; g_snprintf (m_, sizeof m_, __VA_ARGS__); phe_log (m_); } while (0)

static gint frames;
static gint64 t0;
static ClutterActor *spin, *blur, *stage2;

static void
after_paint (ClutterActor *stage, gpointer data)
{
  gint64 now = g_get_monotonic_time ();
  (void) stage; (void) data;
  frames++;
  if (now - t0 >= 2000000) {
    LOG ("[cluttertest] %d frames in %d ms = %.1f fps", frames, (int) ((now - t0) / 1000), frames * 1000000.0 / (now - t0));
    frames = 0; t0 = now;
  }
}

/* proof that the two replaced stubs are real: the SGI libtess (GLU tessellator) and cairo's PNG writer / reader */
extern void *gluNewTess (void);
extern void gluDeleteTess (void *t);
extern void gluTessCallback (void *t, unsigned which, void (*fn) ());
extern void gluTessBeginPolygon (void *t, void *data);
extern void gluTessEndPolygon (void *t);
extern void gluTessBeginContour (void *t);
extern void gluTessEndContour (void *t);
extern void gluTessVertex (void *t, double *coords, void *data);
static int tess_vertices, tess_begins;
static void tess_vertex_cb (void *v) { (void) v; tess_vertices++; }
static void tess_begin_cb (unsigned type) { (void) type; tess_begins++; }
static void tess_end_cb (void) { }
static void
proof_tess_and_png (void)
{
  void *t = gluNewTess ();
  static double poly[5][3] = { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 4, 0 }, { 2, 1, 0 }, { 0, 4, 0 } };      /* a concave pentagon: 3 triangles */
  int i;
  cairo_surface_t *s, *r; cairo_status_t st; cairo_t *cr;
  gluTessCallback (t, 100101, (void (*) ()) tess_vertex_cb);       /* GLU_TESS_VERTEX */
  gluTessCallback (t, 100100, (void (*) ()) tess_begin_cb);        /* GLU_TESS_BEGIN */
  gluTessCallback (t, 100102, (void (*) ()) tess_end_cb);          /* GLU_TESS_END */
  gluTessBeginPolygon (t, NULL); gluTessBeginContour (t);
  for (i = 0; i < 5; i++) gluTessVertex (t, poly[i], poly[i]);
  gluTessEndContour (t); gluTessEndPolygon (t); gluDeleteTess (t);
  LOG ("[cluttertest] libtess: a concave pentagon -> %d primitives, %d vertices", tess_begins, tess_vertices);
  s = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 64, 48);
  cr = cairo_create (s); cairo_set_source_rgba (cr, 0.8, 0.2, 0.2, 0.9); cairo_arc (cr, 32, 24, 20, 0, 6.2832); cairo_fill (cr); cairo_destroy (cr);
  st = cairo_surface_write_to_png (s, "c:/data/cluttertest-cairo.png");
  r = cairo_image_surface_create_from_png ("c:/data/cluttertest-cairo.png");
  LOG ("[cluttertest] cairo PNG: write status %d (%s), read back %dx%d (status %d)", (int) st, cairo_status_to_string (st), cairo_image_surface_get_width (r), cairo_image_surface_get_height (r), (int) cairo_surface_status (r));
  cairo_surface_destroy (s); cairo_surface_destroy (r);
}

static gboolean
uncap (gpointer data)
{
  (void) data;
  clutter_set_default_frame_rate (1000);                  /* the master clock no longer waits for the next 1/60 s */
  LOG ("[cluttertest] --- uncapped (frame rate 1000) ---");
  frames = 0; t0 = g_get_monotonic_time ();
  return FALSE;
}

static gboolean steady;

static gboolean
steady_blur (gpointer data)
{
  (void) data;
  steady = TRUE;                                           /* phase B2: the blurred actor slides instead of turning, so its paint box keeps one size */
  clutter_actor_set_rotation_angle (blur, CLUTTER_Z_AXIS, 0);
  LOG ("[cluttertest] --- phase B2: blurred actor slides (constant paint box) ---");
  frames = 0; t0 = g_get_monotonic_time ();
  return FALSE;
}

static gboolean
no_blur (gpointer data)
{
  (void) data;
  clutter_actor_clear_effects (blur);                      /* phase C: the second actor without the blur effect */
  LOG ("[cluttertest] --- phase C: blur effect removed ---");
  frames = 0; t0 = g_get_monotonic_time ();
  return FALSE;
}

static gboolean
one_quad (gpointer data)
{
  (void) data;
  clutter_actor_hide (blur);                               /* phase D: one turning textured quad only */
  LOG ("[cluttertest] --- phase D: one textured actor ---");
  frames = 0; t0 = g_get_monotonic_time ();
  return FALSE;
}

static void
on_frame (ClutterTimeline *tl, gint msecs, gpointer data)
{
  gdouble a = msecs * 360.0 / 4000.0;
  (void) tl; (void) data;
  clutter_actor_set_rotation_angle (spin, CLUTTER_Z_AXIS, a);
  if (steady)
    clutter_actor_set_x (blur, 60 + 40 * (1 + sin (a * G_PI / 90.0)));
  else
    clutter_actor_set_rotation_angle (blur, CLUTTER_Z_AXIS, -a);
}

static ClutterContent *
make_image (int w, int h, int phase)
{
  guchar *px = g_malloc (w * h * 4); int x, y;
  ClutterContent *img = clutter_image_new ();
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      guchar *p = px + (y * w + x) * 4; int c = ((x / 16) + (y / 16) + phase) & 1;
      p[0] = c ? 230 : 40 + x * 150 / w; p[1] = c ? 200 : 60 + y * 150 / h; p[2] = c ? 60 : 200; p[3] = 255;
    }
  clutter_image_set_data (CLUTTER_IMAGE (img), px, COGL_PIXEL_FORMAT_RGBA_8888, w, h, w * 4, NULL);
  g_free (px);
  return img;
}

/* input through the backend's device manager (C2): a press moves the turning actor there, keys are logged and the arrows move it */
static gboolean
on_press (ClutterActor *stage, ClutterEvent *ev, gpointer data)
{
  gfloat x, y; ClutterInputDevice *dev = clutter_event_get_device (ev);
  clutter_event_get_coords (ev, &x, &y); (void) data;
  LOG ("[cluttertest] button %u press at %.0f,%.0f (device '%s', clicks %d, source %s)", clutter_event_get_button (ev), x, y, dev ? clutter_input_device_get_device_name (dev) : "-",
       clutter_event_get_click_count (ev), G_OBJECT_TYPE_NAME (clutter_event_get_source (ev)));
  clutter_actor_set_position (spin, x - 100, y - 100);
  return CLUTTER_EVENT_STOP;
}

static gboolean
on_press2 (ClutterActor *stage, ClutterEvent *ev, gpointer data)
{
  gfloat x, y; (void) data;
  clutter_event_get_coords (ev, &x, &y);
  LOG ("[cluttertest] SECOND stage: button %u press at %.0f,%.0f", clutter_event_get_button (ev), x, y);
  return CLUTTER_EVENT_STOP;
}

static gint s2_paints;
static void
after_paint2 (ClutterActor *stage, gpointer data)
{
  (void) stage; (void) data;
  if (s2_paints++ < 3) LOG ("[cluttertest] second stage painted (%d)", s2_paints);
}

static gint motions;

static gboolean
on_motion (ClutterActor *stage, ClutterEvent *ev, gpointer data)
{
  gfloat x, y; (void) data;
  clutter_event_get_coords (ev, &x, &y);
  if (motions++ % 20 == 0) LOG ("[cluttertest] motion %d at %.0f,%.0f state 0x%x", motions, x, y, (unsigned) clutter_event_get_state (ev));
  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
on_key (ClutterActor *stage, ClutterEvent *ev, gpointer data)
{
  guint k = clutter_event_get_key_symbol (ev); gfloat x, y; (void) data;
  LOG ("[cluttertest] key %s keysym 0x%x unicode 0x%x hw %u", clutter_event_type (ev) == CLUTTER_KEY_PRESS ? "press" : "release", k, clutter_event_get_key_unicode (ev),
       clutter_event_get_key_code (ev));
  if (clutter_event_type (ev) != CLUTTER_KEY_PRESS) return CLUTTER_EVENT_PROPAGATE;
  clutter_actor_get_position (spin, &x, &y);
  if (k == CLUTTER_KEY_d && stage2) { clutter_actor_destroy (stage2); stage2 = NULL; LOG ("[cluttertest] second stage destroyed (key d): one full-screen stage"); return CLUTTER_EVENT_STOP; }
  if (k == CLUTTER_KEY_Left) x -= 20; else if (k == CLUTTER_KEY_Right) x += 20; else if (k == CLUTTER_KEY_Up) y -= 20; else if (k == CLUTTER_KEY_Down) y += 20;
  clutter_actor_set_position (spin, x, y);
  return CLUTTER_EVENT_STOP;
}

static void
on_resize (ClutterActor *stage, GParamSpec *ps, gpointer data)
{
  gfloat w, h; (void) ps; (void) data;
  clutter_actor_get_size (stage, &w, &h);
  LOG ("[cluttertest] stage size %.0fx%.0f, fullscreen %d", w, h, clutter_stage_get_fullscreen (CLUTTER_STAGE (stage)));
}

static void
on_activate (ClutterActor *stage, gpointer data)
{
  LOG ("[cluttertest] stage %s", data ? "activated (keyboard focus)" : "deactivated");
}

extern void phe_clws_screen_size (int *w, int *h);

/* ======================================================================== the frame side of the registry test (C4, "cluttertest frame")
 * a small compositor (hildon-port.md 7.48/7.49): the registry's events reach the main loop (the backend's wait set); a change lists
 * the windows again (phe_frame_list, cluttertest.cpp: it subscribes to every other process's window as its compositor), a content tick
 * is a redraw. The main loop draws the windows on
 * this process's own FULL-SCREEN stage, at their geometry, in registration order: a window with a GPU share token is its exported
 * texture (imported once per token, flipped: the exporter copies GL rows), else its published bitmap. The composited windows are off
 * the window server's screen (their owners hide them and skip the present), so this stage alone is on the screen and keeps scan-out. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cogl/cogl-egl.h>
#define EGL_SYMBIAN_SVGA_SHARE_PH 0x3FF2
CoglTexture2D *_cogl_egl_texture_2d_new_from_image (CoglContext *ctx, int width, int height, CoglPixelFormat format,
                                                    EGLImageKHR image, CoglError **error);

typedef struct { int id, x, y, w, h, token; unsigned char *px; int pw, ph; char title[64]; } PheFrameWin;
#define FRAME_MAX 8
extern int phe_frame_list (PheFrameWin *out, int max);
extern int phe_clws_frame_watch (void);
extern void (*clutter_symbian_registry_hook) (int content);
extern void phe_frame_free (unsigned char *pixels);
extern int phe_frame_spawn (void);
extern int phe_is_frame (void);
extern int phe_cmd_has (const char *word);

typedef struct { int n; PheFrameWin w[FRAME_MAX]; } FrameList;
typedef struct { int id, token; ClutterActor *gpu, *bmp; EGLImageKHR image; gboolean seen; } FrameWin;
static ClutterActor *frame_stage;
static GList *frame_wins;          /* FrameWin, registration order */
static int frame_checks, frame_lists, frame_frames;
static gint64 frame_t0;

extern int phe_clws_reg_pointer (int id, int type, int x, int y);

/* a click on a bitmap window goes to its owner in the window's content pixels (TRawEvent EButton1Down 10 / EButton1Up 11 /
 * EPointerMove 1): a classic app's screen shown as a window takes the click as its own digitiser's (hildon-port.md 7.51 (d)) */
static gboolean
frame_button (ClutterActor *actor, ClutterEvent *event, gpointer data)
{
  FrameWin *f = data;
  gfloat x, y, lx = 0, ly = 0;
  int type, r;
  ClutterEventType t = clutter_event_type (event);
  type = t == CLUTTER_BUTTON_PRESS ? 10 : t == CLUTTER_BUTTON_RELEASE ? 11 : 1;
  clutter_event_get_coords (event, &x, &y);
  clutter_actor_transform_stage_point (actor, x, y, &lx, &ly);
  r = phe_clws_reg_pointer (f->id, type, (int) lx, (int) ly);
  if (type != 1)
    LOG ("[cluttertest-frame] window %d: pointer %d at %d,%d -> %d", f->id, type, (int) lx, (int) ly, r);
  return CLUTTER_EVENT_STOP;
}

static FrameWin *
frame_win (int id)
{
  GList *l;
  FrameWin *f;
  for (l = frame_wins; l; l = l->next)
    if (((FrameWin *) l->data)->id == id)
      return l->data;
  f = g_new0 (FrameWin, 1);
  f->id = id;
  f->gpu = clutter_texture_new ();
  f->bmp = clutter_actor_new ();
  clutter_actor_set_content (f->bmp, clutter_image_new ());
  clutter_actor_set_reactive (f->bmp, TRUE);
  g_signal_connect (f->bmp, "button-press-event", G_CALLBACK (frame_button), f);
  g_signal_connect (f->bmp, "button-release-event", G_CALLBACK (frame_button), f);
  clutter_actor_hide (f->gpu);
  clutter_actor_hide (f->bmp);
  clutter_actor_add_child (frame_stage, f->gpu);
  clutter_actor_add_child (frame_stage, f->bmp);
  frame_wins = g_list_append (frame_wins, f);
  return f;
}

static void
frame_win_free (FrameWin *f)
{
  CoglContext *ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  clutter_actor_destroy (f->gpu);
  clutter_actor_destroy (f->bmp);
  if (f->image)
    ((PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress ("eglDestroyImageKHR")) (cogl_egl_context_get_egl_display (ctx), f->image);
  g_free (f);
}

static gboolean
frame_import_gpu (FrameWin *f, const PheFrameWin *w)
{
  CoglContext *ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  EGLDisplay dpy = cogl_egl_context_get_egl_display (ctx);
  PFNEGLCREATEIMAGEKHRPROC create = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress ("eglCreateImageKHR");
  PFNEGLDESTROYIMAGEKHRPROC destroy = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress ("eglDestroyImageKHR");
  EGLint attrs[] = { EGL_WIDTH, w->w, EGL_HEIGHT, w->h, EGL_NONE };
  EGLImageKHR img = create (dpy, EGL_NO_CONTEXT, EGL_SYMBIAN_SVGA_SHARE_PH, (EGLClientBuffer) (intptr_t) w->token, attrs);
  CoglError *err = NULL;
  CoglTexture2D *tex = img != EGL_NO_IMAGE_KHR ?
    _cogl_egl_texture_2d_new_from_image (ctx, w->w, w->h, COGL_PIXEL_FORMAT_BGRA_8888_PRE, img, &err) : NULL;

  if (!tex)
    {
      LOG ("[cluttertest-frame] GPU import of token %d failed (EGL %x, %s)", w->token, eglGetError (), err ? err->message : "-");
      if (err) cogl_error_free (err);
      if (img != EGL_NO_IMAGE_KHR) destroy (dpy, img);
      return FALSE;
    }
  clutter_texture_set_cogl_texture (CLUTTER_TEXTURE (f->gpu), COGL_TEXTURE (tex));
  cogl_object_unref (tex);
  if (f->image)
    destroy (dpy, f->image);
  f->image = img;
  f->token = w->token;
  LOG ("[cluttertest-frame] composite: window %d '%s' %dx%d at %d,%d, token %d (GPU texture, no copy)", w->id, w->title, w->w, w->h,
       w->x, w->y, w->token);
  return TRUE;
}

static gboolean
frame_show (gpointer data)
{
  FrameList *list = data;
  GList *l, *next;
  int i;

  for (l = frame_wins; l; l = l->next)
    ((FrameWin *) l->data)->seen = FALSE;
  for (i = 0; i < list->n; i++)
    {
      PheFrameWin *w = &list->w[i];
      FrameWin *f = frame_win (w->id);
      f->seen = TRUE;
      if (w->token && (w->token == f->token || frame_import_gpu (f, w)))
        {
          clutter_actor_set_position (f->gpu, w->x, w->y);
          clutter_actor_set_size (f->gpu, w->w, w->h);
          clutter_actor_set_pivot_point (f->gpu, 0.5, 0.5);
          clutter_actor_set_scale (f->gpu, 1.0, -1.0);
          clutter_actor_hide (f->bmp);
          clutter_actor_show (f->gpu);
          clutter_actor_queue_redraw (f->gpu);       /* the texture is the window's: new content is a redraw */
        }
      else if (w->px)
        {
          GError *err = NULL;
          if (clutter_image_set_data (CLUTTER_IMAGE (clutter_actor_get_content (f->bmp)), w->px, COGL_PIXEL_FORMAT_BGRA_8888_PRE,
                                      w->pw, w->ph, w->pw * 4, &err))
            {
              clutter_actor_set_position (f->bmp, w->x, w->y);
              clutter_actor_set_size (f->bmp, w->pw, w->ph);
              if (w->pw > 0 && w->ph > 0)
                {
                  /* (once per window) the import proof: the content's size and its centre pixel, BGRA */
                  const unsigned char *c = w->px + ((w->ph / 2) * w->pw + w->pw / 2) * 4;
                  static int logged[16];
                  if (w->id > 0 && w->id < 16 && !logged[w->id]++)
                    LOG ("[cluttertest-frame] window %d \"%s\" bitmap imported %dx%d, centre px %02x%02x%02x", w->id, w->title,
                         w->pw, w->ph, c[2], c[1], c[0]);
                }
              clutter_actor_hide (f->gpu);
              clutter_actor_show (f->bmp);
            }
          else
            LOG ("[cluttertest-frame] bitmap import failed: %s", err ? err->message : "?");
          g_clear_error (&err);
        }
      if (w->px)
        phe_frame_free (w->px);
    }
  for (l = frame_wins; l; l = next)
    {
      next = l->next;
      if (!((FrameWin *) l->data)->seen)
        {
          frame_win_free (l->data);
          frame_wins = g_list_delete_link (frame_wins, l);
        }
    }
  if (frame_lists++ < 2)
    LOG ("[cluttertest-frame] compositing %d window(s)", list->n);
  g_free (list);
  return G_SOURCE_REMOVE;
}

/* the registry's events, on the main loop (no helper thread, hildon-port.md 7.49): a change = list again; a content tick = one
   redraw request (Clutter coalesces them into the next master-clock frame) */
static int frame_relists, frame_ticks;
/* per-phase timers of a compositor frame (hildon-port.md 7.52): first content tick -> the master clock's pre-paint (clock latency),
   pre-paint -> stage paint (the stage update: events, relayout), paint -> after-paint (painting), after-paint -> post-paint (the swap
   and the rest of the update), post-paint -> next pre-paint (idle) */
static gint64 frame_tick_first, frame_pre, frame_post, frame_after;
static gint64 frame_sum_lat, frame_sum_upd, frame_sum_swap, frame_sum_idle;
static int frame_lat_n;
extern gint64 phe_cogl_swap_enter_us, phe_cogl_swap_sum_us;   /* the Cogl overlay's swap timing (7.53) */
static gint64 frame_sum_flush, frame_sum_eglswap, frame_swap0, frame_cpu_after, frame_sum_cpu;
extern long long phe_clws_thread_cpu_us (void);
extern unsigned phe_clws_fast_ticks (void);
extern int phe_clws_fast_freq (void);
static unsigned frame_fc0;
static void
frame_registry (int content)
{
  if (content)
    {
      frame_ticks++;
      if (!frame_tick_first)
        frame_tick_first = g_get_monotonic_time ();
      clutter_actor_queue_redraw (frame_stage);
      return;
    }
  {
    FrameList *list = g_new0 (FrameList, 1);
    list->n = phe_frame_list (list->w, FRAME_MAX);
    frame_relists++;
    if (list->n < 0) { LOG ("[cluttertest-frame] registry: %d", list->n); g_free (list); return; }
    frame_show (list);
  }
}

/* after each paint of the compositor's stage: its frame rate, and the pixel check (its (10,10) and (1010,10), three times once a GPU
   window is composited) */
static gint64 frame_paint_start, frame_paint_sum, frame_wait0;
extern unsigned long long phe_clws_wait_us;   /* the backend's time blocked in its wait set (7.50) */
static void
frame_paint (ClutterActor *stage, gpointer data)
{
  (void) stage; (void) data;
  frame_paint_start = g_get_monotonic_time ();
}

static gboolean
frame_pre_paint (gpointer data)
{
  gint64 now = g_get_monotonic_time ();
  (void) data;
  if (frame_post && frame_post <= now)
    frame_sum_idle += now - frame_post;
  if (frame_tick_first && frame_tick_first <= now)
    frame_sum_lat += now - frame_tick_first, frame_lat_n++;
  frame_tick_first = 0;
  frame_pre = now;
  return TRUE;
}

static gboolean
frame_post_paint (gpointer data)
{
  gint64 now = g_get_monotonic_time ();
  (void) data;
  if (frame_after && frame_after <= now && frame_after >= frame_pre)
    {
      frame_sum_swap += now - frame_after;
      if (frame_cpu_after >= 0)
        frame_sum_cpu += phe_clws_thread_cpu_us () - frame_cpu_after;
      if (phe_cogl_swap_enter_us >= frame_after)
        frame_sum_flush += phe_cogl_swap_enter_us - frame_after;   /* Cogl: the journal flush before the winsys swap */
    }
  frame_sum_eglswap += phe_cogl_swap_sum_us - frame_swap0;
  frame_swap0 = phe_cogl_swap_sum_us;
  frame_post = now;
  return TRUE;
}

static void
frame_after_paint (ClutterActor *stage, gpointer data)
{
  guchar a[4], b[4];
  gint64 now = g_get_monotonic_time ();
  (void) data; (void) stage;
  if (!frame_t0)
    {
      frame_t0 = now;
      frame_fc0 = phe_clws_fast_ticks ();
      frame_wait0 = (gint64) phe_clws_wait_us;
    }
  frame_frames++;
  frame_after = now;
  frame_cpu_after = phe_clws_thread_cpu_us ();
  if (frame_paint_start && frame_paint_start <= now)
    frame_paint_sum += now - frame_paint_start;
  if (frame_pre && frame_paint_start >= frame_pre)
    frame_sum_upd += frame_paint_start - frame_pre;
  if (now - frame_t0 >= 2000000)
    {
      const gint64 wait = (gint64) phe_clws_wait_us - frame_wait0;
      {
        static int clock_checks;
        const unsigned fc = phe_clws_fast_ticks ();
        const int ff = phe_clws_fast_freq ();
        if (clock_checks++ < 3)
          LOG ("[cluttertest-frame] clocks over this window: monotonic %d ms, fast counter %u ticks at %d Hz = %d ms",
               (int) ((now - frame_t0) / 1000), fc - frame_fc0, ff, ff > 0 ? (int) ((unsigned long long) (fc - frame_fc0) * 1000u / (unsigned) ff) : -1);
        frame_fc0 = fc;
      }
      LOG ("[cluttertest-frame] %d composited frames in %d ms = %.1f fps (%d content ticks, %d lists; per frame: paint %d us, waiting %d us)",
           frame_frames, (int) ((now - frame_t0) / 1000), frame_frames * 1000000.0 / (now - frame_t0), frame_ticks, frame_relists,
           (int) (frame_paint_sum / frame_frames), (int) (wait / frame_frames));
      LOG ("[cluttertest-frame] phases us/frame: tick->clock %d, update (pre-paint..paint) %d, swap+post %d, idle %d",
           frame_lat_n ? (int) (frame_sum_lat / frame_lat_n) : 0, (int) (frame_sum_upd / frame_frames),
           (int) (frame_sum_swap / frame_frames), (int) (frame_sum_idle / frame_frames));
      LOG ("[cluttertest-frame] swap+post split us/frame: journal flush %d, EGL swap %d, after the swap %d; this thread's CPU in swap+post %d (-1 = not measured)",
           (int) (frame_sum_flush / frame_frames), (int) (frame_sum_eglswap / frame_frames),
           (int) ((frame_sum_swap - frame_sum_flush - frame_sum_eglswap) / frame_frames),
           frame_cpu_after >= 0 ? (int) (frame_sum_cpu / frame_frames) : -1);
      frame_sum_flush = frame_sum_eglswap = frame_sum_cpu = 0;
      frame_sum_lat = frame_sum_upd = frame_sum_swap = frame_sum_idle = 0; frame_lat_n = 0;
      frame_frames = frame_ticks = frame_relists = 0;
      frame_paint_sum = 0;
      frame_wait0 = (gint64) phe_clws_wait_us;
      frame_t0 = now;
    }
  if (frame_checks >= 3 || !frame_wins || !((FrameWin *) frame_wins->data)->token)
    return;
  frame_checks++;
  cogl_framebuffer_read_pixels (cogl_get_draw_framebuffer (), 10, 10, 1, 1, COGL_PIXEL_FORMAT_BGRA_8888_PRE, a);
  cogl_framebuffer_read_pixels (cogl_get_draw_framebuffer (), 1010, 10, 1, 1, COGL_PIXEL_FORMAT_BGRA_8888_PRE, b);
  LOG ("[cluttertest-frame] composite check #%d: pixel(10,10) = %02x%02x%02x%02x, pixel(1010,10) = %02x%02x%02x%02x", frame_checks,
       a[3], a[2], a[1], a[0], b[3], b[2], b[1], b[0]);
}

static int
frame_main (void)
{
  ClutterColor bg = { 0, 0, 0, 255 };

  if (clutter_init (NULL, NULL) != CLUTTER_INIT_SUCCESS) return 1;
  frame_stage = clutter_stage_new ();
  clutter_stage_set_title (CLUTTER_STAGE (frame_stage), "frame");
  clutter_actor_set_background_color (frame_stage, &bg);
  clutter_stage_set_fullscreen (CLUTTER_STAGE (frame_stage), TRUE);
  g_signal_connect (frame_stage, "after-paint", G_CALLBACK (frame_after_paint), NULL);
  g_signal_connect (frame_stage, "paint", G_CALLBACK (frame_paint), NULL);
  clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_PRE_PAINT, frame_pre_paint, NULL, NULL);
  clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_POST_PAINT, frame_post_paint, NULL, NULL);
  clutter_actor_show (frame_stage);
  clutter_symbian_registry_hook = frame_registry;
  LOG ("[cluttertest-frame] up: compositing the registry's windows full screen (registry events: %d)", phe_clws_frame_watch ());
  clutter_main ();
  return 0;
}

int
phe_clutter_main (void)
{
  ClutterActor *stage; ClutterTimeline *tl; ClutterColor bg = { 20, 30, 50, 255 };
  GError *err = NULL; int width, height;
  if (phe_is_frame ())
    return frame_main ();
  LOG ("[cluttertest] clutter_init (Clutter %d.%d.%d)", CLUTTER_MAJOR_VERSION, CLUTTER_MINOR_VERSION, CLUTTER_MICRO_VERSION);
  if (clutter_init (NULL, NULL) != CLUTTER_INIT_SUCCESS) { LOG ("[cluttertest] clutter_init failed"); return 1; }
  if (g_getenv ("CLUTTER_UNCAPPED")) clutter_set_default_frame_rate (1000);
  proof_tess_and_png ();
  phe_clws_screen_size (&width, &height);
  stage = clutter_stage_new ();                            /* the backend makes its window: full screen, in the screen's current mode */
  clutter_stage_set_title (CLUTTER_STAGE (stage), "cluttertest");
  clutter_stage_set_fullscreen (CLUTTER_STAGE (stage), TRUE);
  g_signal_connect (stage, "button-press-event", G_CALLBACK (on_press), NULL);
  g_signal_connect (stage, "motion-event", G_CALLBACK (on_motion), NULL);
  g_signal_connect (stage, "key-press-event", G_CALLBACK (on_key), NULL);
  g_signal_connect (stage, "key-release-event", G_CALLBACK (on_key), NULL);
  g_signal_connect (stage, "notify::size", G_CALLBACK (on_resize), NULL);
  g_signal_connect (stage, "activate", G_CALLBACK (on_activate), GINT_TO_POINTER (1));
  g_signal_connect (stage, "deactivate", G_CALLBACK (on_activate), NULL);
  clutter_actor_set_background_color (stage, &bg);
  spin = clutter_actor_new (); clutter_actor_set_size (spin, 200, 200);
  clutter_actor_set_content (spin, make_image (128, 128, 0));
  clutter_actor_set_pivot_point (spin, 0.5, 0.5);
  clutter_actor_set_position (spin, width / 2 - 100, height / 4 - 100);
  blur = clutter_actor_new (); clutter_actor_set_size (blur, 200, 200);
  clutter_actor_set_content (blur, make_image (128, 128, 1));
  clutter_actor_set_pivot_point (blur, 0.5, 0.5);
  clutter_actor_set_position (blur, width / 2 - 100, 3 * height / 4 - 100);
  clutter_actor_add_effect (blur, clutter_blur_effect_new ());
  clutter_actor_add_child (stage, spin);
  clutter_actor_add_child (stage, blur);
  tl = clutter_timeline_new (4000); clutter_timeline_set_repeat_count (tl, -1);
  g_signal_connect (tl, "new-frame", G_CALLBACK (on_frame), NULL);
  g_signal_connect (stage, "after-paint", G_CALLBACK (after_paint), NULL);
  t0 = g_get_monotonic_time ();
  if (!g_getenv ("CLUTTER_UNCAPPED")) { g_timeout_add_seconds (14, uncap, NULL); g_timeout_add_seconds (28, steady_blur, NULL); g_timeout_add_seconds (42, no_blur, NULL); g_timeout_add_seconds (56, one_quad, NULL); }          /* 14 s at the default 60 fps cap, then as fast as it goes */
  clutter_timeline_start (tl);
  clutter_actor_show (stage);
  {
    /* the two-stage test: a second, smaller stage (its own window and EGL surface through Cogl's symbian platform), shown over the first */
    ClutterActor *s2 = stage2 = clutter_stage_new (), *r = clutter_actor_new (); ClutterColor c2 = { 200, 60, 40, 255 }, cr = { 240, 230, 80, 255 };
    clutter_actor_set_size (s2, 160, 120);
    clutter_actor_set_background_color (s2, &c2);
    clutter_actor_set_size (r, 60, 40); clutter_actor_set_position (r, 50, 40); clutter_actor_set_background_color (r, &cr);
    clutter_actor_add_child (s2, r);
    g_signal_connect (s2, "button-press-event", G_CALLBACK (on_press2), NULL);
    g_signal_connect (s2, "key-press-event", G_CALLBACK (on_key), NULL);   /* (the newest stage has the keyboard focus: its window group is in front) */
    g_signal_connect (s2, "after-paint", G_CALLBACK (after_paint2), NULL);
    clutter_actor_show (s2);
    LOG ("[cluttertest] second stage shown (160x120)");
  }
  {
    /* C3: text through cogl-pango (ClutterText: a Pango layout, glyphs cached in Cogl textures) with the installed fonts */
    ClutterColor tc = { 255, 255, 255, 255 }; PangoFontDescription *fd; PangoLayout *pl; int lw, lh;
    ClutterActor *t = clutter_text_new_full ("Sans 18", "Clutter on Phorepoc \xc3\xa4\xc3\xb6\xc3\xbc \xce\x91\xce\xb2\xce\xb3", &tc);
    clutter_actor_set_position (t, 12, height - 60);
    clutter_actor_add_child (stage, t);
    pl = clutter_text_get_layout (CLUTTER_TEXT (t));
    pango_layout_get_pixel_size (pl, &lw, &lh);
    {
      PangoFont *f = pango_context_load_font (pango_layout_get_context (pl), pango_context_get_font_description (pango_layout_get_context (pl)));   /* the face fontconfig chose */
      fd = f ? pango_font_describe (f) : NULL;
      if (f) g_object_unref (f);
    }
    LOG ("[cluttertest] text: layout %dx%d px, %d lines, font '%s'", lw, lh, pango_layout_get_line_count (pl), fd ? pango_font_description_get_family (fd) : "?");
    if (fd) pango_font_description_free (fd);
  }
  LOG ("[cluttertest] stage shown, running");
  if (phe_cmd_has ("registry"))
    LOG ("[cluttertest] registry test: frame process started (%d)", phe_frame_spawn ());
  clutter_main ();
  (void) err;
  return 0;
}
