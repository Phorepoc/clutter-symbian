/* cluttertest_main.c -- the Clutter feasibility test (docs hildon-port.md 7.33). BSD-2-Clause (Phorepoc).
 * One full-screen stage (the "symbian" backend's own window; presses move the turning actor, keys are logged, arrows move it), a textured actor rotating around its centre, a second textured actor with a ClutterBlurEffect (the offscreen + GLSL path hildon-desktop's task switcher uses), both turning for good;
 * every two seconds "[cluttertest] N frames in T ms = F fps" (frames counted at the stage's after-paint). CLUTTER_UNCAPPED: the master clock runs as fast as it can (frame rate 1000). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <clutter/clutter.h>

extern void phe_log (const char *msg);
#define LOG(...) do { char m_[300]; g_snprintf (m_, sizeof m_, __VA_ARGS__); phe_log (m_); } while (0)

static gint frames;
static gint64 t0;
static ClutterActor *spin, *blur;

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

int
phe_clutter_main (void)
{
  ClutterActor *stage; ClutterTimeline *tl; ClutterColor bg = { 20, 30, 50, 255 };
  GError *err = NULL; int width, height;
  LOG ("[cluttertest] clutter_init (Clutter %d.%d.%d)", CLUTTER_MAJOR_VERSION, CLUTTER_MINOR_VERSION, CLUTTER_MICRO_VERSION);
  if (clutter_init (NULL, NULL) != CLUTTER_INIT_SUCCESS) { LOG ("[cluttertest] clutter_init failed"); return 1; }
  if (g_getenv ("CLUTTER_UNCAPPED")) clutter_set_default_frame_rate (1000);
  proof_tess_and_png ();
  phe_clws_screen_size (&width, &height);
  stage = clutter_stage_new ();                            /* the backend makes its window: full screen, in the screen's current mode */
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
  LOG ("[cluttertest] stage shown, running");
  clutter_main ();
  (void) err;
  return 0;
}
