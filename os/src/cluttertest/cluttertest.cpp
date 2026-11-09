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

// waits for the next registry change after *aSeen, then: every other process's visible window, in registration order (at most
// aMax), each subscribed as composited by this process (EPheWinComposite: its owner keeps it off the window server's screen).
// Returns the count, or an error
extern "C" int phe_frame_list(unsigned* aSeen, PheFrameWin* aOut, int aMax)
	{
	static RPheWinReg reg;
	static TBool up;
	static RArray<TInt> subscribed;
	if (!up)
		{
		TInt r = RFbsSession::GetSession() ? KErrNone : RFbsSession::Connect();
		if (r == KErrNone)
			r = reg.Connect();
		if (r != KErrNone)
			return r;
		up = ETrue;
		}
	// the first call lists at once: the windows that registered before this process came (nothing else would change: their content
	// waits for a subscriber). The registry's serial starts at 1, so from 1 on the wait ends at the next change
	if (*aSeen == 0)
		*aSeen = 1;
	else
		{
		TRequestStatus st;
		reg.Notify(*aSeen, st);
		User::WaitForRequest(st);
		if (st.Int() < 0)
			return st.Int();
		*aSeen = (TUint)st.Int();
		}
	TPheWinInfo list[16];
	TPtr8 p((TUint8*)list, 0, sizeof(list));
	const TInt n = reg.List(p);
	if (n < 0)
		return n;
	const TUint me = (TUint)RProcess().Id().Id();
	TInt out = 0;
	for (TInt i = 0; i < n && i < 16 && out < aMax; ++i)
		{
		const TPheWinInfo& w = list[i];
		if (w.iOwner == me || (w.iState & EPheWinHidden))
			continue;
		if (subscribed.Find(w.iId) < 0 && reg.Subscribe(w.iId, EPheWinComposite) == KErrNone)
			subscribed.Append(w.iId);		// (a window that goes leaves a stale id: ids are not reused)
		PheFrameWin& f = aOut[out];
		Mem::FillZ(&f, sizeof(f));
		f.iId = w.iId; f.iX = w.iGeometry.iTl.iX; f.iY = w.iGeometry.iTl.iY; f.iW = w.iGeometry.Width(); f.iH = w.iGeometry.Height();
		f.iToken = w.iGpuToken;
		if (!f.iToken && w.iBitmap)
			{
			CFbsBitmap* b = new CFbsBitmap;
			if (b && b->Duplicate(w.iBitmap) == KErrNone)
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
