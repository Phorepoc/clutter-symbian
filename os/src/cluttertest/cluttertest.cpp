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

static TInt OwnerCheck();
static TInt HungOwner();
extern "C" void phe_decor_install(void);

GLDEF_C TInt E32Main()
	{
	CTrapCleanup* cleanup = CTrapCleanup::New();
	{
	TBuf<64> cmd;
	User::CommandLine(cmd);
	if (cmd.Find(_L("ownercheck")) == 0)		// the owner-side check's second process (no Clutter)
		{
		const TInt r = OwnerCheck();
		delete cleanup;
		return r;
		}
	if (cmd.Find(_L("hungowner")) == 0)		// the decorations' unresponsive owner (no Clutter)
		{
		const TInt r = HungOwner();
		delete cleanup;
		return r;
		}
	if (cmd.Find(_L("frame")) == 0)
		phe_decor_install();					// the frame's decorations (phedecor.c), on the backend's registry events
	else if (cmd.Find(_L("hung")) >= 0)
		{
		RProcess p;
		if (p.Create(_L("cluttertest.exe"), _L("hungowner")) == KErrNone) { p.Resume(); p.Close(); }
		}
	}
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
				static TInt listLogged;
				if (listLogged++ < 3)
					RDebug::Printf("[cluttertest-frame] window %d: bitmap %d %dx%d mode %d -> pixels %s", w.iId, w.iBitmap, s.iWidth,
						s.iHeight, b->DisplayMode(), f.iPx ? "copied" : "NOT allocated");
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
extern "C" int phe_cmd_has(const char* aWord);
extern "C" int phe_frame_spawn(void)
	{
	RProcess p;
	// "registry-split": the frame starts in the split layout (phedecor.c split_tick)
	TInt r = p.Create(_L("cluttertest.exe"), phe_cmd_has("split") ? TPtrC(_L("frame-split")) : TPtrC(_L("frame")));
	if (r == KErrNone) { p.Resume(); p.Close(); }
	return r;
	}

extern "C" int phe_owner_spawn(void)
	{
	RProcess p;
	TInt r = p.Create(_L("cluttertest.exe"), _L("ownercheck"));
	if (r == KErrNone) { p.Resume(); p.Close(); }
	return r;
	}

extern "C" int phe_is_frame(void)
	{
	TBuf<64> cmd;	// (longer than any test command line: a short buffer panics KERN-EXEC 35)
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

// ======================================================================== the owner side of the backend (hildon-port.md 7.64)
// "cluttertest|owner" runs a resizable 400x300 stage (minimum 200x150) and starts "cluttertest ownercheck", which plays a frame
// with the registry API only: it finds the stage's window, checks the hints the backend set, sends a resize (clamped by the
// registry) and expects the backend's Update with the new size, pings and expects the main loop's pong, and sends focus.
#define OCHECK(c) do { if (!(c)) { RDebug::Printf("[cluttertest-owner] FAIL line %d: %s", __LINE__, #c); return KErrGeneral; } } while (0)

static TInt OwnerFind(RPheWinReg& aReg, TPheWinInfo& aOut)
	{
	TPheWinInfo list[8];
	TPtr8 p((TUint8*)list, 0, sizeof list);
	const TInt n = aReg.List(p);
	for (TInt i = 0; i < n && i < 8; ++i)
		if (list[i].iTitle == _L("cluttertest"))
			{ aOut = list[i]; return KErrNone; }
	return KErrNotFound;
	}

static TInt OwnerCheck()
	{
	RPheWinReg reg;
	OCHECK(reg.Connect() == KErrNone);
	TPheWinInfo w;
	TInt tries = 0;
	for (; tries < 100 && OwnerFind(reg, w) != KErrNone; ++tries)
		User::After(100000);
	OCHECK(tries < 100);
	OCHECK(reg.Subscribe(w.iId, EPheWinWatch) == KErrNone);
	TPheWinHints h;
	for (tries = 0; tries < 50; ++tries)		// (the hints follow the stage's resize / minimum size calls)
		{
		OCHECK(reg.Hints(w.iId, h) == KErrNone);
		if (h.iMinSize == TSize(200, 150) && h.iPreferredSize == TSize(400, 300))
			break;
		User::After(100000);
		}
	RDebug::Printf("[cluttertest-owner] window %d: %dx%d, min %dx%d max %dx%d preferred %dx%d", w.iId, w.iGeometry.Width(), w.iGeometry.Height(),
		h.iMinSize.iWidth, h.iMinSize.iHeight, h.iMaxSize.iWidth, h.iMaxSize.iHeight, h.iPreferredSize.iWidth, h.iPreferredSize.iHeight);
	OCHECK(tries < 50);
	OCHECK(h.iMaxSize == TSize(0, 0));								// user-resizable: no maximum

	OCHECK(reg.Resize(w.iId, TRect(TPoint(0, 0), TSize(500, 100))) == KErrNone);	// 100 high: the registry clamps it to 150
	for (tries = 0; tries < 50; ++tries)
		{
		OCHECK(OwnerFind(reg, w) == KErrNone);
		if (w.iGeometry.Size() == TSize(500, 150))
			break;
		User::After(100000);
		}
	RDebug::Printf("[cluttertest-owner] after resize: %dx%d", w.iGeometry.Width(), w.iGeometry.Height());
	OCHECK(tries < 50);

	OCHECK(reg.Ping(w.iId) == KErrNone);
	for (tries = 0; tries < 50; ++tries)
		{
		OCHECK(reg.Hints(w.iId, h) == KErrNone);
		if (h.iPingPending == 0)
			break;
		User::After(20000);
		}
	RDebug::Printf("[cluttertest-owner] ping answered after ~%d ms", tries * 20);
	OCHECK(tries < 50);
	OCHECK(reg.Action(w.iId, EPheWinActFocus) == KErrNone);
	RDebug::Printf("[cluttertest-owner] PASS: hints from the stage (min, preferred, resizable), resize -> stage -> Update, ping -> pong on the main loop, focus sent");
	reg.Close();
	return KErrNone;
	}

// ======================================================================== decorations' registry access (hildon-port.md 7.66 / 7.68)
// The frame's decorations (phedecor.c) read the same registry session as the frame's compositing (phe_clws.cpp gReg): each
// listed window's title, type, state and geometry, its hints (fixed size?) and the ping state; and send the buttons' actions.
extern RPheWinReg gReg;
struct PheDecorWin { int iId, iX, iY, iW, iH, iType, iState, iFixed, iPingPending, iPingAgeMs, iNoPing; char iTitle[64]; };

extern "C" int phe_decor_query(PheDecorWin* aOut, int aMax)
	{
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
		PheDecorWin& d = aOut[out++];
		Mem::FillZ(&d, sizeof(d));
		d.iId = w.iId; d.iX = w.iGeometry.iTl.iX; d.iY = w.iGeometry.iTl.iY; d.iW = w.iGeometry.Width(); d.iH = w.iGeometry.Height();
		d.iType = w.iType; d.iState = w.iState;
#ifdef PHE_WIN_NOPING
		d.iNoPing = (w.iState & PHE_WIN_NOPING) != 0;	// the registry's "not ping-capable" flag (a published classic screen): never "not responding"
#endif
		TPheWinHints h;
		if (gReg.Hints(w.iId, h) == KErrNone)
			{
			d.iFixed = h.iMaxSize.iWidth && h.iMaxSize == h.iMinSize;
			d.iPingPending = h.iPingPending; d.iPingAgeMs = h.iPingAgeMs;
			}
		TPtr8 t((TUint8*)d.iTitle, 0, sizeof(d.iTitle) - 1);
		t.Copy(w.iTitle.Left(sizeof(d.iTitle) - 1));
		t.ZeroTerminate();
		}
	return out;
	}

extern "C" int phe_decor_action(int aId, int aAction) { return gReg.Action(aId, aAction); }
extern "C" int phe_decor_ping(int aId) { return gReg.Ping(aId); }
extern "C" int phe_decor_resize(int aId, int aX, int aY, int aW, int aH) { return gReg.Resize(aId, TRect(TPoint(aX, aY), TSize(aW, aH))); }
extern "C" void phe_decor_kill(int aId)
	{
	TPheWinInfo list[16];
	const TInt n = phe_clws_reg_list(list, sizeof(list));
	for (TInt i = 0; i < n && i < 16; ++i)
		if (list[i].iId == aId)
			{
			RProcess p;
			if (p.Open(TProcessId(list[i].iOwner)) == KErrNone)
				{
				RDebug::Printf("[phedecor] window %d: killing its unresponsive owner (process %u)", aId, list[i].iOwner);
				p.Kill(KErrNotReady);
				p.Close();
				}
			}
	}

// "cluttertest hungowner": a window owner whose event loop never runs: it registers a window with content, then blocks forever
// without ever taking an action or answering a ping (the decorations' not-responding case)
static TInt HungOwner()
	{
	RFbsSession::Connect();
	RPheWinReg reg;
	if (reg.Connect() != KErrNone)
		return KErrNotFound;
	TPheWinInfo w;
	w.iGeometry = TRect(TPoint(560, 260), TSize(360, 240));
	w.iTitle = _L("Hung app");
	if (reg.Register(w) != KErrNone)
		return KErrGeneral;
	CFbsBitmap* bmp = new CFbsBitmap;
	if (!bmp || bmp->Create(w.iGeometry.Size(), EColor16MAP) != KErrNone)
		return KErrNoMemory;
	bmp->BeginDataAccess();
	TUint32* px = (TUint32*)bmp->DataAddress();
	for (TInt i = 0; i < w.iGeometry.Width() * w.iGeometry.Height(); ++i)
		px[i] = 0xFFE0E1E2u;		// Etesian page grey
	bmp->EndDataAccess(EFalse);
	reg.Content(w.iId, bmp->Handle(), TRect(w.iGeometry.Size()));
	RDebug::Printf("[cluttertest-hung] window %d registered; never answering", w.iId);
	for (;;)
		User::After(10000000);
	}

extern "C" void phe_decor_install(void);
