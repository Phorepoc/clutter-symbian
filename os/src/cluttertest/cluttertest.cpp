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
// thread: the registry session lives there) and imports the first other process's window: the bitmap duplicated by its
// FBSERV handle, its pixels copied out for a Clutter texture.
#include <fbs.h>
#include "phewinreg.h"

// aToken: the window's GPU share token (hildon-port.md 7.47; then *aPixels = NULL and the size is the window's), 0 = bitmap content
extern "C" int phe_frame_next(unsigned* aSeen, int* aId, int* aW, int* aH, unsigned char** aPixels, int* aToken, char* aTitle, int aTitleLen)
	{
	*aToken = 0;
	static RPheWinReg reg;
	static TBool up;
	if (!up)
		{
		TInt r = RFbsSession::GetSession() ? KErrNone : RFbsSession::Connect();
		if (r == KErrNone)
			r = reg.Connect();
		if (r != KErrNone)
			return r;
		up = ETrue;
		}
	TRequestStatus st;
	reg.Notify(*aSeen, st);
	User::WaitForRequest(st);
	if (st.Int() < 0)
		return st.Int();
	*aSeen = (TUint)st.Int();
	TPheWinInfo list[8];
	TPtr8 p((TUint8*)list, 0, sizeof(list));
	const TInt n = reg.List(p);
	const TUint me = (TUint)RProcess().Id().Id();
	for (TInt i = 0; i < n && i < 8; ++i)
		{
		if (list[i].iOwner == me || (list[i].iBitmap == 0 && list[i].iGpuToken == 0) || (list[i].iState & EPheWinHidden))
			continue;
		if (list[i].iGpuToken)
			{
			*aId = list[i].iId; *aW = list[i].iGeometry.Width(); *aH = list[i].iGeometry.Height(); *aPixels = NULL;
			*aToken = list[i].iGpuToken;
			TBuf8<64> t;
			t.Copy(list[i].iTitle);
			const TInt l = Min(t.Length(), aTitleLen - 1);
			Mem::Copy(aTitle, t.Ptr(), l);
			aTitle[l] = 0;
			return 1;
			}
		CFbsBitmap* b = new CFbsBitmap;
		if (!b || b->Duplicate(list[i].iBitmap) != KErrNone)
			{ delete b; continue; }		// (replaced since the list: the next change brings the new one)
		const TSize s = b->SizeInPixels();
		const TInt stride = CFbsBitmap::ScanLineLength(s.iWidth, b->DisplayMode());
		unsigned char* px = (unsigned char*)User::Alloc(s.iWidth * s.iHeight * 4);
		if (px)
			{
			b->BeginDataAccess();
			for (TInt y = 0; y < s.iHeight; ++y)
				Mem::Copy(px + y * s.iWidth * 4, (const TUint8*)b->DataAddress() + y * stride, s.iWidth * 4);
			b->EndDataAccess(ETrue);
			}
		delete b;
		if (!px)
			return KErrNoMemory;
		*aId = list[i].iId; *aW = s.iWidth; *aH = s.iHeight; *aPixels = px;
		TBuf8<64> t;
		t.Copy(list[i].iTitle);
		const TInt l = Min(t.Length(), aTitleLen - 1);
		Mem::Copy(aTitle, t.Ptr(), l);
		aTitle[l] = 0;
		return 1;
		}
	return 0;
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
