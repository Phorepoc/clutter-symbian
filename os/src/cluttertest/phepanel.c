/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * phepanel.c -- the frame's panel (hildon-port.md 7.82): a 44 px button in the stage's bottom-left corner, above every window,
 * that opens the window list: every titled window the frame lists, minimised ones included; a tap restores, focuses and raises
 * it. The list closes on a second press of the button or after a choice. Etesian tokens as phedecor.c (#3A3A3A, focused #1C51AF). */
#include <clutter/clutter.h>
#include <string.h>
#include <stdio.h>

#define BTN 44
#define LOG(...) do { char b_[256]; snprintf (b_, sizeof b_, __VA_ARGS__); phe_log (b_); } while (0)

typedef struct { int id, x, y, w, h, type, state, fixed, ping_pending, ping_age_ms, no_ping; char title[64]; } PheDecorWin;   /* cluttertest.cpp */
extern int phe_decor_query (PheDecorWin *out, int max);
extern int phe_decor_action (int id, int action);
extern void phe_decor_unminimise (int id);
extern void phe_log (const char *msg);

enum { ACT_FOCUS = 4, ACT_RAISE = 5 };
enum { ST_FOCUSED = 16 };

static ClutterActor *stage, *panel, *popup;
static int popup_kind;                  /* 0 = none, 2 = window list */

static void
colour (ClutterColor *c, unsigned rgb)
{
  c->red = rgb >> 16; c->green = (rgb >> 8) & 0xFF; c->blue = rgb & 0xFF; c->alpha = 0xFF;
}

static void
popup_close (void)
{
  if (popup)
    clutter_actor_destroy (popup);
  popup = NULL;
  popup_kind = 0;
}

static void
raise_all (void)
{
  if (popup)
    clutter_actor_set_child_above_sibling (stage, popup, NULL);
  clutter_actor_set_child_above_sibling (stage, panel, NULL);
}

static gboolean
on_window (ClutterActor *actor, ClutterEvent *event, gpointer data)
{
  int id = GPOINTER_TO_INT (data);
  (void) actor; (void) event;
  phe_decor_unminimise (id);
  LOG ("[phepanel] window list: window %d -> focus %d", id, phe_decor_action (id, ACT_FOCUS));
  phe_decor_action (id, ACT_RAISE);
  popup_close ();
  return CLUTTER_EVENT_STOP;
}

static ClutterActor *
popup_new (float w, float h)
{
  ClutterColor bg;
  gfloat sh;
  ClutterActor *p = clutter_actor_new ();
  colour (&bg, 0x2A2A2A);
  clutter_actor_set_background_color (p, &bg);
  clutter_actor_set_size (p, w, h);
  clutter_actor_get_size (stage, NULL, &sh);
  clutter_actor_set_position (p, 0, sh - BTN - h);
  clutter_actor_set_reactive (p, TRUE);   /* a tap between the entries stays in the popup */
  clutter_actor_add_child (stage, p);
  return p;
}

static void
show_windows (void)
{
  PheDecorWin w[16];
  int n = phe_decor_query (w, 16), i, k = 0;
  ClutterColor fg, focused;

  if (n <= 0)
    return;
  popup = popup_new (360, 0);
  popup_kind = 2;
  colour (&fg, 0xFFFFFF); colour (&focused, 0x1C51AF);
  for (i = 0; i < n; i++)
    {
      ClutterActor *row, *t;
      if (!w[i].title[0] || w[i].title[0] == '<')
        continue;                         /* menus, notifications, splash: not windows a user switches to */
      row = clutter_actor_new ();
      clutter_actor_set_size (row, 360, BTN);
      clutter_actor_set_position (row, 0, k * BTN);
      if (w[i].state & ST_FOCUSED)
        clutter_actor_set_background_color (row, &focused);
      t = clutter_text_new_full ("Sans 16", w[i].title, &fg);
      clutter_text_set_ellipsize (CLUTTER_TEXT (t), PANGO_ELLIPSIZE_END);
      clutter_actor_set_width (t, 360 - 24);
      clutter_actor_set_position (t, 12, (BTN - 22) / 2);
      clutter_actor_add_child (row, t);
      clutter_actor_set_reactive (row, TRUE);
      g_signal_connect (row, "button-press-event", G_CALLBACK (on_window), GINT_TO_POINTER (w[i].id));
      clutter_actor_add_child (popup, row);
      k++;
    }
  {
    gfloat sh;
    clutter_actor_get_size (stage, NULL, &sh);
    clutter_actor_set_height (popup, k * BTN);
    clutter_actor_set_y (popup, sh - BTN - k * BTN);
  }
  LOG ("[phepanel] window list: %d windows", k);
  raise_all ();
}

static gboolean
on_panel_button (ClutterActor *actor, ClutterEvent *event, gpointer data)
{
  int kind = GPOINTER_TO_INT (data), was = popup_kind;
  (void) actor; (void) event;
  popup_close ();
  if (was != kind)
    show_windows ();
  return CLUTTER_EVENT_STOP;
}

static ClutterActor *
panel_button (int kind, float x)
{
  ClutterColor fg;
  ClutterActor *b = clutter_actor_new ();
  int i;
  colour (&fg, 0xFFFFFF);
  clutter_actor_set_size (b, BTN, BTN);
  clutter_actor_set_position (b, x, 0);
  for (i = 0; i < 3; i++)
    {
      /* the window list's glyph: three stacked lines */
      ClutterActor *g = clutter_actor_new ();
      clutter_actor_set_background_color (g, &fg);
      clutter_actor_set_size (g, 20, 3);
      clutter_actor_set_position (g, 12, 14 + i * 7);
      clutter_actor_add_child (b, g);
    }
  clutter_actor_set_reactive (b, TRUE);
  g_signal_connect (b, "button-press-event", G_CALLBACK (on_panel_button), GINT_TO_POINTER (kind));
  clutter_actor_add_child (panel, b);
  return b;
}

/* keeps the panel above the windows (a new window actor goes on top); called on every registry change */
void
phe_panel_refresh (void)
{
  gfloat sh;
  if (!panel)
    {
      ClutterColor bg;
      GSList *s = clutter_stage_manager_list_stages (clutter_stage_manager_get_default ());
      stage = s ? s->data : NULL;
      g_slist_free (s);
      if (!stage)
        return;
      panel = clutter_actor_new ();
      colour (&bg, 0x3A3A3A);
      clutter_actor_set_background_color (panel, &bg);
      clutter_actor_set_size (panel, BTN, BTN);
      panel_button (2, 0);
      clutter_actor_add_child (stage, panel);
      LOG ("[phepanel] panel up: window list");
    }
  clutter_actor_get_size (stage, NULL, &sh);
  clutter_actor_set_position (panel, 0, sh - BTN);
  raise_all ();
}
