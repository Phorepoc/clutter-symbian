// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause
//
// cluttertest.cpp -- entry point of the Clutter test (docs hildon-port.md 7.33-7.36): the stage window, its WSERV session and the events are Clutter's own now (the "symbian"
// backend, clutter-symbian backend/), so this only sets the environment and runs cluttertest_main.c inside a trap level.
#include <e32std.h>
#include <e32base.h>
#include <e32debug.h>
#include <stdlib.h>

extern "C" void phe_log(const char* aMsg) { RDebug::Printf("%s", aMsg); }
extern "C" int phe_clutter_main(void);
extern "C" int phe_is_frame(void);

GLDEF_C TInt E32Main()
	{
	CTrapCleanup* cleanup = CTrapCleanup::New();
	setenv("FONTCONFIG_FILE", "z:/resource/phehome/fonts.conf", 1);
	RDebug::Printf("[cluttertest] start");
	TInt rc = 0;
	TRAPD(err, rc = phe_clutter_main());
	RDebug::Printf("[cluttertest] exit rc=%d err=%d", rc, err);
	delete cleanup;
	return err ? err : rc;
	}

// ======================================================================== the frame side of the window registry test (hildon-port.md C4)
// "cluttertest frame": a second process plays the frame process: it waits for registry changes (blocking, on its own
// thread: the registry session lives there) and composites every other process's window (hildon-port.md 7.48): by GPU share
// token, or else the bitmap duplicated by its FBSERV handle and its pixels copied out.
#include <fbs.h>
#include "phewinreg.h"

// one window as the compositor sees it (cluttertest_main.c PheFrameWin): geometry, GPU share token, or else a copy of the bitmap
struct PheFrameWin { int iId, iX, iY, iW, iH, iToken; unsigned char* iPx; int iPw, iPh; char iTitle[64]; };

// every other process's visible window now, in registration order (at most aMax), each subscribed as composited by this process
// (EPheWinComposite: its owner keeps it off the window server's screen). Runs on the main loop, over the backend's registry session
// (its events say when to call it again: hildon-port.md 7.49). Returns the count, or an error
extern "C" int phe_clws_reg_list(void* aBuf, int aBytes);
extern "C" int phe_clws_reg_subscribe(int aId, int aMode);
extern "C" int phe_frame_list(PheFrameWin* aOut, int aMax)
	{
	static RArray<TInt> subscribed;
	if (!RFbsSession::GetSession())
		{
		const TInt r = RFbsSession::Connect();
		if (r != KErrNone)
			return r;
		}
	TPheWinInfo list[16];
	const TInt n = phe_clws_reg_list(list, sizeof(list));
	if (n < 0)
		return n;
	const TUint me = (TUint)RProcess().Id().Id();
	TInt out = 0;
	for (TInt i = 0; i < n && i < 16 && out < aMax; ++i)
		{
		const TPheWinInfo& w = list[i];
		if (w.iOwner == me || (w.iState & EPheWinHidden))
			continue;
		if (subscribed.Find(w.iId) < 0 && phe_clws_reg_subscribe(w.iId, EPheWinComposite) == KErrNone)
			subscribed.Append(w.iId);		// (a window that goes leaves a stale id: ids are not reused)
		PheFrameWin& f = aOut[out];
		Mem::FillZ(&f, sizeof(f));
		f.iId = w.iId; f.iX = w.iGeometry.iTl.iX; f.iY = w.iGeometry.iTl.iY; f.iW = w.iGeometry.Width(); f.iH = w.iGeometry.Height();
		f.iToken = w.iGpuToken;
		if (!f.iToken && w.iBitmap)
			{
			CFbsBitmap* b = new CFbsBitmap;
			const TInt dr = b ? b->Duplicate(w.iBitmap) : KErrNoMemory;
			static TInt dupLogged;
			if (dr != KErrNone && dupLogged++ < 4)
				RDebug::Printf("[cluttertest-frame] window %d: bitmap %d duplicate failed %d", w.iId, w.iBitmap, dr);
			if (dr == KErrNone)
				{
				const TSize s = b->SizeInPixels();
				const TInt stride = CFbsBitmap::ScanLineLength(s.iWidth, b->DisplayMode());
				f.iPx = (unsigned char*)User::Alloc(s.iWidth * s.iHeight * 4);
				if (f.iPx)
					{
					b->BeginDataAccess();
					for (TInt y = 0; y < s.iHeight; ++y)
						Mem::Copy(f.iPx + y * s.iWidth * 4, (const TUint8*)b->DataAddress() + y * stride, s.iWidth * 4);
					b->EndDataAccess(ETrue);
					f.iPw = s.iWidth; f.iPh = s.iHeight;
					}
				}
			delete b;		// (replaced since the list: the next change brings the new one)
			}
		TBuf8<64> t;
		t.Copy(w.iTitle);
		const TInt l = Min(t.Length(), (TInt)sizeof(f.iTitle) - 1);
		Mem::Copy(f.iTitle, t.Ptr(), l);
		f.iTitle[l] = 0;
		++out;
		}
	return out;
	}

extern "C" void phe_frame_free(unsigned char* aPixels) { User::Free(aPixels); }

// start the frame-side process (the test's second process)
extern "C" int phe_frame_spawn(void)
	{
	RProcess p;
	TInt r = p.Create(_L("cluttertest.exe"), _L("frame"));
	if (r == KErrNone) { p.Resume(); p.Close(); }
	return r;
	}

extern "C" int phe_is_frame(void)
	{
	TBuf<16> cmd;
	User::CommandLine(cmd);
	return cmd.Find(_L("frame")) == 0;
	}

// the command line holds aWord (bootui passes "cluttertest|registry" as "registry")
extern "C" int phe_cmd_has(const char* aWord)
	{
	TBuf<64> cmd;
	User::CommandLine(cmd);
	TBuf<32> w;
	w.Copy(TPtrC8((const TUint8*)aWord));
	return cmd.Find(w) >= 0;
	}
