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

namespace dynamics
{
    
struct DynamicHelpers
{
    static Identifier getId(chunkware_simple::SimpleGate*)
    {
        RETURN_STATIC_IDENTIFIER("gate");
    }
    
    static Identifier getId(chunkware_simple::SimpleComp*)
    {
        RETURN_STATIC_IDENTIFIER("comp");
    }
    
    static Identifier getId(chunkware_simple::SimpleCompRms*)
    {
        RETURN_STATIC_IDENTIFIER("comp_rms");
    }
    
    static Identifier getId(chunkware_simple::SimpleLimit*)
    {
        RETURN_STATIC_IDENTIFIER("limiter");
    }
    
    static String getDescription(const chunkware_simple::SimpleGate*)
    {
        return "A gate effect with the ducking amount as modulation signal";
    }
    
    static String getDescription(const chunkware_simple::SimpleComp*)
    {
        return "A compressor with the ducking amount as modulation signal";
    }
    
    static String getDescription(const chunkware_simple::SimpleLimit*)
    {
        return "A limiter with the ducking amount as modulation signal";
    }
};
    
template <class DynamicProcessorType> class dynamics_wrapper : public HiseDspBase,
															   public data::display_buffer_base<true>
{
public:

	enum class Parameters
	{
		Threshhold,
		Attack,
		Release,
		Ratio,
        Sidechain
	};
    
    enum class SidechainMode
    {
        Disabled = 0,
        Original,
        Sidechain,
        numSideChainModes
    };

	DEFINE_PARAMETERS
	{
		DEF_PARAMETER(Threshhold, dynamics_wrapper);
		DEF_PARAMETER(Attack, dynamics_wrapper);
		DEF_PARAMETER(Release, dynamics_wrapper);
		DEF_PARAMETER(Ratio, dynamics_wrapper);
        DEF_PARAMETER(Sidechain, dynamics_wrapper);
	}
	SN_PARAMETER_MEMBER_FUNCTION;

	static Identifier getStaticId()
    {
        DynamicProcessorType* t = nullptr;
        return DynamicHelpers::getId(t);
    }

	static String getDescription()
    {
        DynamicProcessorType* t = nullptr;
        return DynamicHelpers::getDescription(t);
    }

	static constexpr bool isNormalisedModulation() { return true; };

	SN_GET_SELF_AS_OBJECT(dynamics_wrapper);

	dynamics_wrapper()
    {
        
    };

	SN_EMPTY_HANDLE_EVENT;

	void createParameters(ParameterDataList& data)
    {
        {
            DEFINE_PARAMETERDATA(dynamics_wrapper, Threshhold);
            p.setRange({ -100.0, 0.0, 0.1 });
            p.setSkewForCentre(-12.0);
			p.info.textConverter = parameter::pod::Decibel;
            p.setDefaultValue(0.0);
            data.add(std::move(p));
        }
        
        {
            DEFINE_PARAMETERDATA(dynamics_wrapper, Attack);
            p.setRange({ 0.0, 250.0, 0.1 });
            p.setSkewForCentre(50.0);
			p.info.textConverter = parameter::pod::Time;
            p.setDefaultValue(50.0);
            data.add(std::move(p));
        }
        
        {
            DEFINE_PARAMETERDATA(dynamics_wrapper, Release);
            p.setRange({ 0.0, 250.0, 0.1 });
            p.setSkewForCentre(50.0);
			p.info.textConverter = parameter::pod::Time;
            p.setDefaultValue(50.0);
            data.add(std::move(p));
        }
        
        {
            DEFINE_PARAMETERDATA(dynamics_wrapper, Ratio);
            p.setRange({ 1.0, 32.0, 0.1 });
            p.setSkewForCentre(4.0);
            p.setDefaultValue(1.0);
            data.add(std::move(p));
        }
        
        {
            DEFINE_PARAMETERDATA(dynamics_wrapper, Sidechain);
            p.setParameterValueNames({"Disabled", "Original", "Sidechain"});
            p.setDefaultValue(0.0);
            data.add(std::move(p));
        }
    }
    
	bool handleModulation(double& max) noexcept
    {
		return modValue.getChangedValue(max);
    }
    
	void prepare(PrepareSpecs ps)
    {
		display_buffer_base<true>::prepare(ps);
        obj.setSampleRate(ps.sampleRate);
    }
    
	void reset() noexcept
    {
        obj.initRuntime();
    }

	void updateModValue(int numSamples)
	{
		if (updateOnFrame)
		{
			auto mv = jlimit(0.0, 1.0, 1.0 - obj.getGainReduction());
			modValue.setModValueIfChanged(mv);
			updateBuffer(mv, numSamples);
		}
	}

	template <typename ProcessDataType> void process(ProcessDataType& data)
	{
		{
			ScopedValueSetter<bool> svs(updateOnFrame, false);
			snex::Types::FrameConverters::forwardToFrameStereo(this, data);
		}
		
		updateModValue(data.getNumSamples());
	}

	template <typename FrameDataType> void processFrame(FrameDataType& data) noexcept
	{
        int numSignalChannels = data.size();
        
        if(sidechainMode != SidechainMode::Disabled)
            numSignalChannels /= 2;
        
        
		if (numSignalChannels == 2)
		{
			double values[2] = { data[0], data[1] };
            
            if(sidechainMode == SidechainMode::Sidechain)
            {
                float sideChainValue = hmath::max(hmath::abs(data[2]), hmath::abs(data[3]));
                obj.process(values[0], values[1], sideChainValue);
            }
            else
            {
                obj.process(values[0], values[1]);
            }
            
			
			data[0] = (float)values[0];
			data[1] = (float)values[1];
		}
		else
		{
			double values[2] = { data[0], data[0] };
            
            if(sidechainMode == SidechainMode::Sidechain)
                obj.process(values[0], values[1], data[1]);
            else
                obj.process(values[0], values[1]);
            
			data[0] = (float)values[0];
		}

		updateModValue(1);
	}

	void setThreshhold(double v)
    {
        obj.setThresh(v);
    }
    
	void setAttack(double v)
    {
        obj.setAttack(v);
    }
    
	void setRelease(double v)
    {
        obj.setRelease(v);
    }
    
	void setRatio(double v)
    {
        auto ratio = (v != 0.0) ? 1.0 / v : 1.0;
        obj.setRatio(ratio);
    }
    
    void setSidechain(double newMode)
    {
        sidechainMode = (SidechainMode)(int)newMode;
    }

	DynamicProcessorType obj;
	ModValue modValue;
	bool updateOnFrame = true;
	
    
    SidechainMode sidechainMode = SidechainMode::Disabled;
};

template class dynamics_wrapper<chunkware_simple::SimpleGate>;
template class dynamics_wrapper<chunkware_simple::SimpleComp>;
template class dynamics_wrapper<chunkware_simple::SimpleLimit>;

using gate = dynamics_wrapper<chunkware_simple::SimpleGate>;
using comp = dynamics_wrapper<chunkware_simple::SimpleComp>;
using limiter = dynamics_wrapper<chunkware_simple::SimpleLimit>;

    
template <int NV> class envelope_follower: public data::display_buffer_base<true>,
                         public polyphonic_base
{
public:

	enum class Parameters
	{
		Attack,
		Release,
		ProcessSignal
	};

	DEFINE_PARAMETERS
	{
		DEF_PARAMETER(Attack, envelope_follower);
		DEF_PARAMETER(Release, envelope_follower);
		DEF_PARAMETER(ProcessSignal, envelope_follower)
	}
    SN_PARAMETER_MEMBER_FUNCTION;

	SN_NODE_ID("envelope_follower");
	SN_GET_SELF_AS_OBJECT(envelope_follower);

    envelope_follower():
      polyphonic_base(getStaticId(), false)
    {
        
    }

	static constexpr bool isNormalisedModulation() { return true; }

	SN_EMPTY_HANDLE_EVENT;
    SN_EMPTY_INITIALISE;

	bool handleModulation(double& v) noexcept 
	{ 
		updateBuffer(modValue.getModValue(), lastNumSamples);
		return modValue.getChangedValue(v); 
	};

	void prepare(PrepareSpecs ps) override
    {
		display_buffer_base<true>::prepare(ps);

        for(auto& envelope: envelopes)
            envelope.setSampleRate(ps.sampleRate);
    }
    
	void reset() noexcept
    {
        for(auto& envelope: envelopes)
            envelope.reset();
    }

	template <typename ProcessDataType> void process(ProcessDataType& data)
	{
		if (data.getNumChannels() == 1)
			snex::Types::FrameConverters::forwardToFrameMono(this, data);
		if(data.getNumChannels() == 2)
			snex::Types::FrameConverters::forwardToFrameStereo(this, data);

		lastNumSamples = data.getNumSamples();
	}

	template <typename FrameType> void processFrame(FrameType& data)
	{
		float input = 0.0;

		for (auto& s: data)
			input = Math.max(Math.abs(s), input);

		auto output = envelopes.get().calculateValue(input);
		
		if (processSignal)
		{
			for (auto& s : data)
				s = output;
		}

		modValue.setModValue(output);
		lastNumSamples = 1;
	}

	void setAttack(double v)
    {
        for(auto& envelope: envelopes)
            envelope.setAttackDouble(v);
    }
    
	void setRelease(double v)
    {
        for(auto& envelope: envelopes)
            envelope.setReleaseDouble(v);
    }

	void setProcessSignal(double v)
	{
		processSignal = v > 0.5;
	}

	void createParameters(ParameterDataList& data)
    {
        {
            DEFINE_PARAMETERDATA(envelope_follower, Attack);
            p.setRange({ 0.0, 1000.0, 0.1 });
            p.setSkewForCentre(50.0);
            p.setDefaultValue(20.0);
			p.info.textConverter = parameter::pod::Time;
                
            data.add(std::move(p));
        }
            
        {
            DEFINE_PARAMETERDATA(envelope_follower, Release);
            p.setRange({ 0.0, 1000.0, 0.1 });
            p.setSkewForCentre(50.0);
            p.setDefaultValue(20.0);
			p.info.textConverter = parameter::pod::Time;
                
            data.add(std::move(p));
        }
            
        {
            DEFINE_PARAMETERDATA(envelope_follower, ProcessSignal);
			p.setParameterValueNames({"Off", "On"});
            data.add(std::move(p));
        }
    }

	PolyData<EnvelopeFollower::AttackRelease, NV> envelopes;

	ModValue modValue;
	int lastNumSamples = 0;

	hmath Math;
	
	bool processSignal = false;
};

// A loudness-domain RMS follower. Reuses the true-RMS maths of ott::RMSDetector
// (mean of squares -> sqrt) but replaces its fixed 30 ms boxcar buffer with a
// one-pole exponential average, so the window can be set long (loudness domain)
// without a per-voice ring buffer - the polyphonic instance is templated up to
// NUM_POLYPHONIC_VOICES, where a multi-hundred-ms boxcar would cost a lot of RAM.
// Set ProcessSignal to write the RMS into the signal path (e.g. to divide a
// pre/post tap for compressor auto-gain).
template <int NV> class rms: public data::display_buffer_base<true>, public polyphonic_base
{
public:

	enum class Parameters
	{
		Window,
		ProcessSignal
	};

	DEFINE_PARAMETERS
	{
		DEF_PARAMETER(Window, rms);
		DEF_PARAMETER(ProcessSignal, rms);
	}
	SN_PARAMETER_MEMBER_FUNCTION;

	SN_NODE_ID("rms");
	SN_GET_SELF_AS_OBJECT(rms);
	SN_DESCRIPTION("create a modulation signal from the RMS (loudness) of the input");

	rms():
	  polyphonic_base(getStaticId(), false)
	{

	}

	static constexpr bool isNormalisedModulation() { return true; }

	SN_EMPTY_HANDLE_EVENT;
	SN_EMPTY_INITIALISE;

	bool handleModulation(double& v) noexcept
	{
		updateBuffer(modValue.getModValue(), lastNumSamples);
		return modValue.getChangedValue(v);
	};

	void prepare(PrepareSpecs ps) override
	{
		display_buffer_base<true>::prepare(ps);

		sampleRate = ps.sampleRate;
		meanSquare.prepare(ps);

		updateCoefficient();
		reset();
	}

	void reset() noexcept
	{
		for (auto& ms: meanSquare)
			ms = 0.0;
	}

	template <typename ProcessDataType> void process(ProcessDataType& data)
	{
		if (data.getNumChannels() == 1)
			snex::Types::FrameConverters::forwardToFrameMono(this, data);
		if (data.getNumChannels() == 2)
			snex::Types::FrameConverters::forwardToFrameStereo(this, data);

		lastNumSamples = data.getNumSamples();
	}

	template <typename FrameType> void processFrame(FrameType& data)
	{
		float input = 0.0;

		for (auto& s: data)
			input = Math.max(Math.abs(s), input);

		auto& ms = meanSquare.get();
		auto squared = (double)input * (double)input;
		ms += coeff * (squared - ms);
		ms = jmax(0.0, ms);

		auto output = (float)hmath::sqrt(ms);

		if (processSignal)
		{
			for (auto& s : data)
				s = output;
		}

		modValue.setModValue(output);
		lastNumSamples = 1;
	}

	void setWindow(double newWindowMs)
	{
		windowMs = jlimit(1.0, 5000.0, newWindowMs);
		updateCoefficient();
	}

	void setProcessSignal(double v)
	{
		processSignal = v > 0.5;
	}

	void createParameters(ParameterDataList& data)
	{
		{
			DEFINE_PARAMETERDATA(rms, Window);
			p.setRange({ 1.0, 2000.0, 0.1 });
			p.setSkewForCentre(300.0);
			p.setDefaultValue(300.0);
			p.info.textConverter = parameter::pod::Time;

			data.add(std::move(p));
		}

		{
			DEFINE_PARAMETERDATA(rms, ProcessSignal);
			p.setParameterValueNames({"Off", "On"});
			data.add(std::move(p));
		}
	}

private:

	void updateCoefficient()
	{
		// one-pole time constant: ~63% step response over the window length
		auto tau = (windowMs * 0.001) * sampleRate;
		coeff = tau > 1.0 ? (1.0 - hmath::exp(-1.0 / tau)) : 1.0;
	}

	PolyData<double, NV> meanSquare;

	ModValue modValue;
	int lastNumSamples = 0;

	double sampleRate = 44100.0;
	double windowMs = 300.0;
	double coeff = 1.0;

	bool processSignal = false;

	hmath Math;
};

class ott: public data::display_buffer_base<true>
{
public:

	struct RMSDetector
	{
		void prepare(PrepareSpecs ps);
		void processFrame(span<float, 1>& peak);
		void setEnabled(double v);
		void reset();

	private:

		bool enabled = false;
		dyn<double> data;
		heap<double> memory;
		int arrayIndex = 0;
		double sum = 0.0f;
		double coeff = 0.0f;
	};

	struct updown_envelope_follower
	{
		void prepare(PrepareSpecs ps);;
		void reset();
		void processFrame(span<float, 1>& input);
		void setLowThreshold(double v);
		void setAttack(double attack_);
		void setRelease(double release_);

		double attack;
		double release;

		double sampleRate;
		double atk, rel;
		double lo_t;

		double lastValue;

		void updateCoefficients();
	};

public:

	enum class Parameters
	{
		LowThreshold,
		LowRatio,
		HighThreshold,
		HighRatio,
		Knee,
		Attack,
		Release,
		RMS,
		Depth
	};

	// Metadata Definitions ------------------------------------------------------

	SN_NODE_ID("ott");
	SN_DESCRIPTION("OTT-style updown compressor with Depth control and true upward compression");

	SN_GET_SELF_AS_OBJECT(ott);
	SN_FORWARD_PARAMETER_TO_MEMBER(ott);
	SN_EMPTY_INITIALISE;;
	SN_EMPTY_HANDLE_EVENT;

	static constexpr bool isModNode() { return true; };
	static constexpr bool isPolyphonic() { return false; };
	static constexpr bool hasTail() { return false; };
	static constexpr int getFixChannelAmount() { return 2; };
	
	void prepare(PrepareSpecs specs) override;

	void reset();
	
	template <typename T> void process(T& data)
	{
		static constexpr int NumChannels = getFixChannelAmount();
		auto& fixData = data.template as<ProcessData<NumChannels>>();
		auto fd = fixData.toFrameData();
		
		while(fd.next())
			processFrame(fd.toSpan());
	
		this->updateBuffer(gainRed.getModValue(), data.getNumSamples());
	}

	float getGainReduction(float input);

    template <typename T> void processFrame(T& data)
    {
        span<float, 1> peak;

        for (auto& s : data)
            peak[0] = Math.max(peak[0], Math.abs(s));

        rmsDetector.processFrame(peak);
        envelopeFollower.processFrame(peak);

        auto before = peak[0];

        auto g = getGainReduction(peak[0]);

        if (before > 0.0f)
        {
            g = jlimit(0.0f, 24.0f, g / before);
        }
        else
        {
            g = 0.0f;
        }

        gainRed.setModValue(jlimit(0.0, 1.0, (double)g));

        data *= g;
    }
	
	int handleModulation(double& value)
	{
		return gainRed.getChangedValue(value);
	}
	
	// Parameter Functions -------------------------------------------------------
	
	template <int P> void setParameter(double v)
	{
		switch ((Parameters)P)
		{
		case Parameters::LowThreshold:
			state[P].set(hmath::db2gain((float)v));
			envelopeFollower.setLowThreshold(hmath::db2gain((float)v));
			break;
		case Parameters::HighThreshold:
			state[P].set(hmath::db2gain((float)v));
			break;
		case Parameters::LowRatio:
		case Parameters::HighRatio:
             state[P].set(hmath::range((float)v, 0.0f, 1.0f));
			break;
		case Parameters::Knee:
			state[P].set(hmath::range((float)v, 0.0f, 0.5f));
			break;
		case Parameters::Attack:
			envelopeFollower.setAttack(v);
			break;
		case Parameters::Release:
			envelopeFollower.setRelease(v);
			break;
		case Parameters::RMS:
			rmsDetector.setEnabled(v);
			break;
		case Parameters::Depth:
			state[(int)Parameters::Depth].set(hmath::range((float)v, 0.0f, 1.0f));
			break;
		default:
			jassertfalse;
			break;
		}
	}

	void createParameters(ParameterDataList& data);

	void calculateGraph(block values);

private:

	updown_envelope_follower envelopeFollower;
	ModValue gainRed;
	span<sfloat, (int)Parameters::Depth + 1> state;
	RMSDetector rmsDetector;
};


// Classic (pre-OTT) updown_comp: original downward compression behaviour,
// no Depth param, ratio ranges 0.2..100.
class updown_comp: public data::display_buffer_base<true>
{
public:

	using RMSDetector = ott::RMSDetector;
	using updown_envelope_follower = ott::updown_envelope_follower;

	enum class Parameters
	{
		LowThreshold,
		LowRatio,
		HighThreshold,
		HighRatio,
		Knee,
		Attack,
		Release,
		RMS
	};

	SN_NODE_ID("updown_comp");
	SN_DESCRIPTION("Updown compressor: downward compression above/below thresholds with soft knee");

	SN_GET_SELF_AS_OBJECT(updown_comp);
	SN_FORWARD_PARAMETER_TO_MEMBER(updown_comp);
	SN_EMPTY_INITIALISE;;
	SN_EMPTY_HANDLE_EVENT;

	static constexpr bool isModNode() { return true; };
	static constexpr bool isPolyphonic() { return false; };
	static constexpr bool hasTail() { return false; };
	static constexpr int getFixChannelAmount() { return 2; };

	void prepare(PrepareSpecs specs) override;

	void reset();

	template <typename T> void process(T& data)
	{
		static constexpr int NumChannels = getFixChannelAmount();
		auto& fixData = data.template as<ProcessData<NumChannels>>();
		auto fd = fixData.toFrameData();

		while(fd.next())
			processFrame(fd.toSpan());

		this->updateBuffer(gainRed.getModValue(), data.getNumSamples());
	}

	float getGainReduction(float input);

	template <typename T> void processFrame(T& data)
	{
		span<float, 1> peak;

		for (auto& s : data)
			peak[0] = Math.max(peak[0], Math.abs(s));

		rmsDetector.processFrame(peak);
		envelopeFollower.processFrame(peak);

		auto before = peak[0];

		auto g = getGainReduction(peak[0]);

		if (before > 0.0f)
		{
			g = jlimit(-24.0f, 24.0f, g / before);
		}
		else
		{
			g = 0.0f;
		}

		gainRed.setModValue(jlimit(0.0, 1.0, (double)g));

		data *= g;
	}

	int handleModulation(double& value)
	{
		return gainRed.getChangedValue(value);
	}

	template <int P> void setParameter(double v)
	{
		switch ((Parameters)P)
		{
		case Parameters::LowThreshold:
			state[P].set(hmath::db2gain((float)v));
			envelopeFollower.setLowThreshold(hmath::db2gain((float)v));
			break;
		case Parameters::HighThreshold:
			state[P].set(hmath::db2gain((float)v));
			break;
		case Parameters::LowRatio:
		case Parameters::HighRatio:
			state[P].set(hmath::range((float)v, 0.2f, 100.0f));
			break;
		case Parameters::Knee:
			state[P].set(hmath::range((float)v, 0.0f, 0.5f));
			break;
		case Parameters::Attack:
			envelopeFollower.setAttack(v);
			break;
		case Parameters::Release:
			envelopeFollower.setRelease(v);
			break;
		case Parameters::RMS:
			rmsDetector.setEnabled(v);
			break;
		default:
			jassertfalse;
			break;
		}
	}

	void createParameters(ParameterDataList& data);

	void calculateGraph(block values);

private:

	updown_envelope_follower envelopeFollower;
	ModValue gainRed;
	span<sfloat, (int)Parameters::Knee + 1> state;
	RMSDetector rmsDetector;
};


}


}
