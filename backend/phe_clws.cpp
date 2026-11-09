// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause
//
// phe_clws.cpp -- the WSERV layer of clutter-symbian (phe_clws.h): the session, the window group and the windows of Cogl's onscreens, owned by the thread that runs Clutter (Mesa's
// EGL window surface uses the window's session from the rendering thread, and an RWsSession cannot be shared between threads), the WSERV event and redraw requests, and the
// GMainContext poll function that waits for them and for the main loop's file descriptors together.
//
// The wait (the model of Qt 4's Symbian event dispatcher, QSelectThread): the descriptors go to a helper thread that runs GLib's own poll() on them plus the read end of an
// interrupt pipe and completes a request of the Clutter thread when poll() returns; the Clutter thread waits on its request semaphore for that request, the WSERV event request or
// the redraw request. When WSERV comes first, the helper is interrupted through the pipe and its result is collected before returning, so a descriptor that became ready in the
// meantime is still reported. Request accounting follows EUSER's User::WaitForRequest: every completed request's signal is consumed exactly once (Settle), and signals of other
// requests of this thread that arrive during the wait are given back.
#include <e32std.h>
#include <e32base.h>
#include <e32debug.h>
#include <hal.h>
#include <w32std.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include "phe_clws.h"
#include <fbs.h>
#include "phewinreg.h"

namespace
	{
	struct TReq
		{
		TRequestStatus iSt;
		TBool iActive;			// issued and not yet taken
		TBool iConsumed;		// its completion signal has been taken from the semaphore
		TBool Done() const { return iActive && iSt != KRequestPending; }
		void Issue() { iActive = ETrue; iConsumed = EFalse; }
		};

	RWsSession gWs;
	CWsScreenDevice* gScreen;
	RWindowGroup* gGroup;
	TBool gUp;
	TReq gEv, gRd, gSel, gWt, gTm;	// gTm: the poll timeout (RTimer::HighRes)
	RTimer gTimer;		// gWt: the window registry's watcher count (C4)
	TInt gWtSeen = -1;
	TReq gRn, gRc;		// a compositor's registry waits in this wait set (hildon-port.md 7.49): changes (Notify), content ticks (ContentNotify)
	TUint gRnSeen = 1, gRcSeen;
	TThreadId gMainId;

	// the poll helper
	pthread_mutex_t gMx = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t gCv = PTHREAD_COND_INITIALIZER;
	int gIntr[2] = { -1, -1 };
	PheClwsPollFunc gInner;
	PheClwsPollFD* gJobFds;		// the job: the caller's real descriptors + the interrupt pipe at the end
	unsigned gJobN;
	int gJobTimeout, gJobResult;
	TBool gJobPosted;
	TRequestStatus* gJobSt;
	unsigned gWaits, gWsWake;

	void Settle(TReq& aR)
		{
		if (aR.iActive && !aR.iConsumed)
			{
			User::WaitForRequest(aR.iSt);		// takes exactly one signal (EUSER gives back any other)
			aR.iConsumed = ETrue;
			}
		}

	// wait until one of the requests is complete; the signal taken is booked to one of the completed ones, any other signal is given back
	void WaitAny(TReq* aR[], int aN)
		{
		for (int i = 0; i < aN; i++)
			if (aR[i]->Done())
				return;
		int strays = -1;
		TBool any;
		do
			{
			strays++;
			User::WaitForAnyRequest();
			any = EFalse;
			for (int i = 0; i < aN; i++)
				if (aR[i]->Done()) { any = ETrue; break; }
			}
		while (!any);
		for (int i = 0; i < aN; i++)
			if (aR[i]->Done() && !aR[i]->iConsumed) { aR[i]->iConsumed = ETrue; break; }
		while (strays-- > 0)
			RThread().RequestSignal();
		}

	void* PollThread(void*)
		{
		RThread main;
		main.Open(gMainId);
		for (;;)
			{
			pthread_mutex_lock(&gMx);
			while (!gJobPosted)
				pthread_cond_wait(&gCv, &gMx);
			gJobPosted = EFalse;
			PheClwsPollFD* fds = gJobFds;
			const unsigned n = gJobN;
			const int timeout = gJobTimeout;
			TRequestStatus* st = gJobSt;
			pthread_mutex_unlock(&gMx);
			const int r = gInner(fds, n, timeout);
			char sink[16];
			while (read(gIntr[0], sink, sizeof sink) > 0) ;		// the interrupt is used up
			gJobResult = r;
			main.RequestComplete(st, KErrNone);
			}
		}

	TUint32 HandleOf(RWindow* aW) { return reinterpret_cast<TUint32>(aW); }
	}

extern "C" void phe_clws_log(const char* aMsg) { RDebug::Printf("%s", aMsg); }

extern "C" int phe_clws_init(void)
	{
	if (gUp)
		return 0;
	TInt r = gWs.Connect();
	if (r != KErrNone) return r;
	gScreen = new CWsScreenDevice(gWs);
	if (!gScreen || (r = gScreen->Construct()) != KErrNone) return r ? r : KErrNoMemory;
	gGroup = new RWindowGroup(gWs);
	if (!gGroup || (r = gGroup->Construct(0x434c5747, ETrue)) != KErrNone) return r ? r : KErrNoMemory;
	gGroup->EnableScreenChangeEvents();
	gGroup->EnableFocusChangeEvents();
	gMainId = RThread().Id();
	if ((r = gTimer.CreateLocal()) != KErrNone) return r;
	if (pipe(gIntr) != 0) return KErrGeneral;
	fcntl(gIntr[0], F_SETFL, O_NONBLOCK);
	pthread_t th;
	pthread_attr_t at;
	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, 0x8000);
	if (pthread_create(&th, &at, PollThread, NULL) != 0) return KErrGeneral;
	gEv.Issue(); gWs.EventReady(&gEv.iSt);
	gRd.Issue(); gWs.RedrawReady(&gRd.iSt);
	gWs.Flush();
	gUp = ETrue;
	const TSize s = gScreen->SizeInPixels();
	RDebug::Printf("[clws] WSERV session up, screen %dx%d mode %d", s.iWidth, s.iHeight, gScreen->CurrentScreenMode());
	return 0;
	}

extern "C" void phe_clws_screen_size(int* aW, int* aH)
	{
	const TSize s = gScreen->SizeInPixels();
	*aW = s.iWidth; *aH = s.iHeight;
	}

extern "C" void* phe_clws_window_new(int aX, int aY, int aW, int aH, int aVisible)
	{
	RWindow* w = new RWindow(gWs);
	if (!w) return NULL;
	if (w->Construct(*gGroup, HandleOf(w)) != KErrNone) { delete w; return NULL; }
	w->SetExtent(TPoint(aX, aY), TSize(aW, aH));
	w->SetBackgroundColor(KRgbBlack);
	w->PointerFilter(EPointerFilterMove | EPointerFilterDrag | EPointerFilterEnterExit, 0);		// moves and drags are events (pointer tracking, hover)
	w->SetVisible(aVisible ? ETrue : EFalse);
	w->Activate();
	gWs.Flush();
	return w;
	}

extern "C" void phe_clws_window_set_extent(void* aW, int aX, int aY, int aWidth, int aHeight)
	{
	static_cast<RWindow*>(aW)->SetExtent(TPoint(aX, aY), TSize(aWidth, aHeight));
	gWs.Flush();
	}

extern "C" void phe_clws_window_size(void* aW, int* aWidth, int* aHeight)
	{
	const TSize s = static_cast<RWindow*>(aW)->Size();
	*aWidth = s.iWidth; *aHeight = s.iHeight;
	}

extern "C" void phe_clws_window_set_visible(void* aW, int aVisible)
	{
	static_cast<RWindow*>(aW)->SetVisible(aVisible ? ETrue : EFalse);
	gWs.Flush();
	}

extern "C" void phe_clws_window_raise(void* aW, int aPriority)
	{
	if (aW)
		static_cast<RWindow*>(aW)->SetOrdinalPosition(0);		// the window in front of its siblings (the app's other stages)
	gGroup->SetOrdinalPosition(0, aPriority);
	gWs.Flush();
	// which group has the keys (WSERV gives them to the front group that accepts focus)
	const TInt focus = gWs.GetFocusWindowGroup();
	TBuf<64> name;
	gWs.GetWindowGroupNameFromIdentifier(focus, name);
	TBuf8<64> n8;
	n8.Copy(name.Left(60));
	RDebug::Printf("[clws] raised: our group %d (ordinal %d), focus group %d '%s'", gGroup->Identifier(), gGroup->OrdinalPosition(), focus, (const char*)n8.PtrZ());
	}

extern "C" void phe_clws_window_destroy(void* aW)
	{
	RWindow* w = static_cast<RWindow*>(aW);
	w->Close();
	delete w;
	gWs.Flush();
	}

extern "C" int phe_clws_pending(void)
	{
	return gUp && (gEv.Done() || gRd.Done() || gWt.Done() || gRn.Done() || gRc.Done());
	}

static int Translate(const TWsEvent& aWs, PheClwsEvent* aEv)
	{
	memset(aEv, 0, sizeof *aEv);
	static TInt traced;
	if (traced < 12 && aWs.Type() != EEventPointer && aWs.Type() != EEventDragDrop)
		RDebug::Printf("[clws] event %d (#%d)", aWs.Type(), ++traced);
	aEv->time_ms = (unsigned)(aWs.Time().Int64() / 1000);
	switch (aWs.Type())
		{
	case EEventKeyDown: case EEventKey: case EEventKeyUp:
		{
		const TKeyEvent* k = aWs.Key();
		aEv->type = aWs.Type() == EEventKeyDown ? PHE_CLWS_KEY_DOWN : aWs.Type() == EEventKey ? PHE_CLWS_KEY : PHE_CLWS_KEY_UP;
		aEv->code = k->iCode; aEv->scancode = k->iScanCode; aEv->modifiers = k->iModifiers; aEv->repeats = k->iRepeats;
		return 1;
		}
	case EEventPointer:
		{
		const TPointerEvent* p = aWs.Pointer();
		aEv->type = PHE_CLWS_POINTER; aEv->window = reinterpret_cast<void*>(aWs.Handle());
		aEv->x = p->iPosition.iX; aEv->y = p->iPosition.iY; aEv->ptype = p->iType; aEv->modifiers = p->iModifiers;
		return 1;
		}
	case EEventScreenDeviceChanged:
		{
		gScreen->SetAppScreenMode(gScreen->CurrentScreenMode());		// follow the new mode (rotation): this app's windows are laid out in it
		const TSize s = gScreen->SizeInPixels();
		aEv->type = PHE_CLWS_SCREEN_CHANGED; aEv->w = s.iWidth; aEv->h = s.iHeight;
		RDebug::Printf("[clws] screen mode %d: %dx%d", gScreen->CurrentScreenMode(), s.iWidth, s.iHeight);
		return 1;
		}
	case EEventFocusGained: aEv->type = PHE_CLWS_FOCUS_IN; return 1;
	case EEventFocusLost: aEv->type = PHE_CLWS_FOCUS_OUT; return 1;
	default:
		return 0;
		}
	}

extern "C" int phe_clws_next_event(PheClwsEvent* aEv)
	{
	if (!gUp) return 0;
	while (gEv.Done())
		{
		Settle(gEv);
		TWsEvent ws;
		gWs.GetEvent(ws);
		gEv.Issue(); gWs.EventReady(&gEv.iSt);
		if (Translate(ws, aEv))
			return 1;
		}
	if (gWt.Done())
		{
		Settle(gWt);
		const TInt n = gWt.iSt.Int();
		gWt.iActive = EFalse;
		if (n < 0)
			return 0;						// (the registry went away: no more content wanted)
		gWtSeen = n;
		extern RPheWinReg gReg;
		gWt.Issue(); gReg.Watchers(gWtSeen, gWt.iSt);
		memset(aEv, 0, sizeof *aEv);
		aEv->type = PHE_CLWS_WATCHERS; aEv->x = n;
		return 1;
		}
	if (gRn.Done() || gRc.Done())
		{
		extern RPheWinReg gReg;
		TReq& q = gRn.Done() ? gRn : gRc;
		Settle(q);
		const TInt v = q.iSt.Int();
		q.iActive = EFalse;
		if (v < 0)
			return 0;						// (the registry went away)
		memset(aEv, 0, sizeof *aEv);
		if (&q == &gRn)
			{
			gRnSeen = (TUint)v;
			gRn.Issue(); gReg.Notify(gRnSeen, gRn.iSt);
			aEv->type = PHE_CLWS_REG_CHANGED;
			}
		else
			{
			gRcSeen = (TUint)v;
			gRc.Issue(); gReg.ContentNotify(gRcSeen, gRc.iSt);
			aEv->type = PHE_CLWS_REG_CONTENT;
			}
		aEv->x = v;
		return 1;
		}
	if (gRd.Done())
		{
		Settle(gRd);
		TWsRedrawEvent rev;
		gWs.GetRedraw(rev);
		gRd.Issue(); gWs.RedrawReady(&gRd.iSt);
		RWindow* w = reinterpret_cast<RWindow*>(rev.Handle());
		const TRect r = rev.Rect();
		if (w)
			{
			// the window's content is GL's (the next frame repaints it): the region is validated here and the stage redraws
			w->BeginRedraw(r);
			w->EndRedraw();
			gWs.Flush();
			}
		memset(aEv, 0, sizeof *aEv);
		aEv->type = PHE_CLWS_REDRAW; aEv->window = w; aEv->x = r.iTl.iX; aEv->y = r.iTl.iY; aEv->w = r.Width(); aEv->h = r.Height();
		return 1;
		}
	return 0;
	}

extern "C" void phe_clws_set_inner_poll(PheClwsPollFunc aInner) { gInner = aInner; }

// the fast counter (raw ticks) and its frequency, to check the clocks against each other (hildon-port.md 7.56)
extern "C" unsigned phe_clws_fast_ticks(void) { return User::FastCounter(); }
extern "C" int phe_clws_fast_freq(void) { TInt f = 0; HAL::Get(HAL::EFastCounterFrequency, f); return f; }

// this thread's CPU time in us (RThread::GetCpuTime), or -1 when the kernel does not measure it (hildon-port.md 7.54)
extern "C" long long phe_clws_thread_cpu_us(void)
	{
	TTimeIntervalMicroSeconds t;
	return RThread().GetCpuTime(t) == KErrNone ? t.Int64() : -1;
	}

// time blocked in the wait set, for the backend's bounded frame profile (hildon-port.md 7.50)
extern "C" { unsigned long long phe_clws_wait_us; }
static unsigned long long WaitUs(TUint32 aTicks)
	{
	static TInt freq;
	if (!freq && HAL::Get(HAL::EFastCounterFrequency, freq) != KErrNone)
		freq = 1000;
	return (unsigned long long)aTicks * 1000000ull / (unsigned)freq;
	}

extern "C" int phe_clws_poll(PheClwsPollFD* aFds, unsigned aN, int aTimeout)
	{
	// split: the WSERV pseudo descriptors are answered here, the real ones go to the helper (and the interrupt pipe after them)
	PheClwsPollFD local[64];
	PheClwsPollFD* fds = aN + 1 <= 64 ? local : static_cast<PheClwsPollFD*>(malloc((aN + 1) * sizeof(PheClwsPollFD)));
	unsigned n = 0, nws = 0;
	for (unsigned i = 0; i < aN; i++)
		{
		aFds[i].revents = 0;
		if (aFds[i].fd == PHE_CLWS_POLL_FD) nws++;
		else fds[n++] = aFds[i];
		}
	int result;
	if (!gUp || nws == 0)
		result = gInner(fds, n, aTimeout);
	else if (phe_clws_pending())
		result = gInner(fds, n, 0);
	else
		{
		fds[n].fd = gIntr[0]; fds[n].events = 1 /* G_IO_IN */; fds[n].revents = 0;
		gSel.Issue();
		gSel.iSt = KRequestPending;
		pthread_mutex_lock(&gMx);
		// The timeout is this thread's high-resolution timer, not the helper's poll: Open C's poll/select time out through
		// RTimer::After (localif.cpp, as in the 2010 source), which runs on the 1/64 s user tick, so a 16 ms frame wait took 31 ms
		// (the 33 fps ceiling, hildon-port.md 7.45). RTimer::HighRes runs on the nanokernel tick (1 ms here).
		const TBool timed = aTimeout > 0;
		if (timed)
			{
			gTm.Issue();
			gTimer.HighRes(gTm.iSt, aTimeout * 1000);
			}
		gJobFds = fds; gJobN = n + 1; gJobTimeout = timed ? -1 : aTimeout; gJobSt = &gSel.iSt; gJobPosted = ETrue;
		pthread_cond_signal(&gCv);
		pthread_mutex_unlock(&gMx);
		TReq* all[7] = { &gSel, &gEv, &gRd, &gWt, &gTm, &gRn, &gRc };
		const TUint32 w0 = User::FastCounter();
		WaitAny(all, 7);
		phe_clws_wait_us += WaitUs(User::FastCounter() - w0);
		if (timed)
			{
			if (!gTm.Done())
				gTimer.Cancel();
			Settle(gTm);
			gTm.iActive = EFalse;
			}
		if (!gSel.Done())
			{
			const char c = 1;
			write(gIntr[1], &c, 1);			// WSERV first: stop the helper's poll and collect its result
			gWsWake++;
			}
		Settle(gSel);
		gSel.iActive = EFalse;
		result = gJobResult;
		if (result > 0 && fds[n].revents) result--;			// the interrupt pipe is not the caller's
		gWaits++;
		}
	unsigned j = 0;
	const TBool ws = phe_clws_pending();
	if (result < 0) result = 0;
	for (unsigned i = 0; i < aN; i++)
		{
		if (aFds[i].fd == PHE_CLWS_POLL_FD) { if (ws) { aFds[i].revents = 1; result++; } }
		else aFds[i].revents = fds[j++].revents;
		}
	if (fds != local) free(fds);
	return result;
	}

// ======================================================================== the window registry (C4)

RPheWinReg gReg;
static TBool gRegUp;
struct TRegBitmap { TInt iId; CFbsBitmap* iBitmap; };
static RArray<TRegBitmap> gRegBitmaps;

static TInt RegConnect()
	{
	if (gRegUp)
		return KErrNone;
	if (RFbsSession::GetSession() == NULL)
		{
		const TInt r = RFbsSession::Connect();
		if (r != KErrNone)
			return r;
		}
	const TInt r = gReg.Connect();
	gRegUp = r == KErrNone;
	if (gRegUp)
		{
		gWt.Issue(); gReg.Watchers(-1, gWt.iSt);		// the count now, then each change (an event: PHE_CLWS_WATCHERS)
		}
	return r;
	}

static void FillInfo(TPheWinInfo& aInfo, const char* aTitle, int aType, int aX, int aY, int aW, int aH, unsigned aState)
	{
	aInfo.iType = aType;
	aInfo.iGeometry = TRect(TPoint(aX, aY), TSize(aW, aH));
	aInfo.iState = aState;
	TPtrC8 t((const TUint8*)(aTitle ? aTitle : ""));
	aInfo.iTitle.Copy(t.Left(aInfo.iTitle.MaxLength()));	// (UTF-8 bytes widened: titles are ASCII so far)
	}

extern "C" int phe_clws_reg_window(const char* aTitle, int aType, int aX, int aY, int aW, int aH, unsigned aState)
	{
	TInt r = RegConnect();
	if (r != KErrNone)
		return r;
	TPheWinInfo info;
	FillInfo(info, aTitle, aType, aX, aY, aW, aH, aState);
	r = gReg.Register(info);
	return r < 0 ? r : info.iId;
	}

extern "C" int phe_clws_reg_update(int aId, const char* aTitle, int aType, int aX, int aY, int aW, int aH, unsigned aState)
	{
	if (!gRegUp)
		return KErrNotReady;
	TPheWinInfo info;
	info.iId = aId;
	FillInfo(info, aTitle, aType, aX, aY, aW, aH, aState);
	return gReg.Update(info);
	}

extern "C" int phe_clws_reg_content(int aId, int aW, int aH, const void* aPixels, int aStride, int aDx, int aDy, int aDw, int aDh)
	{
	if (!gRegUp)
		return KErrNotReady;
	TInt i = 0;
	while (i < gRegBitmaps.Count() && gRegBitmaps[i].iId != aId)
		++i;
	if (i == gRegBitmaps.Count())
		{
		TRegBitmap b = { aId, new CFbsBitmap };
		if (!b.iBitmap || gRegBitmaps.Append(b) != KErrNone)
			{ delete b.iBitmap; return KErrNoMemory; }
		}
	CFbsBitmap* bmp = gRegBitmaps[i].iBitmap;
	if (bmp->Handle() == 0 || bmp->SizeInPixels() != TSize(aW, aH))
		{
		// a new size = a new bitmap (the old handle stays valid for an importer until it duplicates the new one)
		bmp->Reset();
		const TInt r = bmp->Create(TSize(aW, aH), EColor16MAP);
		if (r != KErrNone)
			return r;
		}
	bmp->BeginDataAccess();
	TUint8* dst = (TUint8*)bmp->DataAddress();
	const TInt dstride = CFbsBitmap::ScanLineLength(aW, EColor16MAP);
	for (TInt y = 0; y < aH; ++y)
		Mem::Copy(dst + y * dstride, (const TUint8*)aPixels + y * aStride, aW * 4);
	bmp->EndDataAccess(EFalse);
	return gReg.Content(aId, bmp->Handle(), TRect(TPoint(aDx, aDy), TSize(aDw, aDh)));
	}

// the GPU path (hildon-port.md 7.47): the content is an exported texture; no bitmap is published (an importer that cannot take the
// token gets no content until the backend falls back to bitmaps)
extern "C" int phe_clws_reg_content_gpu(int aId, int aToken, int aW, int aH, int aDx, int aDy, int aDw, int aDh)
	{
	(void)aW; (void)aH;
	if (!gRegUp)
		return KErrNotReady;
	return gReg.Content(aId, 0, TRect(TPoint(aDx, aDy), TSize(aDw, aDh)), aToken);
	}

// a compositor (hildon-port.md 7.49): registry changes and content ticks arrive as events of this wait set (PHE_CLWS_REG_CHANGED /
// PHE_CLWS_REG_CONTENT), on the main loop, with no helper thread. The first change event comes at once (the serial starts at 1)
extern "C" int phe_clws_frame_watch(void)
	{
	const TInt r = RegConnect();
	if (r != KErrNone || gRn.iActive)
		return r;
	gRn.Issue(); gReg.Notify(gRnSeen, gRn.iSt);
	gRc.Issue(); gReg.ContentNotify(gRcSeen, gRc.iSt);
	return KErrNone;
	}

extern "C" int phe_clws_reg_list(void* aBuf, int aBytes)
	{
	if (!gRegUp)
		return KErrNotReady;
	TPtr8 p((TUint8*)aBuf, 0, aBytes);
	return gReg.List(p);
	}

extern "C" int phe_clws_reg_subscribe(int aId, int aMode)
	{
	return gRegUp ? gReg.Subscribe(aId, aMode) : KErrNotReady;
	}

extern "C" int phe_clws_reg_subs(int aId, int* aSubscribers, int* aComposited)
	{
	*aSubscribers = *aComposited = 0;
	if (!gRegUp)
		return KErrNotReady;
	TPheWinInfo list[16];
	TPtr8 p((TUint8*)list, 0, sizeof(list));
	const TInt n = gReg.List(p);
	if (n < 0)
		return n;
	for (TInt i = 0; i < n && i < 16; ++i)
		if (list[i].iId == aId)
			{
			*aSubscribers = list[i].iSubscribers;
			*aComposited = list[i].iComposited;
			return KErrNone;
			}
	return KErrNotFound;
	}

extern "C" void phe_clws_reg_remove(int aId)
	{
	if (!gRegUp)
		return;
	gReg.Remove(aId);
	for (TInt i = 0; i < gRegBitmaps.Count(); ++i)
		if (gRegBitmaps[i].iId == aId)
			{
			delete gRegBitmaps[i].iBitmap;
			gRegBitmaps.Remove(i);
			break;
			}
	}
