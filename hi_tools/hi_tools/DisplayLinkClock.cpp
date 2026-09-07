/*  ===========================================================================
*
*   DisplayLinkClock (APEX prototype) - see header.
*
*   ===========================================================================
*/

#if JUCE_MAC
// Same trick JUCE uses around its Apple includes: MacTypes.h declares a global `Point`
// (and Carbon a `Component`) that would be ambiguous with the juce:: types in this TU.
#define Point CarbonDummyPointName
#define Component CarbonDummyCompName
#include <CoreVideo/CVDisplayLink.h>
#undef Point
#undef Component
#endif

namespace hise { using namespace juce;

#if JUCE_MAC

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

struct DisplayLinkClock::Pimpl
{
	Pimpl(DisplayLinkClock& p) : parent(p)
	{
		if (CVDisplayLinkCreateWithActiveCGDisplays(&link) == kCVReturnSuccess)
			CVDisplayLinkSetOutputCallback(link, &Pimpl::outputCallback, this);
		else
			link = nullptr;
	}

	~Pimpl()
	{
		if (link != nullptr)
		{
			stop();
			CVDisplayLinkRelease(link);
		}
	}

	void start()
	{
		if (link != nullptr && !CVDisplayLinkIsRunning(link))
			CVDisplayLinkStart(link);
	}

	void stop()
	{
		if (link != nullptr && CVDisplayLinkIsRunning(link))
			CVDisplayLinkStop(link);
	}

	bool isRunning() const { return link != nullptr && CVDisplayLinkIsRunning(link); }

	static CVReturn outputCallback(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*, CVOptionFlags, CVOptionFlags*, void* context)
	{
		static_cast<Pimpl*>(context)->parent.frameArrived();
		return kCVReturnSuccess;
	}

	DisplayLinkClock& parent;
	CVDisplayLinkRef link = nullptr;
};

#pragma clang diagnostic pop

#else

/** Fallback: a ~60 Hz high-resolution timer. Not vsync-locked, but at least
    a steady render clock that the animated elements can integrate against. */
struct DisplayLinkClock::Pimpl : public HighResolutionTimer
{
	Pimpl(DisplayLinkClock& p) : parent(p) {}

	~Pimpl() { stopTimer(); }

	void start() { if (!isTimerRunning()) startTimer(16); }
	void stop() { stopTimer(); }
	bool isRunning() const { return isTimerRunning(); }

	void hiResTimerCallback() override { parent.frameArrived(); }

	DisplayLinkClock& parent;
};

#endif

JUCE_IMPLEMENT_SINGLETON(DisplayLinkClock)

DisplayLinkClock::DisplayLinkClock()
{
	pimpl = std::make_unique<Pimpl>(*this);
}

DisplayLinkClock::~DisplayLinkClock()
{
	pimpl = nullptr;
	clearSingletonInstance();
}

void DisplayLinkClock::addListener(Listener* l)
{
	jassert(MessageManager::getInstance()->isThisTheMessageThread());

	listeners.add(l);
}

void DisplayLinkClock::removeListener(Listener* l)
{
	jassert(MessageManager::getInstance()->isThisTheMessageThread());

	listeners.remove(l);
	activeListeners.removeAllInstancesOf(l);
	lastDemandMs = Time::getMillisecondCounterHiRes();
}

void DisplayLinkClock::setNeedsFrames(Listener* l, bool needsFrames)
{
	jassert(MessageManager::getInstance()->isThisTheMessageThread());

	lastDemandMs = Time::getMillisecondCounterHiRes();

	if (needsFrames)
	{
		activeListeners.addIfNotAlreadyThere(l);

		if (!pimpl->isRunning())
		{
			lastFrameMs = 0.0;   // first delta after a restart is one nominal frame, not the idle gap
			pimpl->start();
		}
	}
	else
	{
		activeListeners.removeAllInstancesOf(l);
		// the link stops itself from tick() once the hold-off has passed
	}
}

void DisplayLinkClock::frameArrived()
{
	// Coalesce: if the message thread has not consumed the previous frame yet,
	// this one is simply dropped rather than queued up.
	if (pending.exchange(true))
		return;

	MessageManager::callAsync([]()
	{
		if (auto c = DisplayLinkClock::getInstanceWithoutCreating())
			c->tick();
	});
}

void DisplayLinkClock::tick()
{
	pending.store(false);

	const double now = Time::getMillisecondCounterHiRes();
	const double delta = lastFrameMs > 0.0 ? jlimit(1.0, 100.0, now - lastFrameMs) : (1000.0 / 60.0);
	lastFrameMs = now;

	listeners.call([now, delta](Listener& l) { l.onDisplayFrame(now, delta); });

	if (activeListeners.isEmpty() && (now - lastDemandMs) > holdOffMs)
		pimpl->stop();
}

}
