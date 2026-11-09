// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause
//
// cluttertest.cpp -- entry point of the Clutter test (docs hildon-port.md 7.33-7.36): the stage window, its WSERV session and the events are Clutter's own now (the "symbian"
// backend, clutter-symbian backend/), so this only sets the environment and runs cluttertest_main.c inside a trap level.
#include <e32std.h>
#include <e32base.h>
#include <e32debug.h>
#include <stdlib.h>
#include <w32std.h>
#include <e32svr.h>
#include <u32hal.h>
#include <phhal.h>
#include <pthread.h>
#include <unistd.h>

extern "C" void phe_log(const char* aMsg) { RDebug::Printf("%s", aMsg); }
extern "C" int phe_clutter_main(void);

// the display follower (a PC image with the dynamic-resolution display HAL, make_ekern.sh DYNRES=1): when the host window is resized the HAL's active size changes;
// this thread does what core's phedynres test client does (phedynres.cpp ApplySize, which owns a full-screen window of its own and so cannot run beside this test):
// it gives the window server's screen mode the new size and re-selects it, so every client gets EEventScreenDeviceChanged -- the event Clutter's backend must follow.
// Without the DYNRES HAL the function fails and the thread ends.
static void ApplySize(CWsScreenDevice& aScreen, TInt aW, TInt aH)
	{
	const TInt modes = aScreen.NumScreenModes();
	if (modes >= 3)
		aScreen.SetScreenMode(modes - 1);
	TSizeMode m = aScreen.GetCurrentScreenModeAttributes();
	m.iScreenSize = TSize(aW, aH);
	m.iScreenTwipsSize = TSize(aW * 48 / 7, aH * 48 / 7);
	m.iOrigin = TPoint(0, 0);
	m.iPointerCursorArea = TRect(0, 0, aW, aH);
	aScreen.SetCurrentScreenModeAttributes(m);
	aScreen.SetScreenMode(aScreen.CurrentScreenMode());
	}

static void* Follower(void*)
	{
	RWsSession ws;
	if (ws.Connect() != KErrNone) return NULL;
	CWsScreenDevice* dev = new CWsScreenDevice(ws);
	if (!dev || dev->Construct() != KErrNone) return NULL;
	TInt v[3] = { 0, 0, 0 };
	if (UserSvr::HalFunction(EHalGroupDisplay, EPhHalDisplayActive, v, NULL) != KErrNone)
		{
		RDebug::Printf("[cluttertest] display follower: no dynamic-resolution HAL");
		delete dev; ws.Close();
		return NULL;
		}
	TInt gen = v[2];
	RDebug::Printf("[cluttertest] display follower: active %dx%d gen %d, %d screen modes", v[0], v[1], gen, dev->NumScreenModes());
	for (;;)
		{
		User::After(150000);
		if (UserSvr::HalFunction(EHalGroupDisplay, EPhHalDisplayActive, v, NULL) != KErrNone || v[2] == gen)
			continue;
		gen = v[2];
		RDebug::Printf("[cluttertest] display follower: display now %dx%d (gen %d) -> screen mode", v[0], v[1], gen);
		ApplySize(*dev, v[0], v[1]);
		ws.Flush();
		}
	}

GLDEF_C TInt E32Main()
	{
	CTrapCleanup* cleanup = CTrapCleanup::New();
	setenv("FONTCONFIG_FILE", "z:/resource/phehome/fonts.conf", 1);
	RDebug::Printf("[cluttertest] start");
	pthread_t th;
	pthread_create(&th, NULL, Follower, NULL);
	TInt rc = 0;
	TRAPD(err, rc = phe_clutter_main());
	RDebug::Printf("[cluttertest] exit rc=%d err=%d", rc, err);
	delete cleanup;
	return err ? err : rc;
	}
