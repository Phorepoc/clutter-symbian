/* cluttertest_main.c -- the Clutter feasibility test (docs hildon-port.md 7.33). BSD-2-Clause (Phorepoc).
 * One stage, a textured actor rotating around its centre, a second textured actor with a ClutterBlurEffect (the offscreen + GLSL path hildon-desktop's task switcher uses), both turning for good;
 * every two seconds "[cluttertest] N frames in T ms = F fps" (frames counted at the stage's after-paint). CLUTTER_UNCAPPED: the master clock runs as fast as it can (frame rate 1000). */
#include <stdio.h>
#include <string.h>
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

static gboolean
uncap (gpointer data)
{
  (void) data;
  clutter_set_default_frame_rate (1000);                  /* the master clock no longer waits for the next 1/60 s */
  LOG ("[cluttertest] --- uncapped (frame rate 1000) ---");
  frames = 0; t0 = g_get_monotonic_time ();
  return FALSE;
}

static void
on_frame (ClutterTimeline *tl, gint msecs, gpointer data)
{
  gdouble a = msecs * 360.0 / 4000.0;
  (void) tl; (void) data;
  clutter_actor_set_rotation_angle (spin, CLUTTER_Z_AXIS, a);
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

int
phe_clutter_main (int width, int height)
{
  ClutterActor *stage; ClutterTimeline *tl; ClutterColor bg = { 20, 30, 50, 255 };
  GError *err = NULL;
  LOG ("[cluttertest] clutter_init (Clutter %d.%d.%d)", CLUTTER_MAJOR_VERSION, CLUTTER_MINOR_VERSION, CLUTTER_MICRO_VERSION);
  if (clutter_init (NULL, NULL) != CLUTTER_INIT_SUCCESS) { LOG ("[cluttertest] clutter_init failed"); return 1; }
  if (g_getenv ("CLUTTER_UNCAPPED")) clutter_set_default_frame_rate (1000);
  stage = clutter_stage_new ();
  clutter_actor_set_size (stage, width, height);
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
  if (!g_getenv ("CLUTTER_UNCAPPED")) g_timeout_add_seconds (14, uncap, NULL);          /* 14 s at the default 60 fps cap, then as fast as it goes */
  clutter_timeline_start (tl);
  clutter_actor_show (stage);
  LOG ("[cluttertest] stage shown, running");
  clutter_main ();
  (void) err;
  return 0;
}
