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
 *   which also must be licenced for commercial applications:
 *
 *   http://www.juce.com
 *
 *   ===========================================================================
 */

#pragma once

namespace hise { using namespace juce;



#ifndef MIN_FILTER_FREQ
#define MIN_FILTER_FREQ 20.0
#endif

namespace FilterLimitValues
{
	constexpr double lowFrequency = 20.0f;
	constexpr double highFrequency = 20000.0f;

	constexpr double lowQ = 0.3;
	constexpr double highQ = 9.999;

	constexpr double lowGain = -18.0;
	constexpr double highGain = 18.0;
}

struct FilterLimits
{
	static double limit(double minValue, double maxValue, double value);
	static double limitFrequency(double freq);
	static double limitQ(double q);
	static double limitGain(double gain);
};


class FilterHelpers
{
public:

	enum BankType
	{
		Poly,
		Mono,
		numBankTypes
	};

	enum CoefficientType
	{
		LowPass,
		LowPassReso,
		HighPass,
		BandPass,
		Notch,
		Peak,
		LowShelf,
		HighShelf,
		AllPass,
		Ladder24db,
		numCoefficientTypes
	};

	enum class FilterSubType
	{
		MoogFilterSubType,
		LadderSubType,
		StateVariableFilterSubType,
		StateVariableEqSubType,
		StaticBiquadSubType,
		SimpleOnePoleSubType,
		PhaseAllpassSubType,
		RingmodFilterSubType,
		LinkwitzRiley,
		MoogLadderSubType,
		Korg35SubType,
		OberheimSubType,
        WaspFilterSubType,
        TB303FilterSubType,
        DiodeLadderSubType,
		CombFilterSubType,
		FormantFilterSubType,
		DisperserFilterSubType,
		ZDFDiodeLadderSubType,
		DJFilterSubType,
		ScreamBodySubType,
		ApexFilterSubType,
		numFilterSubTypes
	};

	struct RenderData
	{
		RenderData(AudioSampleBuffer& b_, int start_, int size_) :
			b(b_),
			startSample(start_),
			numSamples(size_)
		{
		}

		double applyModValue(double f) const;

		AudioSampleBuffer& b;
		int voiceIndex = -1;

		int startSample = 0;
		int numSamples = -1;
		double freqModValue = 1.0;
		double bipolarDelta = 0.0;
		double gainModValue = 1.0;
		double qModValue = 1.0;
	};
};

// Detects whether a filter subtype implements setDrive(double). Only the drive-
// capable subtypes (apex/diode/tb303) do; the rest ignore the Drive parameter.
template <class T, class = void> struct FilterHasSetDrive : std::false_type {};
template <class T> struct FilterHasSetDrive<T, std::void_t<decltype(std::declval<T>().setDrive(0.0))>> : std::true_type {};

/** A base class for filters with multiple channels.
*
*   It exposes an interface for different filter types which have common methods for
*   setting their parameters, initialisation etc.
*/
template <class FilterSubType> class MultiChannelFilter
{
public:

	using SubType = FilterSubType;

	MultiChannelFilter();
	~MultiChannelFilter() {}

	/** Set the filter type. The type is just a plain integer so it's up to your
	*   filter implementation to decide what is happening with the value. */
	void setType(int newType);

	void setSampleRate(double newSampleRate);
	void setSmoothingTime(double newSmoothingTimeSeconds);
	void setNumChannels(int newNumChannels);
	void setFrequency(double newFrequency);
	void setQ(double newQ);
	void setGain(double newGain);

	/** Drive is applied inside the subtype's processing (in-loop), so it just
	*   forwards to subtypes that implement setDrive; the rest ignore it. */
	void setDrive(double newDrive)
	{
		driveValue = newDrive;
		if constexpr (FilterHasSetDrive<FilterSubType>::value)
			internalFilter.setDrive(newDrive);
	}

	double getGain() const;
	double getFrequency() const;
	double getQ() const;
	int getType() const;

	static Identifier getFilterTypeId();
	FilterDataObject::CoefficientData getApproximateCoefficients() const;

	void reset(int unused = 0);
	void processFrame(float* frameData, int channels);
	StringArray getModes() const;
	void render(FilterHelpers::RenderData& r);

private:

	FilterSubType internalFilter;

	double limit(double value, double minValue, double maxValue);
	bool compareAndSet(double& value, double newValue) noexcept;
	void clearCoefficients();
	void updateEvery64Frame();
	void update(FilterHelpers::RenderData& renderData);

	bool dirty = false;
	bool processed = false;

	double smoothingTimeSeconds = 0.03;
	double sampleRate = 44100.0;
	LinearSmoothedValue<double> frequency = 10000.0;
	LinearSmoothedValue<double> q = 1.0;
	LinearSmoothedValue<double> gain = 1.0;

	double currentFreq;
	double currentGain;
	double currentQ;

	double targetFreq = 1000.0;
	double targetQ = 1.0;
	double targetGain = 1.0;

	int frameCounter = 0;
	int type = -1;
	int numChannels = 2;
	double driveValue = 0.0;
};


#define FORWARD_DECLARE_MULTI_CHANNEL_FILTER(className) extern template class MultiChannelFilter<className>;
#define DEFINE_MULTI_CHANNEL_FILTER(className) template class MultiChannelFilter<className>;

class LinkwitzRiley
{
public:

	enum Mode
	{
		LP,
		HP,
		Allpass
	};

	static FilterHelpers::FilterSubType getFilterType();;
	static Identifier getStaticId();;

	StringArray getModes() const;;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double , double, double) const { return {}; }

	void setType(int type);
	void reset(int numChannels);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int);
	void processFrame(float* frameData, int numChannels);
	double b1co, b2co, b3co, b4co;

	void updateCoefficients(double sampleRate, double frequency, double /*q*/, double /*gain*/);

private:


	SpinLock lock;

	struct Data
	{
		double xm1 = 0.0f;
		double xm2 = 0.0f;
		double xm3 = 0.0f;
		double xm4 = 0.0f;
		double ym1 = 0.0f;
		double ym2 = 0.0f;
		double ym3 = 0.0f;
		double ym4 = 0.0f;
	};

	Data hpData[NUM_MAX_CHANNELS];
	Data lpData[NUM_MAX_CHANNELS];

	float process(float input, int channel);

	struct Coefficients
	{
		Coefficients()
		{
			memset(coefficients, 0, sizeof(double) * 5);
		}

		double coefficients[5];
	};

	Coefficients lpco;
	Coefficients hpco;
	int mode;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(LinkwitzRiley);

class MoogFilterSubType
{
public:

	enum Mode
	{
		OnePole = 0,
		TwoPole,
		FourPole,
		numModes
	};

	static FilterHelpers::FilterSubType getFilterType();;
	static Identifier getStaticId();

	MoogFilterSubType();
	~MoogFilterSubType();;

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double f, double q, double) const { return {}; }
	
	void reset(int numChannels);
	void setMode(int newMode);;
	void setType(int /*t*/);;
	void updateCoefficients(double sampleRate, double frequency, double q, double /*gain*/);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	enum Index
	{
		In1 = 0,
		In2,
		In3,
		In4,
		Out1,
		Out2,
		Out3,
		Out4
	};

	Mode mode;

	double data[NUM_MAX_CHANNELS * 8];

	double* in1;
	double* in2;
	double* in3;
	double* in4;

	double* out1;
	double* out2;
	double* out3;
	double* out4;

	double f;
	double fss;
	double invF;
	double fb;
	double fc;
	double res;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(MoogFilterSubType);

class SimpleOnePoleSubType
{
public:

	enum FilterType
	{
		LP = 0,
		HP,
		numTypes
	};

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	static double getPlotValue(void* obj, bool getMagnitude, double normX);

	FilterCoefficientData getCoefficients(double freqNorm, double, double) const;

	SimpleOnePoleSubType();

	void setType(int t);;
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double /*q*/, double /*gain*/);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	FilterType onePoleType;
	size_t lastChannelAmount = NUM_MAX_CHANNELS;
	float lastValues[NUM_MAX_CHANNELS];
	float a0;
	float b1;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(SimpleOnePoleSubType);
using SimpleOnePole = MultiChannelFilter<SimpleOnePoleSubType>;

class RingmodFilterSubType
{
public:

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	StringArray getModes() const;
	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void reset(int /*numChannels*/);
	void setType(int /*t*/);;
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

	FilterCoefficientData getCoefficients(double, double, double) const { return {}; }

private:

	double uptimeDelta = 0.0;
	double uptime = 0.0;
	float oscGain = 1.0f;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(RingmodFilterSubType);
using RingmodFilter = MultiChannelFilter<RingmodFilterSubType>;


class StaticBiquadSubType
{
public:

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	enum FilterType
	{
		LowPass = 0,
		HighPass,
		LowShelf,
		HighShelf,
		Peak,
		ResoLow,
		numFilterTypes
	};

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double, double, double) const { return {}; }

	void setType(int newType);
	void reset(int numNewChannels);
	void processSamples(AudioSampleBuffer& b, int startSample, int numSamples);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processFrame(float* frameData, int numChannels);

private:

	int numChannels = NUM_MAX_CHANNELS;

	IIRCoefficients currentCoefficients;
	IIRFilter filters[NUM_MAX_CHANNELS];
	FilterType biquadType;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(StaticBiquadSubType);
using StaticBiquad = MultiChannelFilter<StaticBiquadSubType>;


class PhaseAllpassSubType
{
public:

	enum Mode
	{
		Phaser3 = 0,
		Phaser6,
		Phaser12,
		Phaser3Inv,
		Phaser6Inv,
		Phaser12Inv,
		numModes
	};

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();
	StringArray getModes() const;
	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;
	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void processSamples(AudioSampleBuffer& b, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);
	void setType(int t);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void reset(int newNumChannels);

private:

	struct InternalFilter
	{

		class AllpassDelay
		{
		public:

			AllpassDelay();

			static float getDelayCoefficient(float delaySamples);
			void setDelay(float newDelay) noexcept;;
			float getNextSample(float input) noexcept;;
			float delay, currentValue;
		};

		InternalFilter();

		void reset();

		float getNextSample(float input);

		AllpassDelay allpassFilters[12];   // 12 slots; numStages (3, 6, or 12) controls usage
		int numStages = 6;

		float minDelay, maxDelay;
		float feedback;
		float sampleRate;
		float fMin;
		float fMax;
		float rate;

		float currentValue;

		bool  fb_inv    = false;   // true = negate feedback (Inv modes)
	};

	// Current mode
	Mode apMode = Phaser6;

	// Per-channel phaser state
	int numFilters = NUM_MAX_CHANNELS;
	InternalFilter filters[NUM_MAX_CHANNELS];

	// Post-allpass LPHP filter (controlled by Gain parameter)
	float lp_s[NUM_MAX_CHANNELS] = {};
	float lp_coeff = 0.0f;
	float lp_hp_mix = 0.0f;   // -1 = full LP, 0 = passthrough, +1 = full HP

	// Stored for getCoefficients / getPlotValue
	double storedSampleRate = 44100.0;
	double storedFrequency  = 1000.0;
	double storedQ          = 1.0;
	double storedGain       = 1.0;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(PhaseAllpassSubType);
using PhaseAllpass = MultiChannelFilter<PhaseAllpassSubType>;

class LadderSubType
{
public:

	static Identifier getStaticId();
	static FilterHelpers::FilterSubType getFilterType();

	enum FilterType
	{
		LP24 = 0,
		numTypes
	};

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double fnorm, double q, double) const { return {}; }

	StringArray getModes() const;

	void reset(int newNumChannels);
	void setType(int /*t*/);;
	void processSamples(AudioSampleBuffer& b, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double /*gain*/);

private:

	float processSample(float input, int channel);
	float buf[NUM_MAX_CHANNELS][4];

	float cut;
	float res;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(LadderSubType);
using Ladder = MultiChannelFilter<LadderSubType>;




/** A SVF Filter replacement for the eq filters. */
class StateVariableEqSubType
{
public:

	enum Mode
	{
		LowPass,
		HighPass,
		LowShelf,
		HighShelf,
		Peak,
		// Higher-order Butterworth LP/HP built by cascading Cytomic SVF stages.
		// Appended after the original modes so existing presets keep their indices.
		LP24,
		LP36,
		LP48,
		HP24,
		HP36,
		HP48,
		// Extra single-order types and same-Q cascades to reach parity with svf.
		BandPass,
		Notch,
		Morph,
		BP24,
		Notch24,
		numModes
	};

	static Identifier getStaticId();
	static FilterHelpers::FilterSubType getFilterType();
	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;;
	StringArray getModes() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

	// Evaluates the exact Cytomic SVF magnitude/phase response so the graph
	// matches the audio (the cookbook IIR fallback diverges near Nyquist).
	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void reset(int newNumChannels);
	void setType(int newType);
	void processSamples(AudioSampleBuffer& b, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);

private:

	Mode t = Mode::LowPass;

	// Parameters captured for getPlotValue (filled in by getCoefficients).
	double plotFreqNorm = 0.0;
	double plotQ = 1.0;
	double plotGain = 1.0;

	struct Coefficients
	{
		Coefficients();

		void update(double freq, double q, Mode type, double samplerate);
		void setGain(double gainDb);
		void setMorph(double normalisedMorph);
		double computeK(double q, bool useGain = false);
		void computeA(double g, double k);
		void tick();

	private:

		double gain;
		double gain_sqrt;
		double morphPos = 0.5;
		double m[4];
		double a[4];

	public:

		double mp[4];
		double ap[4];

	} coefficients;

	// Extra cascade stages (stages 2..4) for the higher-order LP/HP modes.
	Coefficients coefficients_s[3];

	struct State
	{
		State();
		void reset();
		float tick(float inp, const Coefficients& _coef);

	private:

		double _ic1eq;
		double _ic2eq;

		double v[4];
	};

	State states[NUM_MAX_CHANNELS];
	State states_s[3][NUM_MAX_CHANNELS];
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(StateVariableEqSubType);


class StateVariableFilterSubType
{
public:

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

    enum FilterType
    {
        LP = 0, HP, BP, NOTCH, ALLPASS,
        LP24, LP36, LP48,
        HP24, HP36, HP48,
        BP24, NOTCH24,
        Morph,
        numTypes
    };

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

	static double getPlotValueHighOrder(void* obj, bool getMagnitude, double freqNorm);

	StateVariableFilterSubType();

	void reset(int numChannels);;
	void setType(int t);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	FilterType type;

	float v0z[NUM_MAX_CHANNELS];
	float z1_A[NUM_MAX_CHANNELS];
	float v2[NUM_MAX_CHANNELS];

	float k, g1, g2, g3, g4, x1, x2, gCoeff, RCoeff;

	// Cascaded stage state (index 0 = stage 2, index 1 = stage 3, index 2 = stage 4)
	float v0z_s[3][NUM_MAX_CHANNELS];
	float z1_A_s[3][NUM_MAX_CHANNELS];
	float v2_s[3][NUM_MAX_CHANNELS];

	// Cascaded stage coefficients
	float g1_s[3], g2_s[3], g3_s[3], g4_s[3];
	float k_s[3];

	// Stored for visualisation (getPlotValueHighOrder)
	double storedSampleRate = 44100.0;
	double storedFrequency = 1000.0;
	float storedUserQ = 1.0f;

    double morph = 0.5;
    
	float outputGain = 1.0f;

};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(StateVariableFilterSubType);
using StateVariableFilter = MultiChannelFilter<StateVariableFilterSubType>;


class MoogLadderSubType
{
public:

	enum Mode
	{
		LP_OnePole  = 0,
		LP_TwoPole,
		LP_FourPole,
		HP_OnePole,
		HP_TwoPole,
		HP_FourPole,
		numModes
	};

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	MoogLadderSubType();

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void setType(int t);
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	static inline double fastTanh(double x)
	{
		// Padé approximant of tanh. The raw form is NOT bounded — it grows ~x/9 for
		// large |x|, so it cannot limit the loop gain of a leak-free resonant filter
		// (output runs away → volume spike → Inf). Clamp the argument to its monotonic,
		// bounded region: at |x|=3 the value is exactly ±1.0, so the result is bounded
		// to [-1,1], continuous in slope, and unchanged across the normal drive range.
		x = jlimit(-3.0, 3.0, x);
		const double x2 = x * x;
		return x * (27.0 + x2) / (27.0 + 9.0 * x2);
	}

	Mode mode = LP_FourPole;

	// Per-channel integrator states: s[stage][channel]
	double s[4][NUM_MAX_CHANNELS] = {};
	// Previous stage-4 output for 1-sample-delayed feedback
	double y4prev[NUM_MAX_CHANNELS] = {};

	// Current buffer coefficients
	double G    = 0.0;   // bilinear stage coefficient g/(1+g)
	double fb   = 0.0;   // feedback amount [0, 4]
	double norm = 1.0;   // output normalisation

	// Previous buffer coefficients for interpolation
	double G_prev  = 0.0;
	double fb_prev = 0.0;

	// Stored for graph/coefficient queries
	double storedSampleRate = 44100.0;
	double storedFrequency  = 1000.0;
	float  storedQ          = 0.3f;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(MoogLadderSubType);


// ============================================================================
// Korg35SubType
// Korg 35 LP / HP filter — Simper TPT resolved topology.
//   LP: 2-pole lowpass with resonance (K-feedback into stage-2 output)
//   HP: 1-pole highpass with resonance
// ============================================================================

class Korg35SubType
{
public:

	enum Mode
	{
		LP = 0,
		HP,
		numModes
	};

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	Korg35SubType();

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void setType(int t);
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	Mode mode = LP;

	// Per-channel integrator states
	double s1[NUM_MAX_CHANNELS] = {};
	double s2[NUM_MAX_CHANNELS] = {};
	double s3[NUM_MAX_CHANNELS] = {};

	// Current buffer coefficients
	double G      = 0.0;   // TPT stage coefficient g/(1+g)
	double K      = 0.0;   // resonance coefficient [0, 2]
	double alpha0 = 1.0;   // global normalisation 1/(1 - K*G*(1-G))
	double B3     = 0.0;   // state-3 feedback weight (mode-dependent)
	double B2     = 0.0;   // state-2 feedback weight (mode-dependent)

	// Previous buffer coefficients for per-sample interpolation
	double G_prev      = 0.0;
	double K_prev      = 0.0;
	double alpha0_prev = 1.0;
	double B3_prev     = 0.0;
	double B2_prev     = 0.0;

	// Stored for graph/coefficient queries
	double storedSampleRate = 44100.0;
	double storedFrequency  = 1000.0;
	float  storedQ          = 0.3f;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(Korg35SubType);


// ============================================================================
// OberheimSubType
// Oberheim two-integrator SVF with cubic nonlinear saturation on stage 1.
//   cubicnl(x) = x - x^3/3  applied at the output of integrator 1 (BPF).
//   Four simultaneous outputs: LP, HP, BP, BSF.
//   Coefficients: g = tan(pi*fc/fs), R = 1/(2*Q),
//                 FBs1 = 2*R + g,  alpha0 = 1/(1 + 2*R*g + g^2)
// ============================================================================

class OberheimSubType
{
public:

	enum Mode
	{
		LP = 0,
		HP,
		BP,
		BSF,
        Morph,
		numModes
	};

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	OberheimSubType();

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void setType(int t);
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	static inline double fastTanh(double x)
	{
		// Padé approximant of tanh — monotonically increasing everywhere, so it
		// cannot create the positive feedback that the cubic (v - v³/3) produces
		// when |v| > 1 (where the cubic's slope goes negative and destabilises the filter).
		// BUT the raw Padé is unbounded (grows ~x/9 for large |x|): in BP mode the output
		// IS this value, and with leak-free integrators that lets the resonant loop run
		// away → volume spike → Inf → crash. Clamp the argument to its bounded region:
		// at |x|=3 the value is exactly ±1.0, so the result is bounded to [-1,1].
		x = jlimit(-3.0, 3.0, x);
		const double x2 = x * x;
		return x * (27.0 + x2) / (27.0 + 9.0 * x2);
	}

	Mode mode = LP;

	// Per-channel integrator states
	double s1[NUM_MAX_CHANNELS] = {};
	double s2[NUM_MAX_CHANNELS] = {};

	// Target coefficients (recomputed on every parameter change).
	double g      = 0.0;   // bilinear pre-warped frequency: tan(pi*fc/fs)
	double R      = 0.5;   // damping: 1/(2*Q'). Stored so the smoothed coefficients can be
	                       // recomputed CONSISTENTLY from the glided g (see below).
	double alpha0 = 1.0;   // input normalisation: 1/(1 + 2*R*g + g^2)  (target; for display)
	double FBs1   = 0.0;   // damping feedback: 2*R + g                 (target; for display)
    double morph = 0.5;
    double storedGain = 1.0;

	// Smoothing is applied to the CUTOFF ALONE (g_sm glides toward target g); alpha0/FBs1 are
	// then recomputed from g_sm + R every sample. Smoothing g, alpha0 and FBs1 as three
	// independent one-poles was wrong: alpha0 = 1/(1+2Rg+g^2) is nonlinear in g, so a blend of
	// two valid coefficient sets is itself INVALID — mid-glide the feedback normaliser no longer
	// matches g, transiently pushing the resonant loop gain above unity (BP, the resonant-peak
	// output, blows up first). Recomputing from the single glided g keeps the filter a valid,
	// stable configuration at every intermediate sample.
	double g_sm = 0.0;

	// Per-sample one-pole smoothing coefficient (set from sample rate in updateCoefficients).
	// 0.0 = no smoothing (snaps), used until the first coefficient update runs.
	double coeffSmooth = 0.0;

	// First update after a reset/type-switch: g_sm is stale (constructor g=0 = "zero cutoff"),
	// gliding from it gives a click. Snap g_sm to the freshly computed g once instead.
	bool coeffInit = false;

	// Stored for graph/coefficient queries
	double storedSampleRate = 44100.0;
	double storedFrequency  = 1000.0;
	float  storedQ          = 1.0f;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(OberheimSubType);

// ============================================================================
// WaspFilterSubType
// Approximation of the EDP Wasp CMOS inverter SVF.
//   Based on Oberheim topology but with tanh saturation on both integrators,
//   higher drive character, and LP→BP→HP→BSF morph via Gain parameter.
// ============================================================================

class WaspFilterSubType
{
public:

enum Mode
{
    LP = 0,
    HP,
    BP,
    BSF,
    Morph,
    numModes
};

static FilterHelpers::FilterSubType getFilterType();
static Identifier getStaticId();

WaspFilterSubType();

StringArray getModes() const;
Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;
FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;
static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

void setType(int t);
void reset(int numChannels);
void updateCoefficients(double sampleRate, double frequency, double q, double gain);
void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
void processFrame(float* frameData, int numChannels);

private:

static inline double wasp_nl(double x)
{
    // CMOS inverter approximation: tanh with drive
    //return std::tanh(x * 1.2);
    return x;
}

Mode mode = LP;

double s1[NUM_MAX_CHANNELS] = {};
double s2[NUM_MAX_CHANNELS] = {};

double g      = 0.0;   // target pre-warped cutoff: tan(pi*fc/fs)
double R      = 0.5;   // target damping 1/(2*Q'); stored so the glided coefficients stay consistent
double alpha0 = 1.0;
double FBs1   = 0.0;
double morph  = 0.5;
double storedGain = 1.0;

// Smooth the cutoff alone; recompute alpha0/FBs1 from the glided g + R each sample so the
// filter stays a valid, unconditionally-stable SVF at every intermediate sample. (See the
// OberheimSubType note: smoothing the three coefficients independently de-syncs the feedback
// normaliser from g and lets the resonant loop transiently diverge.)
double g_sm = 0.0;
double coeffSmooth = 0.0;   // per-sample one-pole rate; 0 = snap (until first update)
bool   coeffInit = false;   // snap g_sm to g on the first update after a reset/type-switch

double storedSampleRate = 44100.0;
double storedFrequency  = 1000.0;
float  storedQ          = 1.0f;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(WaspFilterSubType);

// ============================================================================
// TB303FilterSubType
// Roland TB-303 diode ladder filter.
//   LP four-pole ladder with:
//     - Fixed 35 Hz input high-pass (approximates 303 input capacitor)
//     - Asymmetric diode nonlinearity (fastTanh with alternating drive)
//     - Resonance compensation (clamped bass boost at high resonance)
// ============================================================================

class TB303FilterSubType
{
public:

    enum Mode
    {
        LP = 0,
        numModes
    };

    static FilterHelpers::FilterSubType getFilterType();
    static Identifier getStaticId();

    TB303FilterSubType();

    StringArray getModes() const;

    Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

    FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

    static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

    void setType(int t);
    void reset(int numChannels);
    void updateCoefficients(double sampleRate, double frequency, double q, double gain);
    void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
    void processFrame(float* frameData, int numChannels);

private:

    static inline double fastTanh(double x)
    {
        // Padé approximant of tanh, argument-clamped to its bounded region. The raw
        // form grows ~x/9 for large |x|; clamping to |x|=3 (where it equals ±1.0)
        // keeps it bounded to [-1,1] so the ladder feedback can't run away.
        x = jlimit(-3.0, 3.0, x);
        const double x2 = x * x;
        return x * (27.0 + x2) / (27.0 + 9.0 * x2);
    }

    Mode mode = LP;

    // Per-channel integrator states: s[stage][channel]
    double s[4][NUM_MAX_CHANNELS] = {};
    // Previous stage-4 output for 1-sample-delayed feedback
    double y4prev[NUM_MAX_CHANNELS] = {};
    // Input HPF integrator state
    double hp_s[NUM_MAX_CHANNELS] = {};
    // Previous HPF output for resonance compensation
    double hpf_prev[NUM_MAX_CHANNELS] = {};

    // Current buffer coefficients
    double G    = 0.0;   // bilinear stage coefficient g/(1+g)
    double fb   = 0.0;   // feedback amount [0, 3.8]
    double G_hp = 0.0;   // fixed 35 Hz input HPF coefficient

    // Previous buffer coefficients for per-sample interpolation
    double G_prev  = 0.0;
    double fb_prev = 0.0;

    // Stored for graph/coefficient queries
    double storedSampleRate = 44100.0;
    double storedFrequency  = 1000.0;
    float  storedQ          = 0.3f;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(TB303FilterSubType);


// ============================================================================
// DiodeLadderSubType
// Doepfer A-102 / EMS VCS3 style diode ladder filter.
//   Four-pole bilinear TPT ladder with diode saturation nonlinearity:
//     diodeSat(x) = x / (1 + |x|)  — softer and cheaper than tanh.
//   Three modes: LP (four-pole), HP and BP via binomial complement.
// ============================================================================

class DiodeLadderSubType
{
public:

    enum Mode
    {
        LP = 0,
        HP,
        BP,
        numModes
    };

    static FilterHelpers::FilterSubType getFilterType();
    static Identifier getStaticId();

    DiodeLadderSubType();

    StringArray getModes() const;

    Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

    FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

    static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

    void setType(int t);
    void reset(int numChannels);
    void updateCoefficients(double sampleRate, double frequency, double q, double gain);
    void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
    void processFrame(float* frameData, int numChannels);

private:

    static inline double diodeSat(double x)
    {
        return x / (1.0 + std::abs(x));
    }

    Mode mode = LP;

    // Per-channel integrator states: s[stage][channel]
    double s[4][NUM_MAX_CHANNELS] = {};
    // Previous stage-4 output for 1-sample-delayed feedback
    double y4prev[NUM_MAX_CHANNELS] = {};

    // Target coefficients (recomputed on every parameter change)
    double G  = 0.0;   // bilinear stage coefficient g/(1+g) — always in [0,1), so independently
                       // smoothable (unlike the SVF's alpha0, G and fb are not interdependent)
    double fb = 0.0;   // feedback amount [0, 3.8]

    // Smoothed running coefficients (block-size-independent one-pole glide toward the targets).
    // Replaces the old one-block linear interp: too short at small block sizes, and processFrame
    // had none, so a double-click cutoff reset clicked.
    double G_sm  = 0.0;
    double fb_sm = 0.0;
    double coeffSmooth = 0.0;   // per-sample one-pole rate; 0 = snap (until first update)
    bool   coeffInit = false;   // snap the smoothed coeffs to target on first update after reset

    // Stored for graph/coefficient queries
    double storedSampleRate = 44100.0;
    double storedFrequency  = 1000.0;
    float  storedQ          = 0.3f;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(DiodeLadderSubType);

// ============================================================================
// CombFilterSubType
// Comb / flanger filter with all-pass fractional delay interpolation.
//
//   Frequency → delay time: delaySamples = sampleRate / frequency (comb/FF)
//   Gain      → feedback amount: gainToDecibels(gain) / 18.0, clamped ±0.99
//   Q         → one-pole LP cutoff in the Comb/InvComb feedback path:
//                 cutoff = sampleRate * 0.5 * (1 - (q-0.3)/9.6)
//                 lp_coeff = exp(-2π * cutoff / sampleRate)
//               Q=0.3 → cutoff≈Nyquist (light damping, bright tail)
//               Q=9.9 → cutoff≈0 Hz   (heavy damping, dark/short tail)
//
//   Modes:
//     Comb             — feedback comb, positive: peaks at f, 2f, 3f…
//     Inv Comb         — feedback comb, negative: notches at f, 2f, 3f…
//     Feedforward      — feedforward comb: y[n] = x[n] + g·x[n-D]
//     Inv Feedforward  — feedforward comb, negative g
//     Flanger          — feedforward comb, short flanger delay range
//     Inv Flanger      — feedforward flanger, negative g
//     Flanger FB       — feedback flanger, positive
//     Inv Flanger FB   — feedback flanger, negative
//
// Delay line is heap-allocated via std::vector to avoid ~1 MB class-layout
// penalty per polyphonic voice.  Access: delayLine[c * maxDelayLine + pos].
// ============================================================================

class CombFilterSubType
{
public:

	enum Mode
	{
		Comb = 0,
		InvComb,
		FeedforwardComb,
		FeedforwardInvComb,
		Flanger,
		FlangerInv,
		FlangerFB,
		FlangerFBInv,
		numModes
	};

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	CombFilterSubType();

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void setType(int t);
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	static constexpr int maxDelayLine = 8192;

	Mode mode = Comb;

	// Delay lines, heap-allocated to keep object size small.
	// Layout: delayLine[c * maxDelayLine + pos]
	std::vector<double> delayLine;

	// Write head per channel
	int writePos[NUM_MAX_CHANNELS] = {};

	// All-pass interpolator state per channel
	double ap_s[NUM_MAX_CHANNELS]      = {};   // previous output y[n-1]
	double ap_s_prev[NUM_MAX_CHANNELS] = {};   // previous input  x[n-1]

	// Feedback lowpass integrator state per channel (FlangerFB modes)
	double fb_lp_s[NUM_MAX_CHANNELS] = {};

	// One-pole filter state per channel (Comb/InvComb/FF/Flanger feedback or feedforward path)
	float lp_s[NUM_MAX_CHANNELS] = {};

	// Precomputed coefficients
	double ap_coeff    = 0.0;    // all-pass η = (1-d)/(1+d)
	double fb_lp_coeff = 0.0;    // FlangerFB feedback LP speed α
	double fb_gain     = 0.0;    // Comb/FF/Flanger: Q-controlled [0, 0.95]; FlangerFB: Gain-controlled [-0.99, 0.99]
	float  lp_coeff    = 0.0f;   // Comb/FF/Flanger feedback filter pole (from Gain); see fb_is_hp
	bool   fb_is_hp    = false;  // Comb/FF/Flanger: false = LP in signal path, true = HP
	double delayLength  = 100.0; // fractional delay in samples
	int    delayLengthInt = 100; // floor(delayLength)

	// Stored for graph/coefficient queries
	double storedSampleRate = 44100.0;
	double storedFrequency  = 440.0;
	float  storedQ          = 1.0f;
	double storedGain       = 1.0;   // linear amplitude as received from framework
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(CombFilterSubType);


// ============================================================================
// FormantFilterSubType
// Vocal formant filter: 5 parallel bandpass biquads tuned to formant frequencies
// interpolated across 5 vowels (u, o, a, e, i) and 5 voice types.
    //   Mode      → voice type (Bass, Tenor, CTenor, Alto, Soprano)
//   Frequency → vowel position, log scale (20 Hz = u, 20 kHz = i)
//   Q         → bandwidth scaling 1x–3x (Color: narrow=high Q)
//   Gain      → formant frequency shift ±1 octave (Shift)
// ============================================================================

class FormantFilterSubType
{
public:

	enum Mode
	{
		Bass    = 0,
		Tenor   = 1,
		CTenor  = 2,
		Alto    = 3,
		Soprano = 4,
		numModes
	};

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	FormantFilterSubType();

	StringArray getModes() const;
  
	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void setType(int t);
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	// [voiceType][vowel][formant][0=freq, 1=dB, 2=bandwidth]
	// Vowel order: u, o, a, e, i
	static const float formantData[5][5][5][3];

	Mode mode = Bass;

	// Biquad coefficients for 5 formants (precomputed in updateCoefficients)
	float b0[5] = {};   // feed-forward: input
	float b2[5] = {};   // feed-forward: input delayed 2  (b1 = 0 for BP)
	float a1[5] = {};   // feed-back: output delayed 1
	float a2[5] = {};   // feed-back: output delayed 2

	// Per-channel, per-formant Direct Form I state
	float x1[NUM_MAX_CHANNELS][5] = {};
	float x2[NUM_MAX_CHANNELS][5] = {};
	float y1[NUM_MAX_CHANNELS][5] = {};
	float y2[NUM_MAX_CHANNELS][5] = {};

	// Stored for graph/coefficient queries
	double storedSampleRate = 44100.0;
	double storedFrequency  = 440.0;
	float  storedQ          = 0.5f;
	double storedGain       = 1.0;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(FormantFilterSubType);


// ============================================================================
// DisperserFilterSubType
// Cascaded allpass filters: Disperser, Diffuser, and Dense modes.
//
//   Disperser: Biquad allpass stages all tuned to the same frequency/Q.
//     Frequency → centre frequency of group delay peak
//     Q         → sharpness of group delay peak
//     Gain      → number of biquad stages (1–16)
//
//   Diffuser: Schroeder allpass chain with prime-spaced delay times.
//     Frequency → size (inverse: low freq = long delays = spacious wash)
//     Q         → feedback density (0.3 = light, 9.9 = heavy wash)
//     Gain      → number of stages (1–8)
//
//   Dense: Nested Schroeder allpass (inner first-order allpass per stage).
//     Same controls as Diffuser but denser temporal scattering.
// ============================================================================

class DisperserFilterSubType
{
public:

	enum Mode
	{
		Disperser = 0,
		Diffuser,
		Dense,
		numModes
	};

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	DisperserFilterSubType();

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void setType(int t);
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	Mode mode = Disperser;

	// --- Disperser mode state ---
	static const int MAX_STAGES = 16;

	float nb0 = 0.0f;
	float nb1 = 0.0f;
	int   numStages = 1;

	float x1[NUM_MAX_CHANNELS][MAX_STAGES] = {};
	float x2[NUM_MAX_CHANNELS][MAX_STAGES] = {};
	float y1[NUM_MAX_CHANNELS][MAX_STAGES] = {};
	float y2[NUM_MAX_CHANNELS][MAX_STAGES] = {};

	// --- Diffuser / Dense mode state ---
	static const int MAX_DIFFUSER_STAGES = 8;
	static const int maxDiffuserDelay = 1024;

	std::vector<double> diffuserDelay;
	int diffWritePos[NUM_MAX_CHANNELS][MAX_DIFFUSER_STAGES] = {};
	int diffDelayLen[MAX_DIFFUSER_STAGES] = {};
	double diffFeedback = 0.5;
	int numDiffuserStages = 4;

	// Dense mode: inner first-order allpass state (1-sample delay)
	double innerZ[NUM_MAX_CHANNELS][MAX_DIFFUSER_STAGES] = {};
	double innerFB = 0.3;

	// Stored for graph/coefficient queries
	double storedSampleRate = 44100.0;
	double storedFrequency  = 1000.0;
	double storedQ          = 1.0;
	double storedGain       = 1.0;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(DisperserFilterSubType);


// ============================================================================
// ZDFDiodeLadderSubType
// Direct translation of the RNBO gen_01_perform() four-pole ZDF diode ladder.
//   One mode: "ZDF Diode LP"
//   No input saturation — xn = raw input sample, exactly as RNBO source.
//   Q parameter (0.3–9.9) maps to resonance k (0–20).
//   Amplitude compensation: amp_comp = 1 + 0.5 * k.
// ============================================================================

class ZDFDiodeLadderSubType
{
public:
	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	ZDFDiodeLadderSubType();

	StringArray getModes() const;
	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;
	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;
	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void setType(int t);
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:
	// Per-channel state (RNBO z0–z3)
	double z0[NUM_MAX_CHANNELS] = {};
	double z1[NUM_MAX_CHANNELS] = {};
	double z2[NUM_MAX_CHANNELS] = {};
	double z3[NUM_MAX_CHANNELS] = {};

	// Coefficients hoisted from sample loop
	double wc4  = 0.0;    // 2*tan(0.5*pi*freq/sr)
	double wc2  = 0.0;    // wc4^2
	double wc3  = 0.0;    // wc4^3
	double wc4p = 0.0;    // wc4^4
	double b    = 0.0;    // 1/(1+8*wc4+20*wc2+16*wc3+2*wc4p)
	double g    = 0.0;    // 2*wc4p*b
	double k    = 0.0;    // resonance [0, 20]
	double amp_comp = 1.0; // 1 + 0.5*k

	// Previous-buffer values for per-sample interpolation
	double k_prev        = 0.0;
	double amp_comp_prev = 1.0;

	// Stored for graph queries
	double storedSampleRate = 44100.0;
	double storedFrequency  = 1000.0;
	double storedQ          = 0.3;
    
    int mode = 0;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(ZDFDiodeLadderSubType);


// ============================================================================
// DJFilterSubType
// DJ isolator filter: frequency morphs between LP and HP.
//   Low frequency  → lowpass (cuts highs)
//   Centre         → passthrough
//   High frequency → highpass (cuts lows)
//   12 dB/oct (two-pole biquad) with Q controlling resonance.
//   Gain → output gain.
// ============================================================================

class DJFilterSubType
{
public:

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	DJFilterSubType();

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void setType(int t);
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	// Biquad coefficients (Direct Form I)
	float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
	float a1 = 0.0f, a2 = 0.0f;

	// Per-channel Direct Form I state
	float x1[NUM_MAX_CHANNELS] = {};
	float x2[NUM_MAX_CHANNELS] = {};
	float y1[NUM_MAX_CHANNELS] = {};
	float y2[NUM_MAX_CHANNELS] = {};

	float outputGain = 1.0f;

	// Stored for graph/coefficient queries
	double storedSampleRate = 44100.0;
	double storedFrequency  = 1000.0;
	double storedQ          = 1.0;
	double storedGain       = 1.0;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(DJFilterSubType);

// ============================================================================
// ScreamBodySubType
//   Reason Scream-4 "Body" style resonant body filter, voiced to scream.
//   A bank of NUM_RES zero-delay-feedback (TPT) SVF band-pass resonators summed
//   on top of the dry signal (series-peaking → fuller, no parallel-BP thinning),
//   with a tanh drive stage feeding the resonators and an internal envelope
//   follower (Auto) that pushes the resonant frequencies with input dynamics.
//
//   Mode  → Body type A/B/C/D (different resonant-peak voicings)
//   Freq  → Scale  (global multiplier on all peak frequencies = body size)
//   Q     → Reso   (resonator Q + boost depth)
//   Gain  → Drive  (pre-resonator saturation amount; arrives as linear gain)
//   (Auto is a fixed internal depth for now; promoted to a real param in the
//    custom node stage.)
// ============================================================================

class ScreamBodySubType
{
public:

	enum Mode
	{
		BodyA = 0,   // small box / megaphone honk
		BodyB,       // wooden body
		BodyC,       // large hollow / cabinet
		BodyD,       // vocal / talkbox
		numModes
	};

	static constexpr int NUM_RES = 3;

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	ScreamBodySubType();

	StringArray getModes() const;

	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;

	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void setType(int t);
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

private:

	static inline double sat(double x) { return std::tanh(x); }

	// [bodyType][resonator][0=freqHz, 1=Q, 2=boostDb]
	static const double bodyData[numModes][NUM_RES][3];

	Mode mode = BodyA;

	// Per-resonator coefficients (recomputed in updateCoefficients)
	double fc[NUM_RES] = {};   // base centre frequency (Hz), pre Auto-modulation
	double Qf[NUM_RES] = {};   // resonator Q
	double gk[NUM_RES] = {};   // boost gain on the band-pass output (peaking)

	double driveGain = 1.0;    // pre-resonator drive (linear)
	double autoDepth = 0.25;   // internal Auto amount (env → frequency)
	double outComp   = 1.0;    // drive/resonance makeup so it stays gain-sane

	// Per-channel resonator state + envelope follower
	double s1[NUM_MAX_CHANNELS][NUM_RES] = {};
	double s2[NUM_MAX_CHANNELS][NUM_RES] = {};
	double env[NUM_MAX_CHANNELS] = {};

	// Stored for graph/coefficient queries
	double storedSampleRate = 44100.0;
	double storedFrequency  = 1000.0;
	double storedQ          = 1.0;
	double storedGain       = 1.0;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(ScreamBodySubType);

// ============================================================================
// ApexFilterSubType
//   The main multimode filter: Cytomic (TPT) SVF core — same DSP as svf_eq — but
//   with the mode set/ordering matched to StateVariableFilterSubType (svf) so it
//   is a drop-in replacement in networks, PLUS a Serum-style pre-filter Drive
//   (drives the signal INTO the filter) so it adds harmonics while PRESERVING
//   resonance — unlike saturating the resonance feedback, which nulls resonance
//   and dulls the sound.
//
//   Modes are index-for-index identical to svf: LP, HP, BP, Notch, Allpass,
//   LP24/36/48, HP24/36/48, BP24, Notch24, Morph. Gain is used only by Morph
//   (as morph position, like svf); Drive is a separate node parameter.
//
//   Drive = 0 is bit-transparent to the clean Cytomic filter (preDrive → identity
//   at g = 0, and outComp = 1).
// ============================================================================

class ApexFilterSubType
{
public:

	enum FilterType
	{
		LP = 0, HP, BP, NOTCH, ALLPASS,
		LP24, LP36, LP48,
		HP24, HP36, HP48,
		BP24, NOTCH24,
		Morph,
		numTypes
	};

	static FilterHelpers::FilterSubType getFilterType();
	static Identifier getStaticId();

	ApexFilterSubType();

	StringArray getModes() const;
	Array<FilterHelpers::CoefficientType> getCoefficientTypeList() const;

	FilterCoefficientData getCoefficients(double freqNorm, double q, double gain) const;
	static double getPlotValue(void* obj, bool getMagnitude, double freqNorm);

	void setType(int t);
	void setDrive(double newDrive);
	void reset(int numChannels);
	void updateCoefficients(double sampleRate, double frequency, double q, double gain);
	void processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples);
	void processFrame(float* frameData, int numChannels);

	static constexpr int MAX_STAGES = 4;   // up to 4 cascaded SVF stages (48 dB)

private:

	// Pre-filter drive: drives the signal into the filter. Unity full-scale and
	// transparent at g = 0. A bias (b) makes the curve asymmetric so it generates
	// even harmonics (2nd/4th) for tube-like low-end weight, à la Serum; b = 0 is
	// the symmetric tanh (odd harmonics only). Adds harmonics while leaving the
	// filter (and its resonance) linear.
	static inline double preDrive(double x, double g, double b)
	{
		if (g <= 0.0) return x;
		return (std::tanh(g * (x + b)) - std::tanh(g * b)) / std::tanh(g);
	}

	// One Cytomic SVF stage: coefficients (a0..a2, mix m0..m2) + per-channel state.
	struct Stage
	{
		double a0 = 1.0, a1 = 0.0, a2 = 0.0;   // trapezoidal coefficients
		double m0 = 0.0, m1 = 0.0, m2 = 1.0;   // output mix
		double ic1eq[NUM_MAX_CHANNELS] = {};
		double ic2eq[NUM_MAX_CHANNELS] = {};

		void resetState();
		void computeA(double g, double k);
		// Processes one sample (clean Cytomic; drive is applied pre-filter).
		inline double tick(double in, int ch);
	};

	// Resolve the mode into base type + cascade section Qs (mirrors svf).
	void configureStages(double sampleRate, double frequency, double q, double gain);
	static void getMix(FilterType baseType, double k, double& m0, double& m1, double& m2);

	FilterType type = LP;
	int numStages = 1;
	double drive = 0.0;       // 0..1 user drive
	double driveCoeff = 0.0;  // pre-drive gain g (drive * 5)
	double driveBias = 0.0;   // asymmetry bias → even-harmonic low-end weight
	double outComp = 1.0;     // partial makeup (lets drive add some weight/level)

	Stage stages[MAX_STAGES];

	// Morph position (Gain repurposed, matching svf), and stored params for the plot.
	double morphPos = 0.5;
	double storedSampleRate = 44100.0;
	double storedFrequency  = 1000.0;
	double storedQ          = 1.0;
	double storedGain       = 1.0;
};

FORWARD_DECLARE_MULTI_CHANNEL_FILTER(ApexFilterSubType);


}
