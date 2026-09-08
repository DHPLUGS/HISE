/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/



namespace hise {
namespace dispatch {	
using namespace juce;

Source::Source(SourceManager& parent_, SourceOwner& owner_, const HashedCharPtr& sourceId_):
	Suspendable(parent_.getRootObject(), &parent_),
	parent(parent_),
	owner(owner_),
	sourceId(sourceId_)
{
	getRootObject().addTypedChild(this);
	parent.addSource(this);
}

Source::~Source()
{
	getRootObject().removeTypedChild(this);
	parent.removeSource(this);
	cleared();
}




void Source::flushChanges(DispatchType n)
{
    

    
    
	//TRACE_FLUSH(getDispatchId());

	if(currentState != State::Running)
		return;

	// APEX: nothing was sent since the last flush -> nothing to walk. The plain async flush is
	// the last one of a UI timer tick and its slot flush also drains the sync / hi-priority
	// bitmaps, so that is where the flag is consumed; the hi-priority pass only peeks. A change
	// that lands between the peek and the walk is picked up by the next tick.
	if(n == DispatchType::sendNotificationAsync)
	{
		if(!pendingFlush.exchange(false, std::memory_order_acq_rel))
			return;
	}
	else if(!pendingFlush.load(std::memory_order_acquire))
		return;

#if PERFETTO
    if(n == DispatchType::sendNotificationAsyncHiPriority)
    {
        StringBuilder b;
        b << "flush " << getDispatchId();
        TRACE_EVENT_BEGIN("dispatch", DYNAMIC_STRING_BUILDER(b));
    }
#endif
    
	for(int i = 0; i < getNumSlotSenders(); i++)
	{
		getSlotSender(i)->flush(n);
	}
    
#if PERFETTO
    if(n == DispatchType::sendNotificationAsyncHiPriority)
    {
        TRACE_EVENT_END("dispatch");
    }
#endif
}

ListenerQueue* Source::getListenerQueue(uint8 slotSenderIndex, DispatchType n)
{
	return getSlotSender(slotSenderIndex)->getListenerQueue(n);
}

void Source::setState(const HashedPath& p, State newState)
{
	if(!matchesPath(p))
		return;
	
	currentState = newState;

	forEachListenerQueue(DispatchType::sendNotification, [p, newState, this](uint8 s, DispatchType n, ListenerQueue* q)
	{
		auto sender = this->getSlotSender(s);

		if(sender->matchesPath(p, n))
		{
			q->setQueueState(newState);

			if(newState == State::Running && n == sendNotificationSync)
			{
				sender->flush(n);
			}
		}
	});
}

void Source::forEachListenerQueue(DispatchType n, const std::function<void(uint8, DispatchType, ListenerQueue* q)>& f)
{
	for(int i = 0; i < getNumSlotSenders(); i++)
	{
		if (n != DispatchType::sendNotification)
		{
			f(i, n, getListenerQueue(i, n));
			continue;
		}
		else
		{
			f(i, DispatchType::sendNotificationSync, getListenerQueue(i, DispatchType::sendNotificationSync));
			f(i, DispatchType::sendNotificationAsync, getListenerQueue(i, DispatchType::sendNotificationAsync));
			f(i, DispatchType::sendNotificationAsyncHiPriority, getListenerQueue(i, DispatchType::sendNotificationAsyncHiPriority));
		}
	}
}
} // dispatch
} // hise
