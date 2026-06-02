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

#pragma once

namespace scriptnode {
using namespace juce;
using namespace hise;

namespace granulator_vis_detail
{
    // Matches juce::String::hashCode() so the SN_GLOBAL_CABLE hash lines up with
    // the script-side cable name (GlobalRoutingManager::Cable::getRuntimeHash()
    // returns id.hashCode()). ASCII-only cable names only.
    // juce::String::hashCode() computes (31 * result + ch) on a signed int with
    // wraparound. Signed overflow is UB in constexpr, so do the math in uint32_t
    // (well-defined modulo 2^32) and reinterpret as int32_t at the end.
    constexpr int juceHashCode(const char* s)
    {
        uint32_t result = 0;
        while (*s)
            result = 31u * result + (uint32_t)(unsigned char)(*s++);
        return (int)(int32_t)result;
    }

    // Cable name on the script side: Engine.getGlobalRoutingManager().getCable("grainpos")
    constexpr int kGrainPosHash = juceHashCode("grainpos");
}

namespace core
{

/*  granulator_vis

    Visualization variant of core::granulator. Inherits from granulator and adds a
    global cable ("grainpos") that streams grain spawn events to the scripting layer.

    On each process block, any grain starts that fired inside the block are drained
    from a lock-free SPSC ring and packed into a juce::Array<var> as triplets:

        [ p0, velPerSec, durMs ]
        p0        : grain start position (0..1) in the audio file
        velPerSec : 0 (core granulator reads a fixed window, no drift)
        durMs     : grain duration in milliseconds

    Scripting side consumes the cable via:
        Engine.getGlobalRoutingManager().getCable("grainpos").registerDataCallback(...)

    This mirrors the pattern proven out in Griffin_GrainOsc.
*/
struct granulator_vis : public granulator,
                        public routing::global_cable_cpp_manager<SN_GLOBAL_CABLE(granulator_vis_detail::kGrainPosHash)>
{
    SNEX_NODE(granulator_vis);
    SN_DESCRIPTION("A granular synthesiser with grain spawn visualization via global cable");

    enum class GlobalCables { grainpos = 0 };

    static constexpr int kQueueCap = 512;

    granulator_vis()
    {
        onGrainSpawn = [this](float p0, float durMs)
        {
            const int idx = qCount.load(std::memory_order_relaxed);
            if ((unsigned)idx >= (unsigned)kQueueCap)
                return;

            qP0[(size_t)idx] = p0;
            qDurMs[(size_t)idx] = durMs;
            qCount.store(idx + 1, std::memory_order_release);
        };
    }

    ~granulator_vis()
    {
        onGrainSpawn = nullptr;
    }

    void prepare(PrepareSpecs ps)
    {
        granulator::prepare(ps);

        // The script's onInit (which creates the "grainpos" cable) runs after
        // connectToRuntimeTarget, so the cable may not be connected yet at first
        // prepare. Swallow NoGlobalCable — the DSP is unaffected and the cable
        // connects when connectToRuntimeTarget fires again after script compilation.
        try
        {
            routing::global_cable_cpp_manager<SN_GLOBAL_CABLE(granulator_vis_detail::kGrainPosHash)>::prepare(ps);
        }
        catch (scriptnode::Error& e)
        {
            jassert(e.error == scriptnode::Error::NoGlobalCable);
            ignoreUnused(e);
        }
    }

    template <typename ProcessDataType> void process(ProcessDataType& d)
    {
        granulator::process(d);
        flushSpawnEvents();
    }

    template <typename FrameDataType> void processFrame(FrameDataType& d)
    {
        granulator::processFrame(d);
    }

    void connectToRuntimeTarget(bool addConnection, const runtime_target::connection& c) override
    {
        routing::global_cable_cpp_manager<SN_GLOBAL_CABLE(granulator_vis_detail::kGrainPosHash)>::connectToRuntimeTarget(addConnection, c);
    }

private:

    void flushSpawnEvents()
    {
        const int N = qCount.exchange(0, std::memory_order_acq_rel);
        if (N <= 0)
        {
            packed.clearQuick();
            return;
        }

        packed.clearQuick();
        packed.ensureStorageAllocated(N * 3);

        for (int i = 0; i < N; ++i)
        {
            packed.add(var((double)qP0[(size_t)i]));
            packed.add(var(0.0)); // velPerSec: core granulator doesn't drift mid-grain
            packed.add(var((double)qDurMs[(size_t)i]));
        }

        this->sendDataToGlobalCable<GlobalCables::grainpos>(var(packed));
    }

    Array<var> packed;

    std::array<float, kQueueCap> qP0{};
    std::array<float, kQueueCap> qDurMs{};
    std::atomic<int> qCount{ 0 };
};

} // namespace core

}
