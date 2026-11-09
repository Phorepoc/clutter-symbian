/*
 * Clutter.
 *
 * clutter-backend-symbian.c -- the "symbian" windowing and input backend of Clutter 1.26 on the Symbian window server (clutter-symbian, docs hildon-port.md plan C, C2).
 * Derived from Clutter 1.26.4's win32 backend (clutter-backend-win32.c, clutter-device-manager-win32.c, clutter-event-win32.c: one core pointer and keyboard, a GSource whose
 * poll descriptor is the window system's message queue) and the EGL native backend (a ClutterStageCogl subclass over Cogl's EGL winsys). The windows and the WSERV session are
 * backend/phe_clws.cpp, the EGL surfaces Cogl's "symbian" platform (overlay/cogl-winsys-egl-symbian.c).
 *
 * Copyright (C) 2006-2010 OpenedHand, Intel Corporation (the files this is derived from); the Symbian backend: The Phorepoc Authors.
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <glib.h>

#include "clutter-backend-private.h"
#include "clutter-device-manager-private.h"
#include "clutter-event-private.h"
#include "clutter-main.h"
#include "clutter-private.h"
#include "clutter-stage-private.h"
#include "clutter-stage-window.h"
#include "clutter-keysyms.h"
#include "cogl/clutter-stage-cogl.h"

#include "phe_clws.h"
#include "clutter-symbian-keys.h"

/* Cogl's symbian platform (overlay/cogl-winsys-egl-symbian.c) */
void *cogl_symbian_onscreen_get_window (CoglOnscreen *onscreen);
void cogl_symbian_onscreen_resize (CoglOnscreen *onscreen, int x, int y, int width, int height);

ClutterBackend *clutter_backend_symbian_new (void);

#define LOG(...) do { char m_[240]; g_snprintf (m_, sizeof m_, __VA_ARGS__); phe_clws_log (m_); } while (0)

/* ======================================================================== types */

typedef struct { ClutterBackend parent_instance; GSource *event_source; } ClutterBackendSymbian;
typedef struct { ClutterBackendClass parent_class; } ClutterBackendSymbianClass;

typedef struct {
  ClutterStageCogl parent_instance;
  gboolean fullscreen;
  gboolean shown;
  gboolean pointer_inside;
  gint width, height;            /* the window size when not full-screen */
  gchar *title;
} ClutterStageSymbian;
typedef struct { ClutterStageCoglClass parent_class; } ClutterStageSymbianClass;

typedef struct {
  ClutterDeviceManager parent_instance;
  GSList *devices;
  ClutterInputDevice *core_pointer, *core_keyboard;
} ClutterDeviceManagerSymbian;
typedef struct { ClutterDeviceManagerClass parent_class; } ClutterDeviceManagerSymbianClass;

GType _clutter_backend_symbian_get_type (void);
GType _clutter_stage_symbian_get_type (void);
GType _clutter_device_manager_symbian_get_type (void);
#define CLUTTER_TYPE_BACKEND_SYMBIAN (_clutter_backend_symbian_get_type ())
#define CLUTTER_TYPE_STAGE_SYMBIAN (_clutter_stage_symbian_get_type ())
#define CLUTTER_TYPE_DEVICE_MANAGER_SYMBIAN (_clutter_device_manager_symbian_get_type ())
#define CLUTTER_STAGE_SYMBIAN(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), CLUTTER_TYPE_STAGE_SYMBIAN, ClutterStageSymbian))
#define CLUTTER_IS_STAGE_SYMBIAN(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), CLUTTER_TYPE_STAGE_SYMBIAN))
#define CLUTTER_DEVICE_MANAGER_SYMBIAN(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), CLUTTER_TYPE_DEVICE_MANAGER_SYMBIAN, ClutterDeviceManagerSymbian))

static GSList *all_stages;       /* the realized stages, for window -> stage */

static int
window_priority (void)
{
  const char *p = g_getenv ("CLUTTER_SYMBIAN_PRIORITY");   /* the window group's ordinal priority (0 = an ordinary application) */
  return p ? atoi (p) : 0;
}

/* ======================================================================== device manager (clutter-device-manager-win32.c) */

G_DEFINE_TYPE (ClutterDeviceManagerSymbian, _clutter_device_manager_symbian, CLUTTER_TYPE_DEVICE_MANAGER)

static void
clutter_device_manager_symbian_constructed (GObject *gobject)
{
  ClutterDeviceManager *manager = CLUTTER_DEVICE_MANAGER (gobject);
  ClutterDeviceManagerSymbian *self = CLUTTER_DEVICE_MANAGER_SYMBIAN (gobject);
  ClutterInputDevice *device;

  device = g_object_new (CLUTTER_TYPE_INPUT_DEVICE,
                         "id", 0, "name", "Core Pointer", "device-manager", manager,
                         "device-type", CLUTTER_POINTER_DEVICE, "device-mode", CLUTTER_INPUT_MODE_MASTER,
                         "has-cursor", TRUE, "enabled", TRUE, NULL);
  _clutter_device_manager_add_device (manager, device);

  device = g_object_new (CLUTTER_TYPE_INPUT_DEVICE,
                         "id", 1, "name", "Core Keyboard", "device-manager", manager,
                         "device-type", CLUTTER_KEYBOARD_DEVICE, "device-mode", CLUTTER_INPUT_MODE_MASTER,
                         "enabled", TRUE, NULL);
  _clutter_device_manager_add_device (manager, device);

  _clutter_input_device_set_associated_device (self->core_pointer, self->core_keyboard);
  _clutter_input_device_set_associated_device (self->core_keyboard, self->core_pointer);
}

static void
clutter_device_manager_symbian_add_device (ClutterDeviceManager *manager, ClutterInputDevice *device)
{
  ClutterDeviceManagerSymbian *self = CLUTTER_DEVICE_MANAGER_SYMBIAN (manager);
  ClutterInputDeviceType t = clutter_input_device_get_device_type (device);

  self->devices = g_slist_prepend (self->devices, device);
  if (t == CLUTTER_POINTER_DEVICE && self->core_pointer == NULL)
    self->core_pointer = device;
  if (t == CLUTTER_KEYBOARD_DEVICE && self->core_keyboard == NULL)
    self->core_keyboard = device;
}

static void
clutter_device_manager_symbian_remove_device (ClutterDeviceManager *manager, ClutterInputDevice *device)
{
  ClutterDeviceManagerSymbian *self = CLUTTER_DEVICE_MANAGER_SYMBIAN (manager);

  self->devices = g_slist_remove (self->devices, device);
}

static const GSList *
clutter_device_manager_symbian_get_devices (ClutterDeviceManager *manager)
{
  return CLUTTER_DEVICE_MANAGER_SYMBIAN (manager)->devices;
}

static ClutterInputDevice *
clutter_device_manager_symbian_get_core_device (ClutterDeviceManager *manager, ClutterInputDeviceType type)
{
  ClutterDeviceManagerSymbian *self = CLUTTER_DEVICE_MANAGER_SYMBIAN (manager);

  switch (type)
    {
    case CLUTTER_POINTER_DEVICE: return self->core_pointer;
    case CLUTTER_KEYBOARD_DEVICE: return self->core_keyboard;
    default: return NULL;
    }
}

static ClutterInputDevice *
clutter_device_manager_symbian_get_device (ClutterDeviceManager *manager, gint id)
{
  GSList *l;

  for (l = CLUTTER_DEVICE_MANAGER_SYMBIAN (manager)->devices; l != NULL; l = l->next)
    if (clutter_input_device_get_device_id (l->data) == id)
      return l->data;
  return NULL;
}

static void
_clutter_device_manager_symbian_class_init (ClutterDeviceManagerSymbianClass *klass)
{
  ClutterDeviceManagerClass *manager_class = CLUTTER_DEVICE_MANAGER_CLASS (klass);

  G_OBJECT_CLASS (klass)->constructed = clutter_device_manager_symbian_constructed;
  manager_class->add_device = clutter_device_manager_symbian_add_device;
  manager_class->remove_device = clutter_device_manager_symbian_remove_device;
  manager_class->get_devices = clutter_device_manager_symbian_get_devices;
  manager_class->get_core_device = clutter_device_manager_symbian_get_core_device;
  manager_class->get_device = clutter_device_manager_symbian_get_device;
}

static void
_clutter_device_manager_symbian_init (ClutterDeviceManagerSymbian *self)
{
}

/* ======================================================================== stage window */

static ClutterStageWindowIface *clutter_stage_window_parent_iface = NULL;
static void clutter_stage_window_iface_init (ClutterStageWindowIface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterStageSymbian, _clutter_stage_symbian, CLUTTER_TYPE_STAGE_COGL,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_STAGE_WINDOW, clutter_stage_window_iface_init))

static void
stage_target_size (ClutterStageSymbian *self, gint *w, gint *h)
{
  if (self->fullscreen || self->width <= 0 || self->height <= 0)
    phe_clws_screen_size (w, h);
  else
    { *w = self->width; *h = self->height; }
}

/* the window and the framebuffer to the size the stage should have now; the stage relays out at that size */
static void
stage_apply_size (ClutterStageSymbian *self)
{
  ClutterStageCogl *cogl = CLUTTER_STAGE_COGL (self);
  gint w, h;

  stage_target_size (self, &w, &h);
  if (cogl->onscreen == NULL)
    return;
  if (cogl_framebuffer_get_width (COGL_FRAMEBUFFER (cogl->onscreen)) == w && cogl_framebuffer_get_height (COGL_FRAMEBUFFER (cogl->onscreen)) == h)
    return;
  cogl_symbian_onscreen_resize (cogl->onscreen, 0, 0, w, h);
  LOG ("[clutter-symbian] stage window %dx%d%s", w, h, self->fullscreen ? " (full screen)" : "");
  clutter_actor_set_size (CLUTTER_ACTOR (cogl->wrapper), w, h);
  _clutter_stage_dirty_viewport (cogl->wrapper);
  clutter_actor_queue_relayout (CLUTTER_ACTOR (cogl->wrapper));
}

static gboolean
clutter_stage_symbian_realize (ClutterStageWindow *stage_window)
{
  ClutterStageSymbian *self = CLUTTER_STAGE_SYMBIAN (stage_window);
  ClutterStageCogl *cogl = CLUTTER_STAGE_COGL (stage_window);
  ClutterBackend *backend = clutter_get_default_backend ();
  gint w, h;

  /* the onscreen gets the size the application asked for through resize (), else the screen's (a stage is realized when it is created, before the application sizes it:
   * its default 800x600 is not a size anyone asked for), then ClutterStageCogl allocates it: Cogl's platform makes the window */
  stage_target_size (self, &w, &h);
  if (cogl->onscreen == NULL)
    cogl->onscreen = cogl_onscreen_new (backend->cogl_context, w, h);
  if (!clutter_stage_window_parent_iface->realize (stage_window))
    return FALSE;
  all_stages = g_slist_prepend (all_stages, self);
  LOG ("[clutter-symbian] stage realized: window %p, %dx%d", cogl_symbian_onscreen_get_window (cogl->onscreen),
       cogl_framebuffer_get_width (COGL_FRAMEBUFFER (cogl->onscreen)), cogl_framebuffer_get_height (COGL_FRAMEBUFFER (cogl->onscreen)));
  return TRUE;
}

static void
clutter_stage_symbian_unrealize (ClutterStageWindow *stage_window)
{
  all_stages = g_slist_remove (all_stages, stage_window);
  clutter_stage_window_parent_iface->unrealize (stage_window);
}

static void
clutter_stage_symbian_show (ClutterStageWindow *stage_window, gboolean do_raise)
{
  ClutterStageSymbian *self = CLUTTER_STAGE_SYMBIAN (stage_window);
  ClutterStageCogl *cogl = CLUTTER_STAGE_COGL (stage_window);

  if (cogl->onscreen)
    {
      cogl_onscreen_show (cogl->onscreen);
      phe_clws_window_raise (cogl_symbian_onscreen_get_window (cogl->onscreen), window_priority ());   /* the app's group in front: it gets the keyboard focus */
    }
  self->shown = TRUE;
  clutter_stage_window_parent_iface->show (stage_window, do_raise);
}

static void
clutter_stage_symbian_hide (ClutterStageWindow *stage_window)
{
  ClutterStageSymbian *self = CLUTTER_STAGE_SYMBIAN (stage_window);
  ClutterStageCogl *cogl = CLUTTER_STAGE_COGL (stage_window);

  if (cogl->onscreen)
    cogl_onscreen_hide (cogl->onscreen);
  self->shown = FALSE;
  clutter_stage_window_parent_iface->hide (stage_window);
}

static void
clutter_stage_symbian_resize (ClutterStageWindow *stage_window, gint width, gint height)
{
  ClutterStageSymbian *self = CLUTTER_STAGE_SYMBIAN (stage_window);

  if (width < 1 || height < 1)
    return;
  self->width = width; self->height = height;
  if (!self->fullscreen)
    stage_apply_size (self);
}

static void
clutter_stage_symbian_set_fullscreen (ClutterStageWindow *stage_window, gboolean is_fullscreen)
{
  ClutterStageSymbian *self = CLUTTER_STAGE_SYMBIAN (stage_window);
  ClutterStageCogl *cogl = CLUTTER_STAGE_COGL (stage_window);

  if (self->fullscreen == is_fullscreen)
    return;
  self->fullscreen = is_fullscreen;
  stage_apply_size (self);
  if (is_fullscreen && cogl->onscreen && self->shown)
    phe_clws_window_raise (cogl_symbian_onscreen_get_window (cogl->onscreen), window_priority ());
  if (cogl->wrapper)
    _clutter_stage_update_state (cogl->wrapper, is_fullscreen ? 0 : CLUTTER_STAGE_STATE_FULLSCREEN, is_fullscreen ? CLUTTER_STAGE_STATE_FULLSCREEN : 0);
}

static void
clutter_stage_symbian_set_title (ClutterStageWindow *stage_window, const gchar *title)
{
  ClutterStageSymbian *self = CLUTTER_STAGE_SYMBIAN (stage_window);

  g_free (self->title);
  self->title = g_strdup (title);   /* WSERV windows have no title bar: kept for the window registry (plan C4) */
}

static void
clutter_stage_symbian_set_cursor_visible (ClutterStageWindow *stage_window, gboolean visible)
{
  /* the pointer of this system is a touch panel or the window server's own sprite: nothing to hide per window */
}

static void
clutter_stage_symbian_set_user_resizable (ClutterStageWindow *stage_window, gboolean is_resizable)
{
}

static gboolean
clutter_stage_symbian_can_clip_redraws (ClutterStageWindow *stage_window)
{
  return TRUE;
}

static void
clutter_stage_window_iface_init (ClutterStageWindowIface *iface)
{
  clutter_stage_window_parent_iface = g_type_interface_peek_parent (iface);

  iface->realize = clutter_stage_symbian_realize;
  iface->unrealize = clutter_stage_symbian_unrealize;
  iface->show = clutter_stage_symbian_show;
  iface->hide = clutter_stage_symbian_hide;
  iface->resize = clutter_stage_symbian_resize;
  iface->set_fullscreen = clutter_stage_symbian_set_fullscreen;
  iface->set_title = clutter_stage_symbian_set_title;
  iface->set_cursor_visible = clutter_stage_symbian_set_cursor_visible;
  iface->set_user_resizable = clutter_stage_symbian_set_user_resizable;
  iface->can_clip_redraws = clutter_stage_symbian_can_clip_redraws;
}

static void
clutter_stage_symbian_finalize (GObject *gobject)
{
  g_free (CLUTTER_STAGE_SYMBIAN (gobject)->title);
  G_OBJECT_CLASS (_clutter_stage_symbian_parent_class)->finalize (gobject);
}

static void
_clutter_stage_symbian_class_init (ClutterStageSymbianClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = clutter_stage_symbian_finalize;
}

static void
_clutter_stage_symbian_init (ClutterStageSymbian *self)
{
}

static ClutterStageSymbian *
stage_of_window (void *window)
{
  GSList *l;

  for (l = all_stages; l != NULL; l = l->next)
    {
      ClutterStageCogl *cogl = l->data;
      if (cogl->onscreen && cogl_symbian_onscreen_get_window (cogl->onscreen) == window)
        return l->data;
    }
  return NULL;
}

static ClutterStageSymbian *
focus_stage (void)
{
  GSList *l;

  for (l = all_stages; l != NULL; l = l->next)
    if (((ClutterStageSymbian *) l->data)->shown)
      return l->data;
  return all_stages ? all_stages->data : NULL;
}

/* ======================================================================== events (clutter-event-win32.c) */

typedef struct { GSource source; GPollFD pfd; ClutterBackend *backend; } ClutterEventSourceSymbian;

static struct { gint pending; gint last; guint down[256]; } kstate = { -1, -1, { 0 } };   /* the held key-down, the key that repeats, the keysym each pressed key has */

static ClutterModifierType
modifiers_of (unsigned m)
{
  ClutterModifierType r = 0;

  if (m & 0x00000400) r |= CLUTTER_SHIFT_MASK;       /* EModifierShift */
  if (m & 0x00000080) r |= CLUTTER_CONTROL_MASK;     /* EModifierCtrl */
  if (m & 0x00000010) r |= CLUTTER_MOD1_MASK;        /* EModifierAlt */
  if (m & 0x00004000) r |= CLUTTER_LOCK_MASK;        /* EModifierCapsLock */
  if (m & 0x00002000) r |= CLUTTER_MOD2_MASK;        /* EModifierFunc */
  return r;
}

static const PheKeyEntry *
key_entry (guint scancode)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (phe_keytable); i++)
    if (phe_keytable[i].code == scancode)
      return &phe_keytable[i];
  return NULL;
}

/* WSERV key codes (e32keys.h TKeyCode) -> keysyms; printable codes are the same in both (gdkevents-symbian.c keysym_of) */
static guint
keysym_of (gint code, gint scancode, unsigned mods)
{
  const PheKeyEntry *e;

  if ((mods & 0x00000080) && code > 0 && code < 0x20 && (e = key_entry (scancode)) != NULL)   /* Ctrl+letter types a control code (Ctrl+A = 1): the key is still the letter */
    return (mods & 0x00000400) ? e->hi : e->lo;
  switch (code)
    {
    case 0x0d: case 0x0e: return CLUTTER_KEY_Return;
    case 0x08: case 0x01: return CLUTTER_KEY_BackSpace;
    case 0x1b: case 0x0b: return CLUTTER_KEY_Escape;
    case 0x09: return CLUTTER_KEY_Tab;
    case 0x7f: return CLUTTER_KEY_Delete;
    case 0xf807: return CLUTTER_KEY_Left; case 0xf808: return CLUTTER_KEY_Right;
    case 0xf809: return CLUTTER_KEY_Up; case 0xf80a: return CLUTTER_KEY_Down;
    case 0xf802: return CLUTTER_KEY_Home; case 0xf803: return CLUTTER_KEY_End;
    case 0xf804: return CLUTTER_KEY_Page_Up; case 0xf805: return CLUTTER_KEY_Page_Down;
    case 0xf845: return CLUTTER_KEY_F5; case 0xf846: return CLUTTER_KEY_F6; case 0xf847: return CLUTTER_KEY_F7;   /* the softkeys EKeyDevice0..2 */
    }
  if (code >= 0x20 && code < 0x7f)
    return (guint) code;
  if (code >= 0xa0 && code < 0xf700)
    return clutter_unicode_to_keysym (code);
  if ((e = key_entry (scancode)) != NULL)
    return (mods & 0x00000400) ? e->hi : e->lo;
  return 0x1008ff00u + (guint) (scancode & 0xff);     /* vendor key space: unknown keys stay distinguishable */
}

static void
queue_key (ClutterBackend *backend, gboolean press, guint keyval, gint scancode, unsigned mods, guint32 time_)
{
  ClutterDeviceManagerSymbian *dm = CLUTTER_DEVICE_MANAGER_SYMBIAN (backend->device_manager);
  ClutterStageSymbian *stage = focus_stage ();
  ClutterEvent *event;

  if (stage == NULL)
    return;
  event = clutter_event_new (press ? CLUTTER_KEY_PRESS : CLUTTER_KEY_RELEASE);
  event->any.stage = CLUTTER_STAGE_COGL (stage)->wrapper;
  event->key.time = time_;
  event->key.keyval = keyval;
  event->key.hardware_keycode = scancode;
  event->key.modifier_state = modifiers_of (mods);
  event->key.unicode_value = clutter_keysym_to_unicode (keyval);
  clutter_event_set_device (event, dm->core_keyboard);
  _clutter_event_push (event, FALSE);
}

static gboolean
is_modifier_scancode (gint sc)
{
  return (sc >= 0x12 && sc <= 0x17) || sc == 0x1a || sc == 0x1b || sc == 0x1c;   /* shift, alt, ctrl (left / right), caps / num / scroll lock: they never type */
}

static void
press_pending (ClutterBackend *backend, gint code, unsigned mods, guint32 time_)
{
  gint sc = kstate.pending;

  kstate.pending = -1;
  kstate.down[sc] = keysym_of (code, sc, mods);
  kstate.last = sc;
  queue_key (backend, TRUE, kstate.down[sc], sc, mods, time_);
}

static void
translate_key (ClutterBackend *backend, const PheClwsEvent *e)
{
  /* WSERV: key-down(scancode), key(code) [repeated], key-up(scancode) for one press. A key-down is held back until the typed event gives the keysym; a modifier key presses at
   * once (it never types), a second key-down or a key-up presses a held one with its scancode's keysym. Each key's release carries the keysym its press had. */
  gint sc = e->scancode & 0xff;

  if (e->type == PHE_CLWS_KEY_DOWN)
    {
      if (kstate.pending >= 0)
        press_pending (backend, 0, e->modifiers, e->time_ms);
      kstate.pending = sc;
      if (is_modifier_scancode (sc))
        press_pending (backend, 0, e->modifiers, e->time_ms);
      return;
    }
  if (e->type == PHE_CLWS_KEY)
    {
      if (kstate.pending >= 0)
        press_pending (backend, e->code, e->modifiers, e->time_ms);
      else if (e->repeats > 0 && kstate.last >= 0 && kstate.down[kstate.last])
        queue_key (backend, TRUE, kstate.down[kstate.last], kstate.last, e->modifiers, e->time_ms);   /* auto-repeat: another press */
      return;
    }
  if (kstate.pending == sc)
    press_pending (backend, 0, e->modifiers, e->time_ms);
  queue_key (backend, FALSE, kstate.down[sc] ? kstate.down[sc] : keysym_of (0, sc, e->modifiers), sc, e->modifiers, e->time_ms);
  kstate.down[sc] = 0;
  if (kstate.last == sc)
    kstate.last = -1;
}

static void
translate_pointer (ClutterBackend *backend, const PheClwsEvent *e)
{
  ClutterDeviceManagerSymbian *dm = CLUTTER_DEVICE_MANAGER_SYMBIAN (backend->device_manager);
  ClutterStageSymbian *self = stage_of_window (e->window);
  ClutterStage *stage;
  ClutterEvent *event;
  ClutterModifierType mods = modifiers_of (e->modifiers);

  if (self == NULL)
    return;
  stage = CLUTTER_STAGE_COGL (self)->wrapper;
  if (!self->pointer_inside)
    {
      /* the first pointer event on this stage: the pointer enters it (clutter-event-win32.c WM_MOUSEMOVE) */
      ClutterEvent *crossing = clutter_event_new (CLUTTER_ENTER);
      crossing->crossing.time = e->time_ms;
      crossing->crossing.x = e->x;
      crossing->crossing.y = e->y;
      crossing->crossing.stage = stage;
      crossing->crossing.source = CLUTTER_ACTOR (stage);
      crossing->crossing.related = NULL;
      clutter_event_set_device (crossing, dm->core_pointer);
      _clutter_input_device_set_stage (dm->core_pointer, stage);
      self->pointer_inside = TRUE;
      _clutter_event_push (crossing, FALSE);
    }
  switch (e->ptype)
    {
    case 0: case 1: case 2: case 3: case 4: case 5:     /* EButton1Down .. EButton3Up */
      event = clutter_event_new ((e->ptype & 1) ? CLUTTER_BUTTON_RELEASE : CLUTTER_BUTTON_PRESS);
      event->button.time = e->time_ms;
      event->button.x = e->x;
      event->button.y = e->y;
      event->button.button = e->ptype / 2 == 0 ? 1 : e->ptype / 2 == 1 ? 2 : 3;     /* EButton2 = the middle button, EButton3 = the right one (pointerevent.h) */
      event->button.click_count = (e->modifiers & 0x00080000) ? 2 : 1;            /* EModifierDoubleClick */
      event->button.modifier_state = mods | ((e->ptype & 1) ? (event->button.button == 1 ? CLUTTER_BUTTON1_MASK : event->button.button == 2 ? CLUTTER_BUTTON2_MASK : CLUTTER_BUTTON3_MASK) : 0);
      break;
    case 6: case 7:                                     /* EDrag (button 1 down), EMove */
      event = clutter_event_new (CLUTTER_MOTION);
      event->motion.time = e->time_ms;
      event->motion.x = e->x;
      event->motion.y = e->y;
      event->motion.modifier_state = mods | (e->ptype == 6 ? CLUTTER_BUTTON1_MASK : 0);
      break;
    default:
      return;
    }
  event->any.stage = stage;
  clutter_event_set_device (event, dm->core_pointer);
  _clutter_event_push (event, FALSE);
}

static void
translate (ClutterBackend *backend, const PheClwsEvent *e)
{
  GSList *l;

  switch (e->type)
    {
    case PHE_CLWS_KEY_DOWN: case PHE_CLWS_KEY: case PHE_CLWS_KEY_UP:
      translate_key (backend, e);
      break;
    case PHE_CLWS_POINTER:
      translate_pointer (backend, e);
      break;
    case PHE_CLWS_REDRAW:
      {
        ClutterStageSymbian *self = stage_of_window (e->window);
        if (self)
          clutter_actor_queue_redraw (CLUTTER_ACTOR (CLUTTER_STAGE_COGL (self)->wrapper));
        break;
      }
    case PHE_CLWS_SCREEN_CHANGED:
      /* a new screen mode (rotation, a new resolution): full-screen stages follow it, the others keep their size inside it */
      for (l = all_stages; l != NULL; l = l->next)
        {
          ClutterStageSymbian *self = l->data;
          if (self->fullscreen || self->width <= 0)
            stage_apply_size (self);
          clutter_actor_queue_redraw (CLUTTER_ACTOR (CLUTTER_STAGE_COGL (self)->wrapper));
        }
      break;
    case PHE_CLWS_FOCUS_IN: case PHE_CLWS_FOCUS_OUT:
      {
        ClutterDeviceManagerSymbian *dm = CLUTTER_DEVICE_MANAGER_SYMBIAN (backend->device_manager);
        ClutterStageSymbian *self = focus_stage ();
        gboolean in = e->type == PHE_CLWS_FOCUS_IN;
        if (self == NULL)
          break;
        _clutter_input_device_set_stage (dm->core_keyboard, in ? CLUTTER_STAGE_COGL (self)->wrapper : NULL);
        _clutter_stage_update_state (CLUTTER_STAGE_COGL (self)->wrapper, in ? 0 : CLUTTER_STAGE_STATE_ACTIVATED, in ? CLUTTER_STAGE_STATE_ACTIVATED : 0);
        if (!in && self->pointer_inside)
          {
            /* the window group went to the background: the pointer has left the stage */
            ClutterEvent *crossing = clutter_event_new (CLUTTER_LEAVE);
            crossing->crossing.time = e->time_ms;
            crossing->crossing.stage = CLUTTER_STAGE_COGL (self)->wrapper;
            crossing->crossing.source = CLUTTER_ACTOR (crossing->crossing.stage);
            clutter_event_set_device (crossing, dm->core_pointer);
            _clutter_input_device_set_stage (dm->core_pointer, NULL);
            self->pointer_inside = FALSE;
            _clutter_event_push (crossing, FALSE);
          }
        break;
      }
    }
}

static gboolean
clutter_event_prepare (GSource *source, gint *timeout)
{
  gboolean retval;

  _clutter_threads_acquire_lock ();
  *timeout = -1;
  retval = clutter_events_pending () || phe_clws_pending ();
  _clutter_threads_release_lock ();
  return retval;
}

static gboolean
clutter_event_check (GSource *source)
{
  ClutterEventSourceSymbian *es = (ClutterEventSourceSymbian *) source;
  gboolean retval;

  _clutter_threads_acquire_lock ();
  retval = (es->pfd.revents & G_IO_IN) ? (clutter_events_pending () || phe_clws_pending ()) : clutter_events_pending ();
  _clutter_threads_release_lock ();
  return retval;
}

static gboolean
clutter_event_dispatch (GSource *source, GSourceFunc callback, gpointer user_data)
{
  ClutterEventSourceSymbian *es = (ClutterEventSourceSymbian *) source;
  ClutterEvent *event;
  PheClwsEvent e;

  _clutter_threads_acquire_lock ();
  /* take WSERV events until one of them turns into Clutter events (clutter-event-win32.c: PeekMessage until the queue has one) */
  while (!clutter_events_pending () && phe_clws_next_event (&e))
    translate (es->backend, &e);
  if ((event = clutter_event_get ()))
    {
      if (event->any.stage)
        _clutter_stage_queue_event (event->any.stage, event, FALSE);
      else
        clutter_event_free (event);
    }
  _clutter_threads_release_lock ();
  return TRUE;
}

static GSourceFuncs event_funcs = { clutter_event_prepare, clutter_event_check, clutter_event_dispatch, NULL, NULL, NULL };

static GPollFunc inner_poll;

static gint
symbian_poll (GPollFD *fds, guint nfds, gint timeout)
{
  return phe_clws_poll ((PheClwsPollFD *) fds, nfds, timeout);
}

/* ======================================================================== backend */

G_DEFINE_TYPE (ClutterBackendSymbian, _clutter_backend_symbian, CLUTTER_TYPE_BACKEND)

static CoglRenderer *
clutter_backend_symbian_get_renderer (ClutterBackend *backend, GError **error)
{
  CoglRenderer *renderer = cogl_renderer_new ();

  cogl_renderer_set_winsys_id (renderer, COGL_WINSYS_ID_EGL_SYMBIAN);
  return renderer;
}

static void
clutter_backend_symbian_init_events (ClutterBackend *backend)
{
  ClutterBackendSymbian *self = (ClutterBackendSymbian *) backend;
  ClutterEventSourceSymbian *es;
  GSource *source;

  if (phe_clws_init () != 0)
    g_error ("clutter-symbian: no window server session");
  backend->device_manager = g_object_new (CLUTTER_TYPE_DEVICE_MANAGER_SYMBIAN, "backend", backend, NULL);

  /* the main loop waits for WSERV and its descriptors together: the poll function of the default context (as GLib's win32 poll waits for the message queue) */
  if (inner_poll == NULL)
    {
      inner_poll = g_main_context_get_poll_func (NULL);
      phe_clws_set_inner_poll ((PheClwsPollFunc) inner_poll);
      g_main_context_set_poll_func (NULL, symbian_poll);
    }

  source = g_source_new (&event_funcs, sizeof (ClutterEventSourceSymbian));
  es = (ClutterEventSourceSymbian *) source;
  es->backend = backend;
  es->pfd.fd = PHE_CLWS_POLL_FD;
  es->pfd.events = G_IO_IN;
  g_source_add_poll (source, &es->pfd);
  g_source_set_priority (source, CLUTTER_PRIORITY_EVENTS);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);
  self->event_source = source;
  LOG ("[clutter-symbian] backend up: WSERV event source, core pointer + keyboard");
}

static void
clutter_backend_symbian_dispose (GObject *gobject)
{
  ClutterBackendSymbian *self = (ClutterBackendSymbian *) gobject;

  if (self->event_source)
    {
      g_source_destroy (self->event_source);
      g_source_unref (self->event_source);
      self->event_source = NULL;
    }
  G_OBJECT_CLASS (_clutter_backend_symbian_parent_class)->dispose (gobject);
}

static void
_clutter_backend_symbian_class_init (ClutterBackendSymbianClass *klass)
{
  ClutterBackendClass *backend_class = CLUTTER_BACKEND_CLASS (klass);

  G_OBJECT_CLASS (klass)->dispose = clutter_backend_symbian_dispose;
  backend_class->stage_window_type = CLUTTER_TYPE_STAGE_SYMBIAN;
  backend_class->get_renderer = clutter_backend_symbian_get_renderer;
  backend_class->init_events = clutter_backend_symbian_init_events;
}

static void
_clutter_backend_symbian_init (ClutterBackendSymbian *self)
{
}

ClutterBackend *
clutter_backend_symbian_new (void)
{
  return g_object_new (CLUTTER_TYPE_BACKEND_SYMBIAN, NULL);
}
