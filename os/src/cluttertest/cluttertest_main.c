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
 * a helper thread blocks on the registry (phe_frame_next, cluttertest.cpp) and hands each imported window to the main loop,
 * which shows it as a texture (ClutterImage) on this process's own small stage */
extern int phe_frame_next (unsigned *seen, int *id, int *w, int *h, unsigned char **pixels, int *token, char *title, int title_len);
extern void phe_frame_free (unsigned char *pixels);
extern int phe_frame_spawn (void);
extern int phe_is_frame (void);
extern int phe_cmd_has (const char *word);

typedef struct { int id, w, h, token; unsigned char *px; char title[64]; } FrameImport;
static ClutterActor *frame_view;
static int frame_imports;

/* the GPU path (hildon-port.md 7.47): the window's exported texture, imported once per token (EGL_SYMBIAN_SVGA_SHARE_PH) and shown 1:1
   (the frame's top-left corner) on a texture actor, flipped (the exporter copies GL rows: its row 0 is the frame's bottom). Each content
   update is only a redraw: the texture is the window's, on the GPU */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cogl/cogl-egl.h>
#define EGL_SYMBIAN_SVGA_SHARE_PH 0x3FF2
CoglTexture2D *_cogl_egl_texture_2d_new_from_image (CoglContext *ctx, int width, int height, CoglPixelFormat format,
                                                    EGLImageKHR image, CoglError **error);
static ClutterActor *frame_gpu_view;
static int frame_gpu_token, frame_gpu_shows, frame_gpu_checks;
static EGLImageKHR frame_gpu_image;

static void
frame_gpu_check (ClutterActor *stage, gpointer data)
{
  /* the pixel check (as the bitmap path's import log): the frame stage's (10,10) after a paint = the window's (10,10) */
  guchar px[4];
  (void) data; (void) stage;
  if (frame_gpu_checks >= 3 || !frame_gpu_token)
    return;
  frame_gpu_checks++;
  cogl_framebuffer_read_pixels (cogl_get_draw_framebuffer (), 10, 10, 1, 1, COGL_PIXEL_FORMAT_BGRA_8888_PRE, px);
  LOG ("[cluttertest-frame] GPU import check #%d: frame stage pixel(10,10) = %02x%02x%02x%02x", frame_gpu_checks, px[3], px[2], px[1], px[0]);
}

static gboolean
frame_show_gpu (FrameImport *f)
{
  if (f->token != frame_gpu_token)
    {
      CoglContext *ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
      EGLDisplay dpy = cogl_egl_context_get_egl_display (ctx);
      PFNEGLCREATEIMAGEKHRPROC create = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress ("eglCreateImageKHR");
      PFNEGLDESTROYIMAGEKHRPROC destroy = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress ("eglDestroyImageKHR");
      EGLint attrs[] = { EGL_WIDTH, f->w, EGL_HEIGHT, f->h, EGL_NONE };
      EGLImageKHR img = create (dpy, EGL_NO_CONTEXT, EGL_SYMBIAN_SVGA_SHARE_PH, (EGLClientBuffer) (intptr_t) f->token, attrs);
      CoglError *err = NULL;
      CoglTexture2D *tex = img != EGL_NO_IMAGE_KHR ?
        _cogl_egl_texture_2d_new_from_image (ctx, f->w, f->h, COGL_PIXEL_FORMAT_BGRA_8888_PRE, img, &err) : NULL;
      if (!tex)
        {
          LOG ("[cluttertest-frame] GPU import of token %d failed (EGL %x, %s)", f->token, eglGetError (), err ? err->message : "-");
          if (err) cogl_error_free (err);
          if (img != EGL_NO_IMAGE_KHR) destroy (dpy, img);
          return FALSE;
        }
      clutter_texture_set_cogl_texture (CLUTTER_TEXTURE (frame_gpu_view), COGL_TEXTURE (tex));
      cogl_object_unref (tex);
      if (frame_gpu_image)
        destroy (dpy, frame_gpu_image);
      frame_gpu_image = img;
      clutter_actor_set_size (frame_gpu_view, f->w, f->h);
      clutter_actor_set_pivot_point (frame_gpu_view, 0.5, 0.5);
      clutter_actor_set_scale (frame_gpu_view, 1.0, -1.0);
      clutter_actor_hide (frame_view);
      clutter_actor_show (frame_gpu_view);
      frame_gpu_token = f->token;
      frame_gpu_checks = 0;
      LOG ("[cluttertest-frame] GPU import: window %d '%s' %dx%d, token %d, as a texture (no copy)", f->id, f->title, f->w, f->h, f->token);
    }
  frame_gpu_shows++;
  clutter_actor_queue_redraw (frame_gpu_view);
  return TRUE;
}

static gboolean
frame_show (gpointer data)
{
  FrameImport *f = data;
  ClutterContent *img = clutter_actor_get_content (frame_view);
  GError *err = NULL;

  if (f->token)
    {
      frame_show_gpu (f);
      g_free (f);
      return G_SOURCE_REMOVE;
    }
  if (frame_gpu_token)
    {
      clutter_actor_hide (frame_gpu_view);
      clutter_actor_show (frame_view);
      frame_gpu_token = 0;
    }

  if (clutter_image_set_data (CLUTTER_IMAGE (img), f->px, COGL_PIXEL_FORMAT_BGRA_8888_PRE, f->w, f->h, f->w * 4, &err))
    {
      if (frame_imports++ < 3 || frame_imports % 100 == 0)
        LOG ("[cluttertest-frame] import #%d: window %d '%s' %dx%d as a texture, pixel(10,10) = %08x", frame_imports, f->id, f->title, f->w, f->h,
             f->w > 10 && f->h > 10 ? ((unsigned *) f->px)[10 * f->w + 10] : 0);
    }
  else
    LOG ("[cluttertest-frame] import failed: %s", err ? err->message : "?");
  g_clear_error (&err);
  phe_frame_free (f->px);
  g_free (f);
  return G_SOURCE_REMOVE;
}

static gpointer
frame_thread (gpointer data)
{
  unsigned seen = 0;
  (void) data;
  for (;;)
    {
      FrameImport *f = g_new0 (FrameImport, 1);
      int r = phe_frame_next (&seen, &f->id, &f->w, &f->h, &f->px, &f->token, f->title, sizeof f->title);
      if (r < 0) { LOG ("[cluttertest-frame] registry: %d", r); g_free (f); return NULL; }
      if (r == 0) { g_free (f); continue; }
      g_idle_add (frame_show, f);
    }
}

static int
frame_main (void)
{
  ClutterActor *stage;
  ClutterColor bg = { 30, 30, 30, 255 };

  if (clutter_init (NULL, NULL) != CLUTTER_INIT_SUCCESS) return 1;
  stage = clutter_stage_new ();
  clutter_stage_set_title (CLUTTER_STAGE (stage), "frame");
  clutter_actor_set_background_color (stage, &bg);
  clutter_actor_set_size (stage, 120, 200);
  frame_view = clutter_actor_new ();
  clutter_actor_set_content (frame_view, clutter_image_new ());
  clutter_actor_set_size (frame_view, 120, 200);
  clutter_actor_add_child (stage, frame_view);
  frame_gpu_view = clutter_texture_new ();
  clutter_actor_hide (frame_gpu_view);
  clutter_actor_add_child (stage, frame_gpu_view);
  g_signal_connect (stage, "after-paint", G_CALLBACK (frame_gpu_check), NULL);
  clutter_actor_show (stage);
  LOG ("[cluttertest-frame] up: waiting for windows in the registry");
  g_thread_new ("frame-registry", frame_thread, NULL);
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
