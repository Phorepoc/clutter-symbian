/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * cogl-winsys-egl-symbian.c -- Cogl's EGL platform for the Symbian window server (clutter-symbian). Derived from Cogl 1.22.8's cogl-winsys-egl-null.c (the renderer
 * connection) and cogl-winsys-egl-x11.c / -wayland.c (one native window and EGL surface per onscreen, a dummy surface for the context, visibility, resize), with the
 * windows created through clutter-symbian's WSERV layer (backend/phe_clws.h): an RWindow is EGLNativeWindowType for Mesa's EGL on this system (core's symgl_window).
 *
 * Copyright (C) 2011 Intel Corporation (the files this is derived from); the Symbian platform: The Phorepoc Authors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
 * OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cogl-winsys-egl-private.h"
#include "cogl-renderer-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-onscreen-private.h"
#include "cogl-error-private.h"
#include "cogl-context-private.h"
#include "cogl-flags.h"
#include "phe_clws.h"

const CoglWinsysVtable *_cogl_winsys_egl_symbian_get_vtable (void);
void *cogl_symbian_onscreen_get_window (CoglOnscreen *onscreen);
void cogl_symbian_onscreen_resize (CoglOnscreen *onscreen, int x, int y, int width, int height);
void cogl_symbian_onscreen_set_composited (CoglOnscreen *onscreen, CoglBool composited);

static const CoglWinsysEGLVtable _cogl_winsys_egl_vtable;

typedef struct _CoglDisplaySymbian
{
  void *dummy_window;          /* 1x1, never shown: the surface the context is current on while no onscreen is (as egl-x11's dummy window) */
} CoglDisplaySymbian;

typedef struct _CoglOnscreenSymbian
{
  void *window;                /* the RWindow of this onscreen */
  CoglBool visible;            /* as the application asked */
  CoglBool composited;         /* a frame process composites this window from its exported texture (hildon-port.md 7.48): the window stays
                                  off the window server's screen and a swap presents nothing (the frame was published after the paint) */
} CoglOnscreenSymbian;

static void
_cogl_winsys_renderer_disconnect (CoglRenderer *renderer)
{
  CoglRendererEGL *egl_renderer = renderer->winsys;

  eglTerminate (egl_renderer->edpy);

  g_slice_free (CoglRendererEGL, egl_renderer);
}

static CoglBool
_cogl_winsys_renderer_connect (CoglRenderer *renderer,
                               CoglError **error)
{
  CoglRendererEGL *egl_renderer;
  int r;

  if ((r = phe_clws_init ()) != 0)
    {
      _cogl_set_error (error, COGL_WINSYS_ERROR, COGL_WINSYS_ERROR_INIT, "Unable to connect to the window server (%d)", r);
      return FALSE;
    }

  renderer->winsys = g_slice_new0 (CoglRendererEGL);
  egl_renderer = renderer->winsys;

  egl_renderer->platform_vtable = &_cogl_winsys_egl_vtable;

  egl_renderer->edpy = eglGetDisplay (EGL_DEFAULT_DISPLAY);

  if (!_cogl_winsys_egl_renderer_connect_common (renderer, error))
    goto error;

  return TRUE;

error:
  _cogl_winsys_renderer_disconnect (renderer);
  return FALSE;
}

static CoglBool
_cogl_winsys_egl_display_setup (CoglDisplay *display,
                                CoglError **error)
{
  CoglDisplayEGL *egl_display = display->winsys;

  egl_display->platform = g_slice_new0 (CoglDisplaySymbian);

  return TRUE;
}

static void
_cogl_winsys_egl_display_destroy (CoglDisplay *display)
{
  CoglDisplayEGL *egl_display = display->winsys;

  g_slice_free (CoglDisplaySymbian, egl_display->platform);
}

static CoglBool
_cogl_winsys_egl_context_created (CoglDisplay *display,
                                  CoglError **error)
{
  CoglRenderer *renderer = display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglDisplaySymbian *sym_display = egl_display->platform;
  const char *error_message;

  sym_display->dummy_window = phe_clws_window_new (0, 0, 1, 1, FALSE);
  if (sym_display->dummy_window == NULL)
    {
      error_message = "Unable to create the dummy window";
      goto fail;
    }

  egl_display->dummy_surface =
    eglCreateWindowSurface (egl_renderer->edpy,
                            egl_display->egl_config,
                            (EGLNativeWindowType) sym_display->dummy_window,
                            NULL);
  if (egl_display->dummy_surface == EGL_NO_SURFACE)
    {
      error_message = "Unable to create an EGL surface";
      goto fail;
    }

  if (!_cogl_winsys_egl_make_current (display,
                                      egl_display->dummy_surface,
                                      egl_display->dummy_surface,
                                      egl_display->egl_context))
    {
      error_message = "Unable to eglMakeCurrent with dummy surface";
      goto fail;
    }

  return TRUE;

 fail:
  _cogl_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_CONTEXT,
                   "%s", error_message);
  return FALSE;
}

/* every onscreen has its own window and surface: several at once (as egl-x11's context_init) */
static CoglBool
_cogl_winsys_egl_context_init (CoglContext *context,
                               CoglError **error)
{
  context->feature_flags |= COGL_FEATURE_ONSCREEN_MULTIPLE;
  COGL_FLAGS_SET (context->features, COGL_FEATURE_ID_ONSCREEN_MULTIPLE, TRUE);
  COGL_FLAGS_SET (context->winsys_features, COGL_WINSYS_FEATURE_MULTIPLE_ONSCREEN, TRUE);
  return TRUE;
}

static void
_cogl_winsys_egl_cleanup_context (CoglDisplay *display)
{
  CoglRenderer *renderer = display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglDisplaySymbian *sym_display = egl_display->platform;

  if (egl_display->dummy_surface != EGL_NO_SURFACE)
    {
      eglDestroySurface (egl_renderer->edpy, egl_display->dummy_surface);
      egl_display->dummy_surface = EGL_NO_SURFACE;
    }

  if (sym_display->dummy_window)
    {
      phe_clws_window_destroy (sym_display->dummy_window);
      sym_display->dummy_window = NULL;
    }
}

static CoglBool
_cogl_winsys_egl_onscreen_init (CoglOnscreen *onscreen,
                                EGLConfig egl_config,
                                CoglError **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = framebuffer->context;
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;
  CoglOnscreenSymbian *sym_onscreen;
  EGLint w = 0, h = 0;

  sym_onscreen = g_slice_new0 (CoglOnscreenSymbian);
  egl_onscreen->platform = sym_onscreen;

  /* the window is created hidden; cogl_onscreen_show () makes it visible (as an X window is mapped) */
  sym_onscreen->window = phe_clws_window_new (0, 0,
                                              cogl_framebuffer_get_width (framebuffer),
                                              cogl_framebuffer_get_height (framebuffer),
                                              FALSE);
  if (sym_onscreen->window == NULL)
    {
      _cogl_set_error (error, COGL_WINSYS_ERROR, COGL_WINSYS_ERROR_CREATE_ONSCREEN, "Unable to create the window");
      return FALSE;
    }

  egl_onscreen->egl_surface =
    eglCreateWindowSurface (egl_renderer->edpy,
                            egl_config,
                            (EGLNativeWindowType) sym_onscreen->window,
                            NULL);
  if (egl_onscreen->egl_surface == EGL_NO_SURFACE)
    {
      _cogl_set_error (error, COGL_WINSYS_ERROR, COGL_WINSYS_ERROR_CREATE_ONSCREEN, "Unable to create the EGL window surface");
      return FALSE;
    }

  eglQuerySurface (egl_renderer->edpy, egl_onscreen->egl_surface, EGL_WIDTH, &w);
  eglQuerySurface (egl_renderer->edpy, egl_onscreen->egl_surface, EGL_HEIGHT, &h);
  if (w > 0 && h > 0)
    _cogl_framebuffer_winsys_update_size (framebuffer, w, h);

  return TRUE;
}

static void
_cogl_winsys_egl_onscreen_deinit (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;
  CoglOnscreenSymbian *sym_onscreen = egl_onscreen->platform;

  /* the generic EGL code has destroyed egl_onscreen->egl_surface; the window goes after it */
  if (sym_onscreen->window)
    phe_clws_window_destroy (sym_onscreen->window);

  g_slice_free (CoglOnscreenSymbian, sym_onscreen);
  egl_onscreen->platform = NULL;
}

static void
_cogl_winsys_onscreen_set_visibility (CoglOnscreen *onscreen,
                                      CoglBool visibility)
{
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;
  CoglOnscreenSymbian *sym_onscreen = egl_onscreen->platform;

  sym_onscreen->visible = visibility;
  phe_clws_window_set_visible (sym_onscreen->window, visibility && !sym_onscreen->composited);
}

void
cogl_symbian_onscreen_set_composited (CoglOnscreen *onscreen, CoglBool composited)
{
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;
  CoglOnscreenSymbian *sym_onscreen = egl_onscreen ? egl_onscreen->platform : NULL;

  if (sym_onscreen == NULL || sym_onscreen->composited == composited)
    return;
  sym_onscreen->composited = composited;
  phe_clws_window_set_visible (sym_onscreen->window, sym_onscreen->visible && !composited);
}

/* composited: no present. The paint is in the back buffer, and the frame was copied out after the paint; without a swap the next
   frame draws over the same buffer (Clutter redraws whole frames here: no buffer age, no swap region) */
static void (*parent_swap_buffers_with_damage) (CoglOnscreen *onscreen, const int *rectangles, int n_rectangles);

static void
_cogl_winsys_onscreen_swap_buffers_with_damage (CoglOnscreen *onscreen, const int *rectangles, int n_rectangles)
{
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;
  CoglOnscreenSymbian *sym_onscreen = egl_onscreen->platform;

  if (sym_onscreen->composited)
    return;
  parent_swap_buffers_with_damage (onscreen, rectangles, n_rectangles);
}

static void
_cogl_winsys_onscreen_set_resizable (CoglOnscreen *onscreen,
                                     CoglBool resizable)
{
  /* a WSERV window has no frame: the application (Clutter's stage) sets its size */
}

void *
cogl_symbian_onscreen_get_window (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;

  return egl_onscreen && egl_onscreen->platform ? ((CoglOnscreenSymbian *) egl_onscreen->platform)->window : NULL;
}

/* the window gets the new extent at once; Mesa's window surface follows the RWindow's size at the next buffer (as wl_egl_window_resize), and the framebuffer takes the
 * new size now, so the next frame is laid out for it (cogl_wayland_onscreen_resize without the pending-swap delay: the surface here is resized by the window, not by us) */
void
cogl_symbian_onscreen_resize (CoglOnscreen *onscreen, int x, int y, int width, int height)
{
  CoglFramebuffer *fb = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;

  if (egl_onscreen == NULL || egl_onscreen->platform == NULL)
    {
      _cogl_framebuffer_winsys_update_size (fb, width, height);
      return;
    }

  phe_clws_window_set_extent (((CoglOnscreenSymbian *) egl_onscreen->platform)->window, x, y, width, height);
  if (cogl_framebuffer_get_width (fb) != width || cogl_framebuffer_get_height (fb) != height)
    _cogl_framebuffer_winsys_update_size (fb, width, height);
}

static const CoglWinsysEGLVtable
_cogl_winsys_egl_vtable =
  {
    .display_setup = _cogl_winsys_egl_display_setup,
    .display_destroy = _cogl_winsys_egl_display_destroy,
    .context_created = _cogl_winsys_egl_context_created,
    .context_init = _cogl_winsys_egl_context_init,
    .cleanup_context = _cogl_winsys_egl_cleanup_context,
    .onscreen_init = _cogl_winsys_egl_onscreen_init,
    .onscreen_deinit = _cogl_winsys_egl_onscreen_deinit
  };

const CoglWinsysVtable *
_cogl_winsys_egl_symbian_get_vtable (void)
{
  static CoglBool vtable_inited = FALSE;
  static CoglWinsysVtable vtable;

  if (!vtable_inited)
    {
      /* a subclass of the EGL winsys, as every EGL platform */
      vtable = *_cogl_winsys_egl_get_vtable ();

      vtable.id = COGL_WINSYS_ID_EGL_SYMBIAN;
      vtable.name = "EGL_SYMBIAN";

      vtable.renderer_connect = _cogl_winsys_renderer_connect;
      vtable.renderer_disconnect = _cogl_winsys_renderer_disconnect;

      vtable.onscreen_set_visibility = _cogl_winsys_onscreen_set_visibility;
      parent_swap_buffers_with_damage = vtable.onscreen_swap_buffers_with_damage;
      vtable.onscreen_swap_buffers_with_damage = _cogl_winsys_onscreen_swap_buffers_with_damage;
      vtable.onscreen_set_resizable = _cogl_winsys_onscreen_set_resizable;

      vtable_inited = TRUE;
    }

  return &vtable;
}
