/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * phelayout.c -- the frame's window placement modes.
 *
 * The frame (frame_show) and its decorations (phedecor.c) ask this module where a window goes. In the FREE mode (the default)
 * the module places nothing: windows keep their own geometry and the decorations' scale-to-fit. In the SPLIT mode two windows
 * share the screen side by side. Each gets one half and is scaled to fit it under its title bar. Its aspect ratio is kept, it is
 * centred horizontally, and its bar sits on top. The other windows are not shown while the split lasts.
 *
 * The decorations choose the mode (a split control, a key, a drag to an edge: their call); this module only does the geometry
 * and remembers which windows are split. The split ends by itself when one of its two windows leaves the registry (phe_layout_sync).
 * No Clutter here: plain arithmetic, so a host test can drive it.
 */
#include "phelayout.h"

static int mode = PHE_LAYOUT_FREE;
static int split_ids[2];

void
phe_layout_set_mode (int new_mode, int left_id, int right_id)
{
  if (new_mode == PHE_LAYOUT_SPLIT && left_id > 0 && right_id > 0 && left_id != right_id)
    {
      mode = PHE_LAYOUT_SPLIT;
      split_ids[0] = left_id;
      split_ids[1] = right_id;
      return;
    }
  mode = PHE_LAYOUT_FREE;
  split_ids[0] = split_ids[1] = 0;
}

int
phe_layout_mode (void)
{
  return mode;
}

int
phe_layout_split_id (int side)
{
  return mode == PHE_LAYOUT_SPLIT && (side == 0 || side == 1) ? split_ids[side] : 0;
}

void
phe_layout_sync (const int *ids, int n)
{
  int side, i, found;
  if (mode != PHE_LAYOUT_SPLIT)
    return;
  for (side = 0; side < 2; side++)
    {
      for (found = 0, i = 0; i < n && !found; i++)
        found = ids[i] == split_ids[side];
      if (!found)
        {
          phe_layout_set_mode (PHE_LAYOUT_FREE, 0, 0);
          return;
        }
    }
}

int
phe_layout_place (int id, int w, int h, int bar_h, int screen_w, int screen_h, PheLayoutPlace *out)
{
  int side, tile_x, tile_w, area_h;
  float kx, ky;
  if (mode != PHE_LAYOUT_SPLIT)
    return PHE_PLACE_FREE;
  side = id == split_ids[0] ? 0 : id == split_ids[1] ? 1 : -1;
  if (side < 0)
    return PHE_PLACE_HIDDEN;
  if (w <= 0 || h <= 0 || screen_w <= 1 || screen_h <= bar_h)
    return PHE_PLACE_FREE;
  tile_w = screen_w / 2;
  tile_x = side * tile_w;
  if (side == 1)
    tile_w = screen_w - tile_w;           /* an odd width: the right half takes the extra column */
  area_h = screen_h - bar_h;
  kx = (float) tile_w / w;
  ky = (float) area_h / h;
  out->scale = kx < ky ? kx : ky;
  out->w = (int) (w * out->scale);
  out->h = (int) (h * out->scale);
  out->x = tile_x + (tile_w - out->w) / 2;
  out->y = bar_h;
  out->bar_x = out->x;
  out->bar_y = 0;
  out->bar_w = out->w;
  return PHE_PLACE_SET;
}
