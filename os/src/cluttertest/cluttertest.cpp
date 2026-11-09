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
#include <pthread.h>
#include <unistd.h>

extern "C" void phe_log(const char* aMsg) { RDebug::Printf("%s", aMsg); }
extern "C" int phe_clutter_main(void);

// the rotation test: another window server client switches the screen mode, as the system's orientation plug-in (akncapserver) does on the phone; only when the ROM
// has more than one mode (make_phorepoc_rom.sh ROTATE=1: portrait 360x640, landscape 640x360 rotated 90). Clutter's backend must follow (EEventScreenDeviceChanged).
static void* Rotator(void*)
	{
	RWsSession ws;
	if (ws.Connect() != KErrNone) return NULL;
	CWsScreenDevice* dev = new CWsScreenDevice(ws);
	if (!dev || dev->Construct() != KErrNone) return NULL;
	const TInt n = dev->NumScreenModes();
	RDebug::Printf("[cluttertest] rotator: %d screen modes", n);
	if (n > 1)
		{
		static const int KAt[3] = { 20, 12, 12 };
		for (TInt i = 0; i < 3; i++)
			{
			sleep(KAt[i]);
			const TInt m = (i & 1) ? 0 : 1;
			TPixelsTwipsAndRotation r;
			dev->GetScreenModeSizeAndRotation(m, r);
			RDebug::Printf("[cluttertest] rotator: screen mode %d (%dx%d, rotation %d)", m, r.iPixelSize.iWidth, r.iPixelSize.iHeight, (int)r.iRotation);
			dev->SetScreenMode(m);
			ws.Flush();
			}
		}
	delete dev;
	ws.Close();
	return NULL;
	}

GLDEF_C TInt E32Main()
	{
	CTrapCleanup* cleanup = CTrapCleanup::New();
	setenv("FONTCONFIG_FILE", "z:/resource/phehome/fonts.conf", 1);
	RDebug::Printf("[cluttertest] start");
	pthread_t th;
	pthread_create(&th, NULL, Rotator, NULL);
	TInt rc = 0;
	TRAPD(err, rc = phe_clutter_main());
	RDebug::Printf("[cluttertest] exit rc=%d err=%d", rc, err);
	delete cleanup;
	return err ? err : rc;
	}
