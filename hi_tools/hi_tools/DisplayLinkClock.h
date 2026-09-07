/*  ===========================================================================
*
*   DisplayLinkClock (APEX prototype)
*
*   A process-wide clock that ticks on the message thread once per display
*   refresh. On macOS it is driven by a CVDisplayLink, so the tick is locked to
*   the monitor's vsync. Elsewhere a high-resolution timer at ~60 Hz is used.
*
*   The purpose is to give animated UI elements (table playheads, filter graphs,
*   meters) a single render clock: they extrapolate / smooth their state on every
*   frame instead of repainting whenever an audio-thread value happens to arrive.
*
*   ===========================================================================
*/

#pragma once

namespace hise { using namespace juce;

class DisplayLinkClock : private DeletedAtShutdown
{
public:

	struct Listener
	{
		virtual ~Listener() {};

		/** Called on the message thread once per display frame.
		    frameTimeMs is Time::getMillisecondCounterHiRes() for this frame,
		    deltaMs the time since the previous frame. */
		virtual void onDisplayFrame(double frameTimeMs, double deltaMs) = 0;
	};

	DisplayLinkClock();
	~DisplayLinkClock();

	/** Message thread only. Registers for frames; does not start the link by itself. */
	void addListener(Listener* l);

	/** Message thread only. */
	void removeListener(Listener* l);

	/** Message thread only. A listener declares that it currently has something to animate.
	    The link runs while at least one listener needs frames and stops itself after a short
	    hold-off once none does, so an idle UI costs nothing. */
	void setNeedsFrames(Listener* l, bool needsFrames);

	JUCE_DECLARE_SINGLETON(DisplayLinkClock, false)

private:

	struct Pimpl;
	friend struct Pimpl;

	/** Called from the display link / timer thread. */
	void frameArrived();

	/** Message thread. */
	void tick();

	std::unique_ptr<Pimpl> pimpl;
	ListenerList<Listener> listeners;
	Array<Listener*> activeListeners;
	std::atomic<bool> pending { false };
	double lastFrameMs = 0.0;
	double lastDemandMs = 0.0;

	/** How long the link keeps running after the last listener went quiet, so that a knob
	    being nudged repeatedly does not start and stop the link on every move. */
	static constexpr double holdOffMs = 1000.0;

	JUCE_DECLARE_NON_COPYABLE(DisplayLinkClock)
};

}
