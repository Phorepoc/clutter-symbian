/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * phedecor.c -- the frame's window decorations (hildon-port.md C5, design 7.66): a title bar with minimise / maximise / close
 * buttons over each composited window, drawn as Clutter actors from the window registry's data (title, type, state, size hints,
 * ping state), in the Etesian tokens of phehildon.rc (title bar #3A3A3A, focused #1C51AF, 44 px touch targets). The buttons send
 * the registry's actions to the window's owner; a title-bar press focuses and raises the window. The frame pings every decorated
 * window every 2 s; a window whose oldest unanswered ping is older than 5 s is "not responding": its bar dims and says so, and its
 * close button then ends the owner process.
 *
 * Independent of the frame's compositing and pointer routing (cluttertest_main.c): it runs on the backend's registry events
 * (clutter_symbian_decor_hook) and a timer, and exports only phe_decor_hit () -- "is this stage point on a decoration" -- for the
 * frame's pointer code. */
#include <clutter/clutter.h>
#include <string.h>
#include <stdio.h>
#include "phelayout.h"

#define BAR_H 44
#define BTN_W 44
#define HUNG_MS 5000
#define LOG(...) do { char b_[256]; snprintf (b_, sizeof b_, __VA_ARGS__); phe_log (b_); } while (0)

typedef struct { int id, x, y, w, h, type, state, fixed, ping_pending, ping_age_ms, no_ping; char title[64]; } PheDecorWin;   /* cluttertest.cpp */
extern int phe_decor_query (PheDecorWin *out, int max);
extern int phe_decor_action (int id, int action);
extern int phe_decor_ping (int id);
extern int phe_decor_resize (int id, int x, int y, int w, int h);
extern void phe_decor_kill (int id);
extern void phe_log (const char *msg);
extern void phe_frame_relist (void);
extern void (*clutter_symbian_decor_hook) (int content);

enum { ACT_CLOSE = 1, ACT_MINIMISE = 2, ACT_MAXIMISE = 3, ACT_FOCUS = 4, ACT_RAISE = 5 };
enum { ACT_SPLIT = 100 };                  /* the frame's own: side by side with the next window (phelayout) */   /* TPheWinAction (phewinreg.h) */
enum { TYPE_NORMAL = 0, TYPE_DIALOG = 1 };                                                 /* TPheWinType */
enum { ST_FULLSCREEN = 1, ST_FOCUSED = 16 };

typedef struct {
  int id;
  ClutterActor *bar, *title, *bsplit, *bmin, *bmax, *bclose;
  ClutterActor *grip;           /* the resize grip: the window's bottom-right corner (a floating window only) */
  ClutterActor *eright, *ebottom;   /* the resize edges: 8 px strips over the window's right and bottom borders */
  float k;                      /* the scale the window is shown at (pointer deltas -> window pixels) */
  int x, y, w, h;               /* the window's geometry now */
  int sx, sy, sw, sh;           /* the geometry before a maximise (0 x 0 = not maximised) */
  gboolean hung, seen, no_ping;
  gboolean hung_logged;         /* the hung state last logged (every change is logged, outside the bar-line cap) */
} Decor;

static ClutterActor *stage;
static GList *decors;
static int logged;

/* a window gets a bar: a titled normal window or dialog, not full screen, wide enough for its buttons */
static gboolean
decorated (const PheDecorWin *w)
{
  return !(w->state & ST_FULLSCREEN) && (w->type == TYPE_NORMAL || w->type == TYPE_DIALOG) && w->w >= 3 * BTN_W
         && w->title[0] && w->title[0] != '<';
}

static GHashTable *decorated_ids;
/* the move policy: a floating window dragged by its title bar is shown moved by (dx, dy) -- the frame's placement, as a
 * compositor's (the owner keeps its own geometry; pointer input maps back through the moved actor). A maximised window and the
 * split layout's windows do not move; a restored window comes back to where it was moved */
typedef struct { int dx, dy; } Move;
static GHashTable *moves;             /* id -> Move* */
static int drag_id;                   /* the window being dragged (0 = none) */
static float drag_x0, drag_y0;        /* the press, stage pixels */
static int drag_dx0, drag_dy0;        /* its move at the press */
static gboolean movable (int id);
static Move *
move_of (int id)
{
  return moves && movable (id) ? g_hash_table_lookup (moves, GINT_TO_POINTER (id)) : NULL;
}
static GHashTable *minimised;         /* id -> 1: minimised while focused; 2: seen unfocused since; a focus after that restores it */

/* scale-to-fit: 1:1 when bar + window fit the screen, else min ((screen h - bar) / h, screen w / w) */
static float
fit (int w, int h)
{
  gfloat sw = 0, sh = 0, s = 1;
  if (stage)
    clutter_actor_get_size (stage, &sw, &sh);
  if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0)
    return 1;
  if ((sh - BAR_H) / h < s)
    s = (sh - BAR_H) / h;
  if (sw / w < s)
    s = sw / w;
  return s;
}

/* the frame's compositing: the scale to show window id at (1 = undecorated or fits) */
float
phe_decor_scale (int id, int w, int h)
{
  return decorated_ids && g_hash_table_contains (decorated_ids, GINT_TO_POINTER (id)) ? fit (w, h) : 1;
}

/* the frame's compositing: where window id's content goes (w x h = its size): PHE_PLACE_FREE (own geometry, phe_decor_content_y +
 * phe_decor_scale), PHE_PLACE_SET (*x, *y top-left, *k scale) or PHE_PLACE_HIDDEN (phelayout, hildon-port.md 7.51.17) */
int
phe_decor_place (int id, int w, int h, float *x, float *y, float *k)
{
  PheLayoutPlace pl;
  gfloat sw = 0, sh = 0;
  int r;
  if (stage)
    clutter_actor_get_size (stage, &sw, &sh);
  r = phe_layout_place (id, w, h, decorated_ids && g_hash_table_contains (decorated_ids, GINT_TO_POINTER (id)) ? BAR_H : 0,
                        (int) sw, (int) sh, &pl);
  if (r == PHE_PLACE_SET)
    {
      *x = pl.x; *y = pl.y; *k = pl.scale;
    }
  return r;
}

/* the frame's compositing: FALSE = window id is minimised (its content is not shown) */
int
phe_decor_shown (int id)
{
  return !minimised || !g_hash_table_contains (minimised, GINT_TO_POINTER (id));
}     /* id -> 1 for the windows decorated at the last refresh */

/* the frame's compositing: the y to show window id's content at (a decorated window below its bar, the bar on screen) */
int
phe_decor_content_y (int id, int y)
{
  const Move *m = move_of (id);
  if (m)
    y += m->dy;
  return decorated_ids && g_hash_table_contains (decorated_ids, GINT_TO_POINTER (id)) && y < BAR_H ? BAR_H : y;
}

/* the frame's compositing: the x to show window id's content at (its own x, plus a title-bar move) */
int
phe_decor_content_x (int id, int x)
{
  const Move *m = move_of (id);
  return m ? x + m->dx : x;
}

static ClutterActor *
find_stage (void)
{
  const GSList *l;
  if (stage)
    return stage;
  for (l = clutter_stage_manager_peek_stages (clutter_stage_manager_get_default ()); l; l = l->next)
    if (g_strcmp0 (clutter_stage_get_title (l->data), "frame") == 0)
      stage = l->data;
  return stage;
}

static Decor *
decor_of (int id)
{
  GList *l;
  for (l = decors; l; l = l->next)
    if (((Decor *) l->data)->id == id)
      return l->data;
  return NULL;
}

static void
colour (ClutterColor *c, unsigned rgb)
{
  c->red = rgb >> 16; c->green = (rgb >> 8) & 0xff; c->blue = rgb & 0xff; c->alpha = 255;
}

static void refresh (void);

/* a window moves when it is floating: decorated, not maximised (its pre-maximise geometry is not kept), and not in the split */
static gboolean
movable (int id)
{
  const Decor *d = decor_of (id);
  return d && !d->sw && phe_layout_mode () == PHE_LAYOUT_FREE;
}

static gboolean
on_bar_motion (ClutterActor *actor, ClutterEvent *event, gpointer data)
{
  gfloat ex, ey;
  Move *m;
  (void) data;
  if (!drag_id || drag_id != GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "phedecor-id")))
    return CLUTTER_EVENT_PROPAGATE;
  clutter_event_get_coords (event, &ex, &ey);
  m = g_hash_table_lookup (moves, GINT_TO_POINTER (drag_id));
  m->dx = drag_dx0 + (int) (ex - drag_x0);
  m->dy = drag_dy0 + (int) (ey - drag_y0);
  refresh ();
  phe_frame_relist ();
  return CLUTTER_EVENT_STOP;
}

static gboolean
on_bar_release (ClutterActor *actor, ClutterEvent *event, gpointer data)
{
  const Move *m;
  (void) data;
  if (!drag_id)
    return CLUTTER_EVENT_PROPAGATE;
  clutter_input_device_ungrab (clutter_event_get_device (event));
  m = g_hash_table_lookup (moves, GINT_TO_POINTER (drag_id));
  LOG ("[phedecor] window %d: moved by %d,%d", drag_id, m->dx, m->dy);
  drag_id = 0;
  (void) actor;
  return CLUTTER_EVENT_STOP;
}

/* resizing from the bottom-right corner: a drag on the grip asks the owner for the new size on release (phewinreg Resize, clamped
 * to the owner's min / max hints; the owner answers with Update). An outline follows the pointer meanwhile */
static int grip_id;
static int grip_axes;                 /* 1 = width (the right edge), 2 = height (the bottom edge), 3 = both (the corner) */
static float grip_x0, grip_y0;
static ClutterActor *grip_outline;

static gboolean
on_grip_press (ClutterActor *actor, ClutterEvent *event, gpointer data)
{
  const int id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "phedecor-id"));
  Decor *d = decor_of (id);
  ClutterColor c;
  (void) data;
  if (!d || !movable (id) || !clutter_event_get_device (event))
    return CLUTTER_EVENT_STOP;
  clutter_event_get_coords (event, &grip_x0, &grip_y0);
  grip_id = id;
  grip_axes = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "phedecor-axes"));
  if (!grip_outline)
    {
      grip_outline = clutter_actor_new ();
      colour (&c, 0x1C51AF);
      c.alpha = 0x60;
      clutter_actor_set_background_color (grip_outline, &c);
      clutter_actor_add_child (stage, grip_outline);
    }
  clutter_actor_set_position (grip_outline, clutter_actor_get_x (d->bar), clutter_actor_get_y (d->bar) + BAR_H);
  clutter_actor_set_size (grip_outline, d->w * d->k, d->h * d->k);
  clutter_actor_set_child_above_sibling (stage, grip_outline, NULL);
  clutter_actor_show (grip_outline);
  clutter_input_device_grab (clutter_event_get_device (event), actor);
  return CLUTTER_EVENT_STOP;
}

static gboolean
on_grip_motion (ClutterActor *actor, ClutterEvent *event, gpointer data)
{
  gfloat ex, ey;
  Decor *d = decor_of (grip_id);
  (void) actor; (void) data;
  if (!grip_id || !d)
    return CLUTTER_EVENT_PROPAGATE;
  clutter_event_get_coords (event, &ex, &ey);
  if (!(grip_axes & 1))
    ex = grip_x0;
  if (!(grip_axes & 2))
    ey = grip_y0;
  clutter_actor_set_size (grip_outline, MAX (3 * BTN_W, d->w * d->k + ex - grip_x0), MAX (BAR_H, d->h * d->k + ey - grip_y0));
  return CLUTTER_EVENT_STOP;
}

static gboolean
on_grip_release (ClutterActor *actor, ClutterEvent *event, gpointer data)
{
  gfloat ex, ey;
  Decor *d = decor_of (grip_id);
  int w, h, r;
  (void) actor; (void) data;
  if (!grip_id)
    return CLUTTER_EVENT_PROPAGATE;
  clutter_input_device_ungrab (clutter_event_get_device (event));
  clutter_actor_hide (grip_outline);
  if (d)
    {
      clutter_event_get_coords (event, &ex, &ey);
      if (!(grip_axes & 1))
        ex = grip_x0;
      if (!(grip_axes & 2))
        ey = grip_y0;
      w = (int) (d->w + (ex - grip_x0) / d->k);
      h = (int) (d->h + (ey - grip_y0) / d->k);
      if (w < 3 * BTN_W)
        w = 3 * BTN_W;
      if (h < 1)
        h = 1;
      r = phe_decor_resize (grip_id, d->x, d->y, w, h);
      LOG ("[phedecor] window %d: resize from the %s %dx%d -> %dx%d (%d)", grip_id,
           grip_axes == 1 ? "right edge" : grip_axes == 2 ? "bottom edge" : "corner", d->w, d->h, w, h, r);
    }
  grip_id = 0;
  return CLUTTER_EVENT_STOP;
}

static gboolean
on_button (ClutterActor *actor, ClutterEvent *event, gpointer data)
{
  int id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "phedecor-id"));
  int what = GPOINTER_TO_INT (data);
  Decor *d = decor_of (id);
  int r = 0;

  if (d == NULL)
    return CLUTTER_EVENT_STOP;
  switch (what)
    {
    case ACT_CLOSE:
      if (d->hung)
        phe_decor_kill (id);                /* not responding: the close button ends the owner */
      else
        r = phe_decor_action (id, ACT_CLOSE);
      break;
    case ACT_MINIMISE:
      /* the frame's policy (the owner leaves minimise to the frame): hidden here and in the compositing until focused again */
      r = phe_decor_action (id, ACT_MINIMISE);
      g_hash_table_insert (minimised, GINT_TO_POINTER (id), GINT_TO_POINTER (1));
      clutter_actor_hide (d->bar);
      phe_frame_relist ();
      break;
    case ACT_MAXIMISE:
      {
        gfloat sw, sh;
        clutter_actor_get_size (stage, &sw, &sh);
        if (d->sw)                          /* restore */
          {
            r = phe_decor_resize (id, d->sx, d->sy, d->sw, d->sh);
            d->sw = d->sh = 0;
          }
        else
          {
            d->sx = d->x; d->sy = d->y; d->sw = d->w; d->sh = d->h;
            r = phe_decor_resize (id, 0, BAR_H, (int) sw, (int) sh - BAR_H);   /* the work area below a title bar */
          }
        phe_decor_action (id, ACT_MAXIMISE);
        break;
      }
    case ACT_SPLIT:
      {
        /* split: this window on the left, the next decorated window on the right; pressed again (or one window): back to free */
        GList *l;
        int other = 0;
        if (phe_layout_mode () == PHE_LAYOUT_FREE)
          for (l = decors; l; l = l->next)
            if (((Decor *) l->data)->id != id)
              {
                other = ((Decor *) l->data)->id;
                break;
              }
        phe_layout_set_mode (other ? PHE_LAYOUT_SPLIT : PHE_LAYOUT_FREE, id, other);
        r = phe_layout_mode ();
        phe_frame_relist ();
        refresh ();
        break;
      }
    default:                                /* the title bar itself: focus + raise, and a drag moves a floating window */
      if (clutter_event_get_click_count (event) == 2 && phe_layout_mode () == PHE_LAYOUT_FREE)
        {
          /* a double click on the title = the maximise button (maximise / restore), as desktop window managers do */
          LOG ("[phedecor] window %d: title double click", id);
          return on_button (d->bmax, event, GINT_TO_POINTER (ACT_MAXIMISE));
        }
      r = phe_decor_action (id, ACT_FOCUS);
      phe_decor_action (id, ACT_RAISE);
      if (movable (id) && clutter_event_get_device (event))
        {
          Move *m;
          if (!moves)
            moves = g_hash_table_new_full (NULL, NULL, NULL, g_free);
          m = g_hash_table_lookup (moves, GINT_TO_POINTER (id));
          if (!m)
            g_hash_table_insert (moves, GINT_TO_POINTER (id), m = g_new0 (Move, 1));
          clutter_event_get_coords (event, &drag_x0, &drag_y0);
          drag_dx0 = m->dx; drag_dy0 = m->dy;
          drag_id = id;
          clutter_input_device_grab (clutter_event_get_device (event), actor);
        }
      break;
    }
  LOG ("[phedecor] window %d: %s -> %d", id, what == ACT_CLOSE ? (d->hung ? "close (kill: not responding)" : "close")
       : what == ACT_MINIMISE ? "minimise" : what == ACT_MAXIMISE ? "maximise" : what == ACT_SPLIT ? "split (mode)" : "focus", r);
  return CLUTTER_EVENT_STOP;
}

static ClutterActor *
button (Decor *d, const char *label, int what)
{
  ClutterColor fg;
  ClutterActor *b = clutter_actor_new (), *t;

  colour (&fg, 0xFFFFFF);
  if (label && label[0] == 1)
    {
      /* the split glyph: two outlined rectangles side by side (the UI font has no U+2016 / U+25EB) */
      int k;
      t = clutter_actor_new ();
      clutter_actor_set_size (t, 18, 14);
      for (k = 0; k < 2; k++)
        {
          ClutterActor *o = clutter_actor_new (), *in = clutter_actor_new ();
          clutter_actor_set_size (o, 8, 14);
          clutter_actor_set_position (o, k * 10, 0);
          clutter_actor_set_background_color (o, &fg);
          clutter_actor_set_size (in, 4, 10);
          clutter_actor_set_position (in, 2, 2);
          clutter_actor_add_child (o, in);
          clutter_actor_add_child (t, o);
          g_object_set_data (G_OBJECT (b), k ? "phedecor-inner2" : "phedecor-inner", in);
        }
    }
  else if (label)
    t = clutter_text_new_full ("Sans 16", label, &fg);
  else
    {
      /* the maximise glyph as an outlined square (the UI font has no U+25A1) */
      ClutterActor *in = clutter_actor_new ();
      t = clutter_actor_new ();
      clutter_actor_set_size (t, 14, 14);
      clutter_actor_set_background_color (t, &fg);
      clutter_actor_set_size (in, 10, 10);
      clutter_actor_set_position (in, 2, 2);
      clutter_actor_add_child (t, in);
      g_object_set_data (G_OBJECT (b), "phedecor-inner", in);
    }
  clutter_actor_set_size (b, BTN_W, BAR_H);
  clutter_actor_add_child (b, t);
  {
    /* centred in the button by its own size (an align constraint on the parent added the parent's offset twice) */
    gfloat tw, th;
    clutter_actor_get_preferred_size (t, NULL, NULL, &tw, &th);
    clutter_actor_set_position (t, (BTN_W - tw) / 2, (BAR_H - th) / 2);
  }
  clutter_actor_set_reactive (b, TRUE);
  g_object_set_data (G_OBJECT (b), "phedecor-id", GINT_TO_POINTER (d->id));
  g_signal_connect (b, "button-press-event", G_CALLBACK (on_button), GINT_TO_POINTER (what));
  clutter_actor_add_child (d->bar, b);
  return b;
}

/* a resize edge: a transparent reactive strip with the grip's handlers, resizing along one axis */
static ClutterActor *
edge_new (int id, int axes)
{
  ClutterActor *e = clutter_actor_new ();
  clutter_actor_set_reactive (e, TRUE);
  g_object_set_data (G_OBJECT (e), "phedecor-id", GINT_TO_POINTER (id));
  g_object_set_data (G_OBJECT (e), "phedecor-axes", GINT_TO_POINTER (axes));
  g_signal_connect (e, "button-press-event", G_CALLBACK (on_grip_press), NULL);
  g_signal_connect (e, "motion-event", G_CALLBACK (on_grip_motion), NULL);
  g_signal_connect (e, "button-release-event", G_CALLBACK (on_grip_release), NULL);
  clutter_actor_hide (e);
  return e;
}

static Decor *
decor_new (int id)
{
  Decor *d = g_new0 (Decor, 1);
  ClutterColor fg;

  d->id = id;
  d->bar = clutter_actor_new ();
  clutter_actor_set_reactive (d->bar, TRUE);
  g_object_set_data (G_OBJECT (d->bar), "phedecor-id", GINT_TO_POINTER (id));
  g_signal_connect (d->bar, "button-press-event", G_CALLBACK (on_button), GINT_TO_POINTER (0));
  g_signal_connect (d->bar, "motion-event", G_CALLBACK (on_bar_motion), NULL);
  {
    ClutterColor gc;
    d->grip = clutter_actor_new ();
    colour (&gc, 0x8A8A8A);
    clutter_actor_set_background_color (d->grip, &gc);
    clutter_actor_set_size (d->grip, 14, 14);
    clutter_actor_set_reactive (d->grip, TRUE);
    g_object_set_data (G_OBJECT (d->grip), "phedecor-id", GINT_TO_POINTER (id));
    g_signal_connect (d->grip, "button-press-event", G_CALLBACK (on_grip_press), NULL);
    g_signal_connect (d->grip, "motion-event", G_CALLBACK (on_grip_motion), NULL);
    g_signal_connect (d->grip, "button-release-event", G_CALLBACK (on_grip_release), NULL);
    g_object_set_data (G_OBJECT (d->grip), "phedecor-axes", GINT_TO_POINTER (3));
    clutter_actor_hide (d->grip);
    d->eright = edge_new (id, 1);
    d->ebottom = edge_new (id, 2);
  }
  g_signal_connect (d->bar, "button-release-event", G_CALLBACK (on_bar_release), NULL);
  colour (&fg, 0xFFFFFF);
  d->title = clutter_text_new_full ("Sans 16", "", &fg);
  clutter_actor_set_position (d->title, 12, (BAR_H - 22) / 2);
  clutter_text_set_ellipsize (CLUTTER_TEXT (d->title), PANGO_ELLIPSIZE_END);
  clutter_actor_add_child (d->bar, d->title);
  d->bsplit = button (d, "\x01", ACT_SPLIT);                /* two outlined rectangles: side by side */
  d->bmin = button (d, "\xe2\x80\x93", ACT_MINIMISE);       /* en dash */
  d->bmax = button (d, NULL, ACT_MAXIMISE);                 /* outlined square */
  d->bclose = button (d, "\xc3\x97", ACT_CLOSE);            /* multiplication sign */
  clutter_actor_add_child (stage, d->bar);
  clutter_actor_add_child (stage, d->grip);
  clutter_actor_add_child (stage, d->eright);
  clutter_actor_add_child (stage, d->ebottom);
  decors = g_list_append (decors, d);
  return d;
}

static void
decor_free (Decor *d)
{
  clutter_actor_destroy (d->bar);
  clutter_actor_destroy (d->grip);
  clutter_actor_destroy (d->eright);
  clutter_actor_destroy (d->ebottom);
  g_free (d);
}

/* the registry changed (or a ping tick): every decorated window's bar to its window's place, title, colour and buttons */
static void
refresh (void)
{
  PheDecorWin w[8];
  GList *l, *next;
  int n, i;

  if (find_stage () == NULL)
    return;
  n = phe_decor_query (w, 8);
  if (n < 0)
    return;
  for (l = decors; l; l = l->next)
    ((Decor *) l->data)->seen = FALSE;
  if (!decorated_ids)
    decorated_ids = g_hash_table_new (NULL, NULL);
  g_hash_table_remove_all (decorated_ids);
  for (i = 0; i < n; i++)
    if (decorated (&w[i]))
      g_hash_table_add (decorated_ids, GINT_TO_POINTER (w[i].id));
  {
    int ids[8];
    for (i = 0; i < n; i++)
      ids[i] = w[i].id;
    phe_layout_sync (ids, n);
  }
  for (i = 0; i < n; i++)
    {
      Decor *d;
      ClutterColor bg;
      char title[96];
      int bw = (int) (w[i].w * fit (w[i].w, w[i].h)), bx, by, barx = w[i].x, pr;
      PheLayoutPlace pl;
      gfloat sw = 0, sh = 0;
      if (!decorated (&w[i]))
        continue;                           /* full screen, menus, notifications, splash, untitled: no decoration */
      if (g_hash_table_contains (minimised, GINT_TO_POINTER (w[i].id)))
        {
          if (!(w[i].state & ST_FOCUSED))
            g_hash_table_insert (minimised, GINT_TO_POINTER (w[i].id), GINT_TO_POINTER (2));
          if (!(w[i].state & ST_FOCUSED) || GPOINTER_TO_INT (g_hash_table_lookup (minimised, GINT_TO_POINTER (w[i].id))) == 1)
            continue;                       /* still minimised: no bar (its Decor goes) */
          g_hash_table_remove (minimised, GINT_TO_POINTER (w[i].id));   /* focused again (a switcher): restored */
          LOG ("[phedecor] window %d restored (focused)", w[i].id);
        }
      clutter_actor_get_size (stage, &sw, &sh);
      pr = phe_layout_place (w[i].id, w[i].w, w[i].h, BAR_H, (int) sw, (int) sh, &pl);
      if (pr == PHE_PLACE_HIDDEN)
        continue;                           /* outside the split: no bar (its Decor goes) */
      d = decor_of (w[i].id);
      if (d == NULL)
        d = decor_new (w[i].id);
      d->seen = TRUE;
      d->x = w[i].x; d->y = w[i].y; d->w = w[i].w; d->h = w[i].h;
      d->no_ping = w[i].no_ping;
      d->hung = !w[i].no_ping && w[i].ping_pending && w[i].ping_age_ms > HUNG_MS;   /* not ping-capable: never hung */
      if (d->hung != d->hung_logged)
        {
          d->hung_logged = d->hung;
          LOG ("[phedecor] window %d '%s': %s (ping pending %d ms)", w[i].id, w[i].title,
               d->hung ? "NOT RESPONDING" : "responding again", w[i].ping_pending ? w[i].ping_age_ms : 0);
        }
      /* directly above the window, same width; the frame shows a window that starts under BAR_H lower (phe_decor_content_y) */
      by = phe_decor_content_y (w[i].id, w[i].y) - BAR_H;
      barx = phe_decor_content_x (w[i].id, w[i].x);
      if (pr == PHE_PLACE_SET)
        {
          barx = pl.bar_x; by = pl.bar_y; bw = pl.bar_w;
          static int last_[16];
          if (w[i].id > 0 && w[i].id < 16 && last_[w[i].id] != pl.x * 100000 + pl.h && (last_[w[i].id] = pl.x * 100000 + pl.h))
            LOG ("[phedecor] window %d split: %dx%d -> %d,%d %dx%d k %.3f", w[i].id, w[i].w, w[i].h, pl.x, pl.y, pl.w, pl.h,
               (double) pl.scale);
        }
      bx = bw - 3 * BTN_W;
      clutter_actor_show (d->bar);
      clutter_actor_set_position (d->bar, barx, by);
      clutter_actor_set_size (d->bar, bw, BAR_H);   /* the window's shown width */
      colour (&bg, d->hung ? 0x6A6A6A : (w[i].state & ST_FOCUSED) ? 0x1C51AF : 0x3A3A3A);
      clutter_actor_set_background_color (d->bar, &bg);
      /* the inner of the maximise square takes the bar colour (an outline); the title stops before the buttons */
      clutter_actor_set_background_color (g_object_get_data (G_OBJECT (d->bmax), "phedecor-inner"), &bg);
      clutter_actor_set_background_color (g_object_get_data (G_OBJECT (d->bsplit), "phedecor-inner"), &bg);
      clutter_actor_set_background_color (g_object_get_data (G_OBJECT (d->bsplit), "phedecor-inner2"), &bg);
      /* a narrow bar gives up the title first: the four buttons stay while they fit (4 x 44 = 176 px: a 180 px split half);
       * only below that the split button goes too (minimise / maximise / close always stay: decorated windows are >= 3 x 44) */
      snprintf (title, sizeof title, "%s%s", w[i].title, d->hung ? " (not responding)" : "");
      clutter_text_set_text (CLUTTER_TEXT (d->title), title);
      if (bx - BTN_W >= 12 + 40)
        {
          clutter_actor_set_width (d->title, bx - BTN_W - 12 - 8);
          clutter_actor_show (d->title);
        }
      else
        clutter_actor_hide (d->title);
      clutter_actor_set_position (d->bsplit, bx - BTN_W, 0);
      if (bx - BTN_W < 0)
        clutter_actor_hide (d->bsplit);      /* too narrow for a fourth button */
      else
        clutter_actor_show (d->bsplit);
      clutter_actor_set_position (d->bmin, bx, 0);
      clutter_actor_set_position (d->bmax, bx + BTN_W, 0);
      clutter_actor_set_position (d->bclose, bx + 2 * BTN_W, 0);
      if (w[i].fixed || w[i].type == TYPE_DIALOG)
        clutter_actor_hide (d->bmax);       /* a fixed-size window or a dialog does not maximise */
      else
        clutter_actor_show (d->bmax);
      if (w[i].type == TYPE_DIALOG)
        clutter_actor_hide (d->bmin);
      else
        clutter_actor_show (d->bmin);
      clutter_actor_set_child_above_sibling (stage, d->bar, NULL);   /* above every window actor */
      d->k = pr == PHE_PLACE_SET ? pl.scale : fit (w[i].w, w[i].h);
      if (movable (w[i].id) && !d->hung)
        {
          clutter_actor_set_position (d->grip, barx + bw - 14, by + BAR_H + (int) (w[i].h * d->k) - 14);
          clutter_actor_show (d->grip);
          clutter_actor_set_child_above_sibling (stage, d->grip, NULL);
          {
            /* the edges: 8 px strips centred on the right and bottom borders, short of the grip */
            const int sh = (int) (w[i].h * d->k);
            clutter_actor_set_position (d->eright, barx + bw - 4, by + BAR_H);
            clutter_actor_set_size (d->eright, 8, MAX (0, sh - 14));
            clutter_actor_set_position (d->ebottom, barx, by + BAR_H + sh - 4);
            clutter_actor_set_size (d->ebottom, MAX (0, bw - 14), 8);
            clutter_actor_show (d->eright);
            clutter_actor_show (d->ebottom);
            clutter_actor_set_child_above_sibling (stage, d->eright, NULL);
            clutter_actor_set_child_above_sibling (stage, d->ebottom, NULL);
          }
        }
      else
        {
          clutter_actor_hide (d->grip);
          clutter_actor_hide (d->eright);
          clutter_actor_hide (d->ebottom);
        }
      if (logged < 24)
        {
          logged++;
          LOG ("[phedecor] window %d '%s': bar at %d,%d %dx%d%s%s", w[i].id, w[i].title, w[i].x, by, w[i].w, BAR_H,
               (w[i].state & ST_FOCUSED) ? ", focused" : "", d->hung ? ", NOT RESPONDING" : "");
        }
    }
  for (l = decors; l; l = next)
    {
      next = l->next;
      if (!((Decor *) l->data)->seen)
        {
          decor_free (l->data);
          decors = g_list_delete_link (decors, l);
        }
    }
  phe_panel_refresh ();                     /* the frame's panel stays above every window and bar (phepanel.c) */
}

/* the panel's window list: a chosen window comes back from minimised */
void
phe_decor_unminimise (int id)
{
  if (minimised && g_hash_table_remove (minimised, GINT_TO_POINTER (id)))
    {
      LOG ("[phedecor] window %d restored (window list)", id);
      phe_frame_relist ();
    }
}

extern void phe_panel_refresh (void);

static void
on_registry (int content)
{
  if (!content)
    refresh ();
}

/* every 2 s: ping each decorated window, then re-read the ping ages (a hung owner's age only grows; no registry event says so) */
static gboolean
ping_tick (gpointer data)
{
  GList *l;
  for (l = decors; l; l = l->next)
    if (!((Decor *) l->data)->no_ping)
      phe_decor_ping (((Decor *) l->data)->id);
  refresh ();
  return G_SOURCE_CONTINUE;
}

/* the frame's pointer code: the id of the window whose decoration is at stage point x,y, else 0 */
int
phe_decor_hit (float x, float y)
{
  GList *l;
  for (l = decors; l; l = l->next)
    {
      Decor *d = l->data;
      gfloat bx, by;
      if (!clutter_actor_is_visible (d->bar))
        continue;
      clutter_actor_get_position (d->bar, &bx, &by);
      if (x >= bx && x < bx + clutter_actor_get_width (d->bar) && y >= by && y < by + BAR_H)
        return d->id;
    }
  return 0;
}

extern int phe_cmd_has (const char *word);

/* the split control, pressed by the frame's command line (a scripted run has no pointer): the first decorated window on the
 * left, the next on the right, as on_button's ACT_SPLIT; once */
static gboolean
split_tick (gpointer data)
{
  (void) data;
  if (g_list_length (decors) < 2)
    return G_SOURCE_CONTINUE;
  phe_layout_set_mode (PHE_LAYOUT_SPLIT, ((Decor *) decors->data)->id, ((Decor *) decors->next->data)->id);
  LOG ("[phedecor] split (command line): %d | %d -> %d", ((Decor *) decors->data)->id, ((Decor *) decors->next->data)->id,
       phe_layout_mode ());
  phe_frame_relist ();
  refresh ();
  return G_SOURCE_REMOVE;
}

void
phe_decor_install (void)
{
  minimised = g_hash_table_new (NULL, NULL);
  clutter_symbian_decor_hook = on_registry;
  g_timeout_add (2000, ping_tick, NULL);
  if (phe_cmd_has ("split"))
    g_timeout_add (2000, split_tick, NULL);   /* "frame-split": the split control pressed once two windows are decorated */
}
