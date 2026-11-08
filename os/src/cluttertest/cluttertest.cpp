// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause
//
// cluttertest.cpp -- entry point of the Clutter feasibility test: a full-screen WSERV window (as core's glestri makes it) that Cogl's EGL "null" platform takes as its native window
// (phe_cogl_native_window, the one patch to Cogl, recipes/build_cogl_x32.sh), then cluttertest_main.c (Clutter: a rotating textured actor and a blurred one, frames per second in the log).
#include <e32std.h>
#include <e32base.h>
#include <e32debug.h>
#include <w32std.h>
#include <stdlib.h>

extern "C" void phe_log(const char* aMsg) { RDebug::Printf("%s", aMsg); }
extern "C" int phe_clutter_main(int aWidth, int aHeight);

static RWindow* TheWindow = NULL;
extern "C" void* phe_cogl_native_window(void) { return TheWindow; }

static void MainL()
	{
	RWsSession ws;
	User::LeaveIfError(ws.Connect());
	CleanupClosePushL(ws);
	CWsScreenDevice* screen = new (ELeave) CWsScreenDevice(ws);
	CleanupStack::PushL(screen);
	User::LeaveIfError(screen->Construct());
	RWindowGroup group(ws);
	User::LeaveIfError(group.Construct(0x434c5431, ETrue));
	CleanupClosePushL(group);
	RWindow win(ws);
	User::LeaveIfError(win.Construct(group, 0x434c5432));
	CleanupClosePushL(win);
	const TSize scr = screen->SizeInPixels();
	win.SetExtent(TPoint(0, 0), scr);
	win.Activate();
	group.SetOrdinalPosition(0, 1000);
	ws.Flush();
	TheWindow = &win;
	RDebug::Printf("[cluttertest] window %dx%d", scr.iWidth, scr.iHeight);
	const int rc = phe_clutter_main(scr.iWidth, scr.iHeight);
	RDebug::Printf("[cluttertest] clutter main returned %d", rc);
	TheWindow = NULL;
	CleanupStack::PopAndDestroy(4);
	}

GLDEF_C TInt E32Main()
	{
	CTrapCleanup* cleanup = CTrapCleanup::New();
	setenv("FONTCONFIG_FILE", "z:/resource/phehome/fonts.conf", 1);
	setenv("CLUTTER_INPUT_BACKEND", "null", 1);                  // no input device yet (the WSERV event source is milestone M2)
	RDebug::Printf("[cluttertest] start");
	TRAPD(err, MainL());
	RDebug::Printf("[cluttertest] exit err=%d", err);
	delete cleanup;
	return err;
	}
