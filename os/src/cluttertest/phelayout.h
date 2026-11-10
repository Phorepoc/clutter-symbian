/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * phelayout.h -- the frame's window placement interface. See phelayout.c.
 */
#ifndef PHELAYOUT_H
#define PHELAYOUT_H

enum { PHE_LAYOUT_FREE = 0, PHE_LAYOUT_SPLIT = 1 };
/* phe_layout_place results */
enum { PHE_PLACE_FREE = 0,      /* the layout does not place this window: keep its own geometry / scale-to-fit */
       PHE_PLACE_SET = 1,       /* *out is where it goes */
       PHE_PLACE_HIDDEN = 2 };  /* not shown in this mode (a window outside the split) */

typedef struct
{
  int x, y;           /* the content's top-left on the stage (the scale pivot is this corner) */
  int w, h;           /* the content's shown size (= its size * scale) */
  float scale;        /* the same in x and y: the aspect ratio is kept */
  int bar_x, bar_y, bar_w;   /* the title bar: directly above the content, the content's shown width */
} PheLayoutPlace;

/* PHE_LAYOUT_SPLIT with two different window ids (left, right), or PHE_LAYOUT_FREE (the ids are ignored). A split call with a
 * bad pair falls back to FREE */
void phe_layout_set_mode (int mode, int left_id, int right_id);
int phe_layout_mode (void);
/* the split's window on side 0 (left) or 1 (right); 0 when not split */
int phe_layout_split_id (int side);
/* the registry's current window ids (each relist): a split whose window is gone ends (back to FREE) */
void phe_layout_sync (const int *ids, int n);
/* where window id goes: its content size w x h, its bar height (0 = undecorated) and the stage size */
int phe_layout_place (int id, int w, int h, int bar_h, int screen_w, int screen_h, PheLayoutPlace *out);

#endif
