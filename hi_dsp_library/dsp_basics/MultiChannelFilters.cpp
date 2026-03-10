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

namespace hise
{
using namespace juce;


double FilterLimits::limit(double minValue, double maxValue, double value)
{
#if HISE_IOS
	return jlimit<double>(minValue, maxValue, value);
#else
	_mm_store_sd(&value, _mm_min_sd(_mm_max_sd(_mm_set_sd(value), _mm_set_sd(minValue)), _mm_set_sd(maxValue)));
	return value;
#endif
}

double FilterLimits::limitFrequency(double freq)
{
	return limit(FilterLimitValues::lowFrequency, FilterLimitValues::highFrequency, freq);
}

double FilterLimits::limitQ(double q)
{
	return limit(FilterLimitValues::lowQ, FilterLimitValues::highQ, q);
}

double FilterLimits::limitGain(double gain)
{
	return limit(FilterLimitValues::lowGain, FilterLimitValues::highGain, gain);
}


template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::setType(int newType)
{
	if (type != newType)
	{
		type = newType;
		internalFilter.setType(type);
		clearCoefficients();
	}
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::setSampleRate(double newSampleRate)
{
	sampleRate = newSampleRate;

	frequency.reset(newSampleRate / 64.0, smoothingTimeSeconds);
	q.reset(newSampleRate / 64.0, smoothingTimeSeconds);
	gain.reset(newSampleRate / 64.0, smoothingTimeSeconds);

	reset();
	clearCoefficients();
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::setSmoothingTime(double newSmoothingTimeSeconds)
{
	smoothingTimeSeconds = newSmoothingTimeSeconds;

	if(sampleRate > 0.0)
		setSampleRate(sampleRate);
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::setNumChannels(int newNumChannels)
{
	numChannels = jlimit<int>(0, NUM_MAX_CHANNELS, newNumChannels);
	reset();
	clearCoefficients();
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::setFrequency(double newFrequency)
{
	targetFreq = FilterLimits::limitFrequency(newFrequency);
	frequency.setValue(targetFreq, !processed);
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::setQ(double newQ)
{
	targetQ = FilterLimits::limitQ(newQ);
	q.setValue(targetQ, !processed);
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::setGain(double newGain)
{
	targetGain = FilterLimits::limitGain(newGain);
	gain.setValue(targetGain, !processed);
}

template <class FilterSubType>
double MultiChannelFilter<FilterSubType>::getQ() const
{
	return targetQ;
}

template <class FilterSubType>
double MultiChannelFilter<FilterSubType>::getFrequency() const
{
	return targetFreq;
}

template <class FilterSubType>
double MultiChannelFilter<FilterSubType>::getGain() const
{
	return targetGain;
}

template <class FilterSubType>
int MultiChannelFilter<FilterSubType>::getType() const
{
	return type;
}

template <class FilterSubType>
Identifier MultiChannelFilter<FilterSubType>::getFilterTypeId()
{
	return FilterSubType::getStaticId();
}

template <class FilterSubType>
FilterDataObject::CoefficientData MultiChannelFilter<FilterSubType>::getApproximateCoefficients() const
{
	auto cType = internalFilter.getCoefficientTypeList();

	auto m = cType[type];

	auto f_ = (double)targetFreq; //(double)frequency.getCurrentValue();
	auto q_ = (double)targetQ; //(double)q.getCurrentValue();
	auto g_ = (float)targetGain; //(float)gain.getCurrentValue();

	auto fnorm = f_ / sampleRate;

	FilterCoefficientData cd = internalFilter.getCoefficients(fnorm, q_, g_);

	if(cd.hasCustomFunction())
		return cd;

	switch (m)
	{
	case FilterHelpers::AllPass:		return { IIRCoefficients::makeAllPass(sampleRate, f_, q_), 1 };
	case FilterHelpers::LowPassReso:	return { IIRCoefficients::makeLowPass(sampleRate, f_, q_), 1 };
	case FilterHelpers::Ladder24db:	    return { IIRCoefficients::makeLowPass(sampleRate, f_, q_), 2 };
	case FilterHelpers::LowPass:		return { IIRCoefficients::makeLowPass(sampleRate, f_), 1 };
	case FilterHelpers::BandPass:		return { IIRCoefficients::makeBandPass(sampleRate, f_, q_), 1 };
	case FilterHelpers::Notch:			return { IIRCoefficients::makeNotchFilter(sampleRate, f_, q_), 1 };
	case FilterHelpers::LowShelf:		return { IIRCoefficients::makeLowShelf(sampleRate, f_, q_, g_), 1 };
	case FilterHelpers::HighShelf:		return { IIRCoefficients::makeHighShelf(sampleRate, f_, q_, g_), 1 };
	case FilterHelpers::Peak:			return { IIRCoefficients::makePeakFilter(sampleRate, f_, q_, g_), 1 };
	case FilterHelpers::HighPass:		return { IIRCoefficients::makeHighPass(sampleRate, f_, q_), 1 };
	default:							return { IIRCoefficients::makeLowPass(sampleRate, f_), 1 };
	}
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::reset(int unused/*=0*/)
{
	ignoreUnused(unused);
	frequency.setValueWithoutSmoothing(targetFreq);
	gain.setValueWithoutSmoothing(targetGain);
	q.setValueWithoutSmoothing(targetQ);

	processed = false;

	internalFilter.reset(numChannels);
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::processFrame(float* frameData, int channels)
{
	jassert(channels == numChannels);
	processed = true;

	if (--frameCounter <= 0)
	{
		frameCounter = 64;
		updateEvery64Frame();
	}

	internalFilter.processFrame(frameData, channels);
}

template <class FilterSubType>
StringArray MultiChannelFilter<FilterSubType>::getModes() const
{
	return internalFilter.getModes();
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::render(FilterHelpers::RenderData& r)
{
	update(r);

	if (numChannels != r.b.getNumChannels())
	{
		setNumChannels(r.b.getNumChannels());
	}

	processed = true;

	internalFilter.processSamples(r.b, r.startSample, r.numSamples);
}

template <class FilterSubType>
double MultiChannelFilter<FilterSubType>::limit(double value, double minValue, double maxValue)
{
	// Branchless SSE clamp.
	// return minss( maxss(val,minval), maxval );

#if HISE_IOS
	return jlimit<double>(minValue, maxValue, value);
#else
	_mm_store_sd(&value, _mm_min_sd(_mm_max_sd(_mm_set_sd(value), _mm_set_sd(minValue)), _mm_set_sd(maxValue)));
	return value;
#endif
}

template <class FilterSubType>
bool MultiChannelFilter<FilterSubType>::compareAndSet(double& value, double newValue) noexcept
{
	bool unEqual = value != newValue;
	value = newValue;
	return unEqual;
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::clearCoefficients()
{
	dirty = true;
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::updateEvery64Frame()
{
	auto thisFreq = FilterLimits::limitFrequency(frequency.getNextValue());
	auto thisGain = gain.getNextValue();
	auto thisQ = FilterLimits::limitQ(q.getNextValue());

	dirty |= compareAndSet(currentFreq, thisFreq);
	dirty |= compareAndSet(currentGain, thisGain);
	dirty |= compareAndSet(currentQ, thisQ);

	if (dirty)
	{
		internalFilter.updateCoefficients(sampleRate, thisFreq, thisQ, thisGain);
		dirty = false;
	}
}

template <class FilterSubType>
void MultiChannelFilter<FilterSubType>::update(FilterHelpers::RenderData& renderData)
{


	const auto f = renderData.applyModValue(frequency.getNextValue());

	auto thisFreq = FilterLimits::limitFrequency(f);
	auto thisGain = renderData.gainModValue * gain.getNextValue();
	auto thisQ = FilterLimits::limitQ(q.getNextValue() * renderData.qModValue);

	dirty |= compareAndSet(currentFreq, thisFreq);
	dirty |= compareAndSet(currentGain, thisGain);
	dirty |= compareAndSet(currentQ, thisQ);

	if (dirty)
	{
		internalFilter.updateCoefficients(sampleRate, thisFreq, thisQ, thisGain);
		dirty = false;
	}
}

template <class FilterSubType>
MultiChannelFilter<FilterSubType>::MultiChannelFilter() :
	frequency(1000.0),
	q(1.0),
	gain(1.0),
	currentFreq(1000.0),
	currentQ(1.0),
	currentGain(1.0)
{

}

MoogFilterSubType::MoogFilterSubType()
{
	in1 = data;
	in2 = in1 + NUM_MAX_CHANNELS;
	in3 = in2 + NUM_MAX_CHANNELS;
	in4 = in3 + NUM_MAX_CHANNELS;
	out1 = in4 + NUM_MAX_CHANNELS;
	out2 = out1 + NUM_MAX_CHANNELS;
	out3 = out2 + NUM_MAX_CHANNELS;
	out4 = out3 + NUM_MAX_CHANNELS;

	reset(NUM_MAX_CHANNELS);

	updateCoefficients(44100.0, 20000.0, 1.0, 0.0);
}


hise::FilterHelpers::FilterSubType MoogFilterSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::MoogFilterSubType;
}

juce::Identifier MoogFilterSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("moog");
}

MoogFilterSubType::~MoogFilterSubType()
{

}

juce::StringArray MoogFilterSubType::getModes() const
{
	return { "One Pole", "Two Poles", "Four Poles" };
}

Array<hise::FilterHelpers::CoefficientType> MoogFilterSubType::getCoefficientTypeList() const
{
	return { FilterHelpers::LowPass, FilterHelpers::LowPassReso, FilterHelpers::Ladder24db };
}

void MoogFilterSubType::reset(int numChannels)
{
	memset(data, 0, sizeof(double)*numChannels * 8);
}

void MoogFilterSubType::setMode(int newMode)
{
	mode = (Mode)newMode;
}

void MoogFilterSubType::setType(int /*t*/)
{

}

void MoogFilterSubType::updateCoefficients(double sampleRate, double frequency, double q, double /*gain*/)
{
	auto lFrequency = FilterLimits::limitFrequency(frequency);

	fc = lFrequency / (0.5 *sampleRate);
	res = q / 2.0;
	if (res > 4.0) res = 4.0;
	f = fc * 1.16;
	fss = (f * f) * (f * f);
	invF = 1.0 - f;
	fb = res * (1.0 - 0.15 * f * f);
}

void MoogFilterSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
	for (int c = 0; c < buffer.getNumChannels(); c++)
	{
		float* d = buffer.getWritePointer(c, startSample);

		for (int i = 0; i < numSamples; i++)
		{
			double input = (double)d[i];

			input -= out4[c] * fb;
			input *= 0.35013 * fss;
			out1[c] = input + 0.3 * in1[c] + invF * out1[c];
			in1[c] = input;
			out2[c] = out1[c] + 0.3 * in2[c] + invF * out2[c];
			in2[c] = out1[c];
			out3[c] = out2[c] + 0.3 * in3[c] + invF * out3[c];
			in3[c] = out2[c];
			out4[c] = out3[c] + 0.3 * in4[c] + invF * out4[c];
			in4[c] = out3[c];
			d[i] = 2.0f * (float)out4[c];
		}
	}
}

void MoogFilterSubType::processFrame(float* frameData, int numChannels)
{
	for (int c = 0; c < numChannels; c++)
	{
		double input = (double)frameData[c];

		input -= out4[c] * fb;
		input *= 0.35013 * fss;
		out1[c] = input + 0.3 * in1[c] + invF * out1[c];
		in1[c] = input;
		out2[c] = out1[c] + 0.3 * in2[c] + invF * out2[c];
		in2[c] = out1[c];
		out3[c] = out2[c] + 0.3 * in3[c] + invF * out3[c];
		in3[c] = out2[c];
		out4[c] = out3[c] + 0.3 * in4[c] + invF * out4[c];
		in4[c] = out3[c];
		frameData[c] = 2.0f * (float)out4[c];
	}
}

DEFINE_MULTI_CHANNEL_FILTER(MoogFilterSubType);

// ============================================================================
// MoogLadderSubType
// Zavalishin bilinear TPT 4-pole Moog ladder filter with nonlinear saturation.
// ============================================================================

MoogLadderSubType::MoogLadderSubType()
{
	reset(NUM_MAX_CHANNELS);
	// Initialise coefficients so the first buffer interpolates from a valid state
	updateCoefficients(44100.0, 20000.0, 0.3, 0.0);
	G_prev  = G;
	fb_prev = fb;
}

hise::FilterHelpers::FilterSubType MoogLadderSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::MoogLadderSubType;
}

juce::Identifier MoogLadderSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("moog_ladder");
}

juce::StringArray MoogLadderSubType::getModes() const
{
	return { "LP One Pole", "LP Two Poles", "LP Four Poles",
		     "HP One Pole", "HP Two Poles", "HP Four Poles" };
}

Array<hise::FilterHelpers::CoefficientType> MoogLadderSubType::getCoefficientTypeList() const
{
	return { FilterHelpers::LowPass, FilterHelpers::LowPassReso, FilterHelpers::Ladder24db,
		     FilterHelpers::HighPass, FilterHelpers::HighPass,   FilterHelpers::HighPass };
}

void MoogLadderSubType::setType(int t)
{
	mode = (Mode)jlimit(0, (int)numModes - 1, t);
}

void MoogLadderSubType::reset(int numChannels)
{
	memset(s,      0, sizeof(s));
	memset(y4prev, 0, sizeof(double) * numChannels);
}

void MoogLadderSubType::updateCoefficients(double sampleRate, double frequency, double q, double /*gain*/)
{
	storedSampleRate = sampleRate;

	// Moog-specific minimum frequency floor, independent of global FilterLimitValues
	auto lFrequency = jmax(20.0, FilterLimits::limitFrequency(frequency));

	// Bilinear pre-warped cutoff coefficient: g = tan(pi * fc / fs)
	// Stage coefficient: G = g / (1 + g)  →  DC gain per stage = 1.0 (unity, inherent in bilinear)
	const double g = std::tan(double_Pi * lFrequency / sampleRate);
	G = g / (1.0 + g);

	// Normalisation: unity DC gain is inherent in the bilinear formulation —
	// H_lp(z=1) = G*2 / (2G) = 1.0 for every stage, so norm = 1.0 for all modes.
	norm = 1.0;

	// Resonance: maps Q [0.3, 9.999] → feedback fb [0.0, 3.5]
	//   Q = 0.3   → fb = 0.0  (no resonance)
	//   Q ≈ 5.15  → fb ≈ 1.75 (moderate resonance, midpoint)
	//   Q = 9.999 → fb = 3.5  (just below self-oscillation)
	fb = jmin(3.8, jmax(0.0, (q - 0.3) * (3.8 / 9.699)));
}

FilterCoefficientData MoogLadderSubType::getCoefficients(double freqNorm, double q, double /*gain*/) const
{
	// getApproximateCoefficients() discards any FilterCoefficientData that lacks a
	// customFunction — the switch(CoefficientType) fallback overrides it. The only
	// way to return our own coefficients is through the custom-function early exit.
	auto* m = const_cast<MoogLadderSubType*>(this);
	if (storedSampleRate > 0.0)
		m->storedFrequency = freqNorm * storedSampleRate;
	m->storedQ = (float)q;

	FilterCoefficientData cd;
	cd.customFunction = getPlotValue;
	cd.obj = m;
	return cd;
}

double MoogLadderSubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
	auto* typed = static_cast<MoogLadderSubType*>(obj);
	const double sr = typed->storedSampleRate;

	// Recompute G and fb from stored frequency/Q (same formulas as updateCoefficients),
	// so the graph always matches the currently displayed frequency and Q parameters.
	const double freq  = jmax(20.0, jmin(sr * 0.499, typed->storedFrequency));
	const double g_val = std::tan(double_Pi * freq / sr);
	const double G     = g_val / (1.0 + g_val);
	const double fb    = jmin(3.9, jmax(0.0, ((double)typed->storedQ - 0.3) * (3.8 / 9.699)));

	// Exact TPT one-pole LP transfer function evaluated at this frequency:
	//   H_lp(z) = G*(1 + z^-1) / (1 - (1-2G)*z^-1),  z = e^(j2π·freqNorm)
	const double omega = 2.0 * double_Pi * freqNorm;
	const double a     = 1.0 - 2.0 * G;
	const std::complex<double> e_neg(std::cos(omega), -std::sin(omega));
	const std::complex<double> H  = G * (1.0 + e_neg) / (1.0 - a * e_neg);
	const std::complex<double> H2 = H * H;
	const std::complex<double> H4 = H2 * H2;

	// Global feedback denominator: D = 1 + fb·H^4
	// (linearised analysis — tanh ≈ identity, standard practice for graph display)
	const std::complex<double> D = 1.0 + fb * H4;

	// Numerator per mode:
	//   LP taps: H^N           (H, H^2, H^4)
	//   HP taps: (1-H)^N       (binomial complement from the processing implementation)
	// Output transfer function = N / D for all modes.
	std::complex<double> N;
	switch (typed->mode)
	{
		case LP_OnePole:  N = H;                                  break;
		case LP_TwoPole:  N = H2;                                 break;
		case LP_FourPole: N = H4;                                 break;
		case HP_OnePole:  { auto hm = 1.0 - H; N = hm; }         break;
		case HP_TwoPole:  { auto hm = 1.0 - H; N = hm * hm; }    break;
		case HP_FourPole: { auto hm = 1.0 - H; N = hm*hm*hm*hm; } break;
		default:          N = H4;                                 break;
	}

	const std::complex<double> response = N / D;
	return getMagnitude ? std::abs(response) : std::arg(response);
}

void MoogLadderSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
	const double G_t  = G;
	const double fb_t = fb;

	for (int c = 0; c < buffer.getNumChannels(); c++)
	{
		float* d = buffer.getWritePointer(c, startSample);

		for (int i = 0; i < numSamples; i++)
		{
			// Linear interpolation of coefficients across the buffer
			const double interp = (numSamples > 1) ? (double)i / (double)(numSamples - 1) : 1.0;
			const double G_i  = G_prev  + (G_t  - G_prev)  * interp;
			const double fb_i = fb_prev + (fb_t - fb_prev) * interp;

			const double input = (double)d[i];

			// Feedback with tanh nonlinearity on the feedback signal
			const double u = input - fb_i * fastTanh(y4prev[c]);

			// Cache tanh values — reused for both LP stage coefficients and HP outputs
			const double tu  = fastTanh(u);
			const double v0  = G_i * (tu  - s[0][c]);
			const double y0  = v0 + s[0][c];
			s[0][c] = y0 + v0;

			const double ty0 = fastTanh(y0);
			const double v1  = G_i * (ty0 - s[1][c]);
			const double y1  = v1 + s[1][c];
			s[1][c] = y1 + v1;

			const double ty1 = fastTanh(y1);
			const double v2  = G_i * (ty1 - s[2][c]);
			const double y2  = v2 + s[2][c];
			s[2][c] = y2 + v2;

			const double ty2 = fastTanh(y2);
			const double v3  = G_i * (ty2 - s[3][c]);
			const double y3  = v3 + s[3][c];
			s[3][c] = y3 + v3;

			y4prev[c] = y3;

			double out;
			switch (mode)
			{
				case LP_OnePole:  out = y0;       break;
				case LP_TwoPole:  out = y1;       break;
				case LP_FourPole: out = y3;       break;
				case HP_OnePole:  out = tu  - y0; break;
				case HP_TwoPole:  out = tu - 2.0*y0 + y1;                    break;
				case HP_FourPole: out = tu - 4.0*y0 + 6.0*y1 - 4.0*y2 + y3; break;
				default:          out = y3;       break;
			}

			d[i] = (float)(out * norm);
		}
	}

	// Advance previous-buffer coefficients for next call
	G_prev  = G_t;
	fb_prev = fb_t;
}

void MoogLadderSubType::processFrame(float* frameData, int numChannels)
{
	for (int c = 0; c < numChannels; c++)
	{
		const double input = (double)frameData[c];

		const double u = input - fb * fastTanh(y4prev[c]);

		const double tu  = fastTanh(u);
		const double v0  = G * (tu  - s[0][c]);
		const double y0  = v0 + s[0][c];
		s[0][c] = y0 + v0;

		const double ty0 = fastTanh(y0);
		const double v1  = G * (ty0 - s[1][c]);
		const double y1  = v1 + s[1][c];
		s[1][c] = y1 + v1;

		const double ty1 = fastTanh(y1);
		const double v2  = G * (ty1 - s[2][c]);
		const double y2  = v2 + s[2][c];
		s[2][c] = y2 + v2;

		const double ty2 = fastTanh(y2);
		const double v3  = G * (ty2 - s[3][c]);
		const double y3  = v3 + s[3][c];
		s[3][c] = y3 + v3;

		y4prev[c] = y3;

		double out;
		switch (mode)
		{
			case LP_OnePole:  out = y0;       break;
			case LP_TwoPole:  out = y1;       break;
			case LP_FourPole: out = y3;       break;
			case HP_OnePole:  out = tu  - y0; break;
			case HP_TwoPole:  out = ty0 - y1; break;
			case HP_FourPole: out = ty2 - y3; break;
			default:          out = y3;       break;
		}

		frameData[c] = (float)(out * norm);
	}
}

DEFINE_MULTI_CHANNEL_FILTER(MoogLadderSubType);

// ============================================================================
// Korg35SubType
// Korg 35 LP / HP filter — Simper TPT resolved topology.
//   LP: 2-pole lowpass with resonance
//   HP: 1-pole highpass with resonance
// Coefficients derived from:
//   g = tan(pi*fc/fs),  G = g/(1+g),  K in [0,2]
//   alpha0 = 1/(1 - K*G*(1-G))
//   LP: B3 = K*(1-G)^2,  B2 = -(1-G)
//   HP: B3 = (1-G),      B2 = -G*(1-G)
// ============================================================================

Korg35SubType::Korg35SubType()
{
	reset(NUM_MAX_CHANNELS);
	// Initialise so first buffer interpolates from a valid state
	G_prev = G; K_prev = K; alpha0_prev = alpha0; B3_prev = B3; B2_prev = B2;
}

hise::FilterHelpers::FilterSubType Korg35SubType::getFilterType()
{
	return FilterHelpers::FilterSubType::Korg35SubType;
}

juce::Identifier Korg35SubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("korg35");
}

juce::StringArray Korg35SubType::getModes() const
{
	return { "LP", "HP" };
}

Array<hise::FilterHelpers::CoefficientType> Korg35SubType::getCoefficientTypeList() const
{
	return { FilterHelpers::LowPass, FilterHelpers::HighPass };
}

void Korg35SubType::setType(int t)
{
	mode = (Mode)jlimit(0, (int)numModes - 1, t);
}

void Korg35SubType::reset(int /*numChannels*/)
{
	memset(s1, 0, sizeof(s1));
	memset(s2, 0, sizeof(s2));
	memset(s3, 0, sizeof(s3));
}

void Korg35SubType::updateCoefficients(double sampleRate, double frequency, double q, double /*gain*/)
{
	storedSampleRate = sampleRate;

	const double fc = jmax(20.0, FilterLimits::limitFrequency(frequency));
	const double g  = std::tan(double_Pi * fc / sampleRate);
	G = g / (1.0 + g);

	// K spans [0, 2] as Q spans [1/sqrt(2), 10]
	// At Q=10: K = 2*(10-1/sqrt(2))/(10-1/sqrt(2)) = 2.0 exactly
	const double sqHalf = 1.0 / std::sqrt(2.0);   // ~0.70711
	K = jmin(2.0, jmax(0.0, 2.0 * (q - sqHalf) / (10.0 - sqHalf)));

	const double oneMinusG = 1.0 - G;
	alpha0 = 1.0 / (1.0 - K * G * oneMinusG);

	if (mode == LP)
	{
		B3 = K * oneMinusG * oneMinusG;   // K*(1-G)^2
		B2 = -oneMinusG;                  // -(1-G)
	}
	else // HP
	{
		B3 =  oneMinusG;                  // (1-G)
		B2 = -G * oneMinusG;              // -G*(1-G)
	}
}

FilterCoefficientData Korg35SubType::getCoefficients(double freqNorm, double q, double /*gain*/) const
{
	// Use the custom-function path so getApproximateCoefficients() forwards our
	// own plot evaluation instead of falling through to the generic biquad switch.
	auto* m = const_cast<Korg35SubType*>(this);
	if (storedSampleRate > 0.0)
		m->storedFrequency = freqNorm * storedSampleRate;
	m->storedQ = (float)q;

	FilterCoefficientData cd;
	cd.customFunction = getPlotValue;
	cd.obj = m;
	return cd;
}

double Korg35SubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
	auto* typed = static_cast<Korg35SubType*>(obj);
	const double sr   = typed->storedSampleRate;
	const double freq = jmax(20.0, jmin(sr * 0.499, typed->storedFrequency));
	const double g_v  = std::tan(double_Pi * freq / sr);
	const double Gv   = g_v / (1.0 + g_v);
	const double a    = 1.0 - 2.0 * Gv;

	const double sqHalf = 1.0 / std::sqrt(2.0);
	const double Kv   = jmin(2.0, jmax(0.0, 2.0 * ((double)typed->storedQ - sqHalf) / (10.0 - sqHalf)));

	const double oneMinusG = 1.0 - Gv;
	const double alpha0v   = 1.0 / (1.0 - Kv * Gv * oneMinusG);

	double B3v, B2v;
	if (typed->mode == LP)
	{
		B3v = Kv * oneMinusG * oneMinusG;
		B2v = -oneMinusG;
	}
	else
	{
		B3v =  oneMinusG;
		B2v = -Gv * oneMinusG;
	}

	// H(z) = G*(1+z^-1)/(1-a*z^-1)   — single TPT one-pole LP transfer function
	// P(z) = 2G*z^-1/(1-a*z^-1)       — state (memory register) transfer function
	const double omega = 2.0 * double_Pi * freqNorm;
	const std::complex<double> e_neg(std::cos(omega), -std::sin(omega));
	const std::complex<double> denom_pole = 1.0 - a * e_neg;
	const std::complex<double> H = Gv * (1.0 + e_neg) / denom_pole;
	const std::complex<double> P = 2.0 * Gv * e_neg / denom_pole;

	std::complex<double> response;
	if (typed->mode == LP)
	{
		// H_LP = alpha0*H^2 / (1 - alpha0*P*(B3_LP + B2_LP*K*H))
		// (Note: B2 term includes an extra H because stage-2 input is K*lp3=K*H*sig)
		const std::complex<double> num = alpha0v * H * H;
		const std::complex<double> den = 1.0 - alpha0v * P * (B3v + B2v * Kv * H);
		response = num / den;
	}
	else
	{
		// H_HP = alpha0*(1-H) / (1 - alpha0*P*K*(B3_HP*(1-H) + B2_HP))
		const std::complex<double> oneMinusH = 1.0 - H;
		const std::complex<double> num = alpha0v * oneMinusH;
		const std::complex<double> den = 1.0 - alpha0v * P * Kv * (B3v * oneMinusH + B2v);
		response = num / den;
	}

	return getMagnitude ? std::abs(response) : std::arg(response);
}

void Korg35SubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
	// Snapshot current target coefficients
	const double G_t      = G;
	const double K_t      = K;
	const double a0_t     = alpha0;
	const double B3_t     = B3;
	const double B2_t     = B2;

	const int numChannels = buffer.getNumChannels();
	const double inv = (numSamples > 1) ? 1.0 / (double)(numSamples - 1) : 1.0;

	for (int n = 0; n < numSamples; ++n)
	{
		// Linearly interpolate coefficients across the buffer
		const double t       = n * inv;
		const double G_i     = G_prev     + t * (G_t   - G_prev);
		const double K_i     = K_prev     + t * (K_t   - K_prev);
		const double a0_i    = alpha0_prev + t * (a0_t  - alpha0_prev);
		const double B3_i    = B3_prev    + t * (B3_t  - B3_prev);
		const double B2_i    = B2_prev    + t * (B2_t  - B2_prev);

		for (int c = 0; c < numChannels; ++c)
		{
			const double x = buffer.getSample(c, startSample + n);

			if (mode == LP)
			{
				// Stage 1: LP, input = x
				const double v1  = G_i * (x       - s1[c]);
				const double lp1 = v1  + s1[c];
				s1[c] += 2.0 * v1;

				// Mixing point
				const double sig = a0_i * (lp1 + B3_i * s3[c] + B2_i * s2[c]);

				// Stage 3: LP, input = sig
				const double v3  = G_i * (sig  - s3[c]);
				const double lp3 = v3  + s3[c];
				s3[c] += 2.0 * v3;

				// Stage 2: LP, input = K*lp3
				const double v2 = G_i * (K_i * lp3 - s2[c]);
				s2[c] += 2.0 * v2;

				buffer.setSample(c, startSample + n, (float)lp3);
			}
			else // HP
			{
				// Stage 1: LP then HP complement
				const double v1  = G_i * (x       - s1[c]);
				const double lp1 = v1  + s1[c];
				s1[c] += 2.0 * v1;
				const double hp1 = x - lp1;

				// Mixing point (output)
				const double sig = a0_i * (hp1 + B3_i * s3[c] + B2_i * s2[c]);

				// Stage 2: LP, input = K*sig
				const double v2  = G_i * (K_i * sig - s2[c]);
				const double lp2 = v2  + s2[c];
				s2[c] += 2.0 * v2;
				const double hp2 = K_i * sig - lp2;

				// Stage 3: LP, input = hp2
				const double v3 = G_i * (hp2 - s3[c]);
				s3[c] += 2.0 * v3;

				buffer.setSample(c, startSample + n, (float)sig);
			}
		}
	}

	// Store end-of-buffer coefficients as the start of the next buffer
	G_prev      = G_t;
	K_prev      = K_t;
	alpha0_prev = a0_t;
	B3_prev     = B3_t;
	B2_prev     = B2_t;
}

void Korg35SubType::processFrame(float* frameData, int numChannels)
{
	for (int c = 0; c < numChannels; ++c)
	{
		const double x = frameData[c];

		if (mode == LP)
		{
			const double v1  = G * (x   - s1[c]);
			const double lp1 = v1 + s1[c];
			s1[c] += 2.0 * v1;

			const double sig = alpha0 * (lp1 + B3 * s3[c] + B2 * s2[c]);

			const double v3  = G * (sig  - s3[c]);
			const double lp3 = v3 + s3[c];
			s3[c] += 2.0 * v3;

			const double v2 = G * (K * lp3 - s2[c]);
			s2[c] += 2.0 * v2;

			frameData[c] = (float)lp3;
		}
		else // HP
		{
			const double v1  = G * (x   - s1[c]);
			const double lp1 = v1 + s1[c];
			s1[c] += 2.0 * v1;
			const double hp1 = x - lp1;

			const double sig = alpha0 * (hp1 + B3 * s3[c] + B2 * s2[c]);

			const double v2  = G * (K * sig - s2[c]);
			const double lp2 = v2 + s2[c];
			s2[c] += 2.0 * v2;
			const double hp2 = K * sig - lp2;

			const double v3 = G * (hp2 - s3[c]);
			s3[c] += 2.0 * v3;

			frameData[c] = (float)sig;
		}
	}
}

DEFINE_MULTI_CHANNEL_FILTER(Korg35SubType);

// ============================================================================
// OberheimSubType
// Oberheim two-integrator SVF with cubic nonlinear saturation on integrator 1.
//
// Signal flow per sample (derived from Faust letrec):
//   hpf  = (x - s2 - FBs1*s1) * alpha0
//   v1   = g*hpf + s1                       // pre-saturation integrator 1 sum
//   bpf  = cubicnl(v1) = v1 - v1^3/3        // BPF — only cubicnl point
//   lpf  = g*bpf + s2                        // LPF
//   bsf  = lpf + hpf                         // BSF = LP + HP
//   s1  := g*hpf + bpf                       // = 2*g*hpf + s1 in linear case
//   s2  := s2 + 2*g*bpf
//
// Morph mode: Gain parameter sweeps LP → BP → HP continuously.
//   gain -18dB = pure LP, 0dB = pure BP, +18dB = pure HP
// ============================================================================

OberheimSubType::OberheimSubType()
{
    reset(NUM_MAX_CHANNELS);
    g_prev = g; alpha0_prev = alpha0; FBs1_prev = FBs1;
}

hise::FilterHelpers::FilterSubType OberheimSubType::getFilterType()
{
    return FilterHelpers::FilterSubType::OberheimSubType;
}

juce::Identifier OberheimSubType::getStaticId()
{
    RETURN_STATIC_IDENTIFIER("oberheim");
}

juce::StringArray OberheimSubType::getModes() const
{
    return { "LP", "HP", "BP", "BSF", "Morph" };
}

Array<hise::FilterHelpers::CoefficientType> OberheimSubType::getCoefficientTypeList() const
{
    return { FilterHelpers::LowPass, FilterHelpers::HighPass,
             FilterHelpers::BandPass, FilterHelpers::LowPass,
             FilterHelpers::BandPass };
}

void OberheimSubType::setType(int t)
{
    mode = (Mode)jlimit(0, (int)numModes - 1, t);
}

void OberheimSubType::reset(int /*numChannels*/)
{
    memset(s1, 0, sizeof(s1));
    memset(s2, 0, sizeof(s2));
}

void OberheimSubType::updateCoefficients(double sampleRate, double frequency, double q, double gain)
{
    storedSampleRate = sampleRate;
    storedGain       = gain;

    const double fc = jmax(20.0, FilterLimits::limitFrequency(frequency));
    g = std::tan(double_Pi * fc / sampleRate);

    const double R = 1.0 / (2.0 * jmax(0.01, q * 0.5));
    FBs1   = 2.0 * R + g;
    alpha0 = 1.0 / (1.0 + 2.0 * R * g + g * g);

    // Morph: gain -18dB → morph=0.0 (pure LP), 0dB → 0.5 (pure BP), +18dB → 1.0 (pure HP)
    const double gain_dB = Decibels::gainToDecibels(jmax((float)gain, 1.0e-6f));
    // Four-stage sweep: -18dB=LP, -6dB=BP, +6dB=HP, +18dB=BSF
    morph = jlimit(0.0, 1.0, (gain_dB + 18.0) / 36.0);
}

FilterCoefficientData OberheimSubType::getCoefficients(double freqNorm, double q, double gain) const
{
    auto* m = const_cast<OberheimSubType*>(this);
    if (storedSampleRate > 0.0)
        m->storedFrequency = freqNorm * storedSampleRate;
    m->storedQ    = (float)q;
    m->storedGain = gain;

    // Recompute morph from gain so plot updates live
    const double gain_dB = Decibels::gainToDecibels(jmax((float)gain, 1.0e-6f));
    m->morph = jlimit(0.0, 1.0, (gain_dB + 18.0) / 36.0);

    FilterCoefficientData cd;
    cd.customFunction = getPlotValue;
    cd.obj = m;
    return cd;
}

double OberheimSubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
    auto* typed = static_cast<OberheimSubType*>(obj);
    const double sr   = typed->storedSampleRate;
    const double freq = jmax(20.0, jmin(sr * 0.499, typed->storedFrequency));
    const double gv   = std::tan(double_Pi * freq / sr);
    const double Rv   = 1.0 / (2.0 * jmax(0.01, (double)typed->storedQ));
    const double FBv  = 2.0 * Rv + gv;
    const double a0v  = 1.0 / (1.0 + 2.0 * Rv * gv + gv * gv);

    const double omega = 2.0 * double_Pi * freqNorm;
    const std::complex<double> e_neg(std::cos(omega), -std::sin(omega));

    const std::complex<double> one_m = 1.0 - e_neg;
    const std::complex<double> one_p = 1.0 + e_neg;

    const std::complex<double> D =
        one_m * one_m
        + 2.0 * gv * a0v * e_neg * (FBv * one_m + gv * one_p);

    const std::complex<double> N_LP  = gv * gv * a0v * one_p * one_p;
    const std::complex<double> N_HP  = a0v * one_m * one_m;
    const std::complex<double> N_BP  = gv * a0v * (one_m * one_p);
    const std::complex<double> N_BSF = a0v * (one_m * one_m + gv * gv * one_p * one_p);

    std::complex<double> N;
    switch (typed->mode)
    {
        case LP:  N = N_LP;  break;
        case HP:  N = N_HP;  break;
        case BP:  N = N_BP;  break;
        case BSF: N = N_BSF; break;
        case Morph:
        {
            const double mt = typed->morph * 3.0;
            if (mt <= 1.0)
                N = N_LP * (1.0 - mt) + N_BP * mt;
            else if (mt <= 2.0)
                N = N_BP * (2.0 - mt) + N_HP * (mt - 1.0);
            else
                N = N_HP * (3.0 - mt) + N_BSF * (mt - 2.0);
            break;
        }
        default: N = N_LP; break;
    }

    const std::complex<double> response = N / D;
    return getMagnitude ? std::abs(response) : std::arg(response);
}

void OberheimSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
    const double g_t    = g;
    const double a0_t   = alpha0;
    const double FBs1_t = FBs1;

    const int numChannels = buffer.getNumChannels();
    const double inv = (numSamples > 1) ? 1.0 / (double)(numSamples - 1) : 1.0;

    for (int n = 0; n < numSamples; ++n)
    {
        const double t    = n * inv;
        const double g_i  = g_prev      + t * (g_t    - g_prev);
        const double a0_i = alpha0_prev  + t * (a0_t   - alpha0_prev);
        const double fb_i = FBs1_prev   + t * (FBs1_t - FBs1_prev);

        for (int c = 0; c < numChannels; ++c)
        {
            const double x   = buffer.getSample(c, startSample + n);
            const double hpf = (x - s2[c] - fb_i * s1[c]) * a0_i;
            const double v1  = g_i * hpf + s1[c];
            const double bpf = v1 - v1 * v1 * v1 / 3.0;
            const double lpf = g_i * bpf + s2[c];

            s1[c] = g_i * hpf + bpf;
            s2[c] = s2[c] + 2.0 * g_i * bpf;

            double out;
            if (mode == Morph)
            {
                const double mt = morph * 3.0;  // 0-3 across four stages
                if (mt <= 1.0)
                    out = lpf * (1.0 - mt) + bpf * mt;
                else if (mt <= 2.0)
                    out = bpf * (2.0 - mt) + hpf * (mt - 1.0);
                else
                    out = hpf * (3.0 - mt) + (lpf + hpf) * (mt - 2.0);
            }
            else
            {
                switch (mode)
                {
                    case LP:  out = lpf;       break;
                    case HP:  out = hpf;       break;
                    case BP:  out = bpf;       break;
                    case BSF: out = lpf + hpf; break;
                    default:  out = lpf;       break;
                }
            }

            buffer.setSample(c, startSample + n, (float)out);
        }
    }

    g_prev      = g_t;
    alpha0_prev = a0_t;
    FBs1_prev   = FBs1_t;
}

void OberheimSubType::processFrame(float* frameData, int numChannels)
{
    for (int c = 0; c < numChannels; ++c)
    {
        const double x   = frameData[c];
        const double hpf = (x - s2[c] - FBs1 * s1[c]) * alpha0;
        const double v1  = g * hpf + s1[c];
        const double bpf = v1 - v1 * v1 * v1 / 3.0;
        const double lpf = g * bpf + s2[c];

        s1[c] = g * hpf + bpf;
        s2[c] = s2[c] + 2.0 * g * bpf;

        double out;
        if (mode == Morph)
        {
            const double mt = morph * 3.0;  // 0-3 across four stages
            if (mt <= 1.0)
                out = lpf * (1.0 - mt) + bpf * mt;
            else if (mt <= 2.0)
                out = bpf * (2.0 - mt) + hpf * (mt - 1.0);
            else
                out = hpf * (3.0 - mt) + (lpf + hpf) * (mt - 2.0);
        }
        else
        {
            switch (mode)
            {
                case LP:  out = lpf;       break;
                case HP:  out = hpf;       break;
                case BP:  out = bpf;       break;
                case BSF: out = lpf + hpf; break;
                default:  out = lpf;       break;
            }
        }

        frameData[c] = (float)out;
    }
}

DEFINE_MULTI_CHANNEL_FILTER(OberheimSubType);

// ============================================================================
// WaspFilterSubType implementation
// ============================================================================

WaspFilterSubType::WaspFilterSubType()
{
    reset(NUM_MAX_CHANNELS);
    g_prev = g; alpha0_prev = alpha0; FBs1_prev = FBs1;
}

hise::FilterHelpers::FilterSubType WaspFilterSubType::getFilterType()
{
    return FilterHelpers::FilterSubType::WaspFilterSubType;
}

juce::Identifier WaspFilterSubType::getStaticId()
{
    RETURN_STATIC_IDENTIFIER("wasp");
}

juce::StringArray WaspFilterSubType::getModes() const
{
    return { "LP", "HP", "BP", "BSF", "Morph" };
}

Array<hise::FilterHelpers::CoefficientType> WaspFilterSubType::getCoefficientTypeList() const
{
    return { FilterHelpers::LowPass, FilterHelpers::HighPass,
             FilterHelpers::BandPass, FilterHelpers::LowPass,
             FilterHelpers::BandPass };
}

void WaspFilterSubType::setType(int t)
{
    mode = (Mode)jlimit(0, (int)numModes - 1, t);
}

void WaspFilterSubType::reset(int /*numChannels*/)
{
    memset(s1, 0, sizeof(s1));
    memset(s2, 0, sizeof(s2));
}

void WaspFilterSubType::updateCoefficients(double sampleRate, double frequency, double q, double gain)
{
    storedSampleRate = sampleRate;
    storedGain       = gain;

    const double fc = jmax(20.0, FilterLimits::limitFrequency(frequency));
    g = std::tan(double_Pi * fc / sampleRate);

    // Wasp has a tighter Q range than Oberheim — maps to slightly higher damping
    const double R = 1.0 / (2.0 * jmax(0.01, q));
    FBs1   = 2.0 * R + g;
    alpha0 = 1.0 / (1.0 + 2.0 * R * g + g * g);

    const double gain_dB = Decibels::gainToDecibels(jmax((float)gain, 1.0e-6f));
    morph = jlimit(0.0, 1.0, (gain_dB + 18.0) / 36.0);
}

FilterCoefficientData WaspFilterSubType::getCoefficients(double freqNorm, double q, double gain) const
{
    auto* m = const_cast<WaspFilterSubType*>(this);
    if (storedSampleRate > 0.0)
        m->storedFrequency = freqNorm * storedSampleRate;
    m->storedQ    = (float)q;
    m->storedGain = gain;

    const double gain_dB = Decibels::gainToDecibels(jmax((float)gain, 1.0e-6f));
    m->morph = jlimit(0.0, 1.0, (gain_dB + 18.0) / 36.0);

    FilterCoefficientData cd;
    cd.customFunction = getPlotValue;
    cd.obj = m;
    return cd;
}

double WaspFilterSubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
    auto* typed = static_cast<WaspFilterSubType*>(obj);
    const double sr   = typed->storedSampleRate;
    const double freq = jmax(20.0, jmin(sr * 0.499, typed->storedFrequency));
    const double gv   = std::tan(double_Pi * freq / sr);
    const double Rv   = 1.0 / (2.0 * jmax(0.01, (double)typed->storedQ));
    const double FBv  = 2.0 * Rv + gv;
    const double a0v  = 1.0 / (1.0 + 2.0 * Rv * gv + gv * gv);

    const double omega = 2.0 * double_Pi * freqNorm;
    const std::complex<double> e_neg(std::cos(omega), -std::sin(omega));

    const std::complex<double> one_m = 1.0 - e_neg;
    const std::complex<double> one_p = 1.0 + e_neg;

    const std::complex<double> D =
        one_m * one_m
        + 2.0 * gv * a0v * e_neg * (FBv * one_m + gv * one_p);

    const std::complex<double> N_LP  = gv * gv * a0v * one_p * one_p;
    const std::complex<double> N_HP  = a0v * one_m * one_m;
    const std::complex<double> N_BP  = gv * a0v * (one_m * one_p);
    const std::complex<double> N_BSF = a0v * (one_m * one_m + gv * gv * one_p * one_p);

    std::complex<double> N;
    switch (typed->mode)
    {
        case LP:  N = N_LP;  break;
        case HP:  N = N_HP;  break;
        case BP:  N = N_BP;  break;
        case BSF: N = N_BSF; break;
        case Morph:
        {
            const double mt = typed->morph * 3.0;
            if (mt <= 1.0)
                N = N_LP * (1.0 - mt) + N_BP * mt;
            else if (mt <= 2.0)
                N = N_BP * (2.0 - mt) + N_HP * (mt - 1.0);
            else
                N = N_HP * (3.0 - mt) + N_BSF * (mt - 2.0);
            break;
        }
        default: N = N_LP; break;
    }

    const std::complex<double> response = N / D;
    return getMagnitude ? std::abs(response) : std::arg(response);
}

void WaspFilterSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
    const double g_t    = g;
    const double a0_t   = alpha0;
    const double FBs1_t = FBs1;

    const int numChannels = buffer.getNumChannels();
    const double inv = (numSamples > 1) ? 1.0 / (double)(numSamples - 1) : 1.0;

    for (int n = 0; n < numSamples; ++n)
    {
        const double t    = n * inv;
        const double g_i  = g_prev      + t * (g_t    - g_prev);
        const double a0_i = alpha0_prev  + t * (a0_t   - alpha0_prev);
        const double fb_i = FBs1_prev   + t * (FBs1_t - FBs1_prev);

        for (int c = 0; c < numChannels; ++c)
        {
            const double x   = buffer.getSample(c, startSample + n);

            // CMOS inverter nonlinearity on both integrators
            const double hpf = (x - s2[c] - fb_i * s1[c]) * a0_i;
            const double v1  = g_i * hpf + s1[c];
            const double bpf = wasp_nl(v1);
            const double lpf = g_i * bpf + s2[c];

            s1[c] = g_i * hpf + bpf;
            s2[c] = s2[c] + 2.0 * g_i * bpf;

            double out;
            if (mode == Morph)
            {
                const double mt = morph * 3.0;
                if (mt <= 1.0)
                    out = lpf * (1.0 - mt) + bpf * mt;
                else if (mt <= 2.0)
                    out = bpf * (2.0 - mt) + hpf * (mt - 1.0);
                else
                    out = hpf * (3.0 - mt) + (lpf + hpf) * (mt - 2.0);
            }
            else
            {
                switch (mode)
                {
                    case LP:  out = lpf;       break;
                    case HP:  out = hpf;       break;
                    case BP:  out = bpf;       break;
                    case BSF: out = lpf + hpf; break;
                    default:  out = lpf;       break;
                }
            }

            buffer.setSample(c, startSample + n, (float)out);
        }
    }

    g_prev      = g_t;
    alpha0_prev = a0_t;
    FBs1_prev   = FBs1_t;
}

void WaspFilterSubType::processFrame(float* frameData, int numChannels)
{
    for (int c = 0; c < numChannels; ++c)
    {
        const double x   = frameData[c];

        const double hpf = (x - s2[c] - FBs1 * s1[c]) * alpha0;
        const double v1  = g * hpf + s1[c];
        const double bpf = wasp_nl(v1);
        const double lpf = g * wasp_nl(bpf) + s2[c];

        s1[c] = g * hpf + bpf;
        s2[c] = s2[c] + 2.0 * g * bpf;

        double out;
        if (mode == Morph)
        {
            const double mt = morph * 3.0;
            if (mt <= 1.0)
                out = lpf * (1.0 - mt) + bpf * mt;
            else if (mt <= 2.0)
                out = bpf * (2.0 - mt) + hpf * (mt - 1.0);
            else
                out = hpf * (3.0 - mt) + (lpf + hpf) * (mt - 2.0);
        }
        else
        {
            switch (mode)
            {
                case LP:  out = lpf;       break;
                case HP:  out = hpf;       break;
                case BP:  out = bpf;       break;
                case BSF: out = lpf + hpf; break;
                default:  out = lpf;       break;
            }
        }

        frameData[c] = (float)out;
    }
}

DEFINE_MULTI_CHANNEL_FILTER(WaspFilterSubType);

// ============================================================================
// TB303FilterSubType
// Roland TB-303 diode ladder filter.
//   Signal flow per sample:
//     1. Fixed 35 Hz one-pole TPT highpass on input (approximates 303 input capacitor)
//     2. Resonance compensation: clamped bass boost using previous HPF output
//     3. Four-pole diode ladder with asymmetric fastTanh drive:
//          even stages (0, 2): fastTanh(x * 1.1)
//          odd  stages (1, 3): fastTanh(x * 0.9)
// ============================================================================

TB303FilterSubType::TB303FilterSubType()
{
    reset(NUM_MAX_CHANNELS);
    // Initialise coefficients so the first buffer interpolates from a valid state
    updateCoefficients(44100.0, 20000.0, 0.3, 0.0);
    G_prev  = G;
    fb_prev = fb;
}

hise::FilterHelpers::FilterSubType TB303FilterSubType::getFilterType()
{
    return FilterHelpers::FilterSubType::TB303FilterSubType;
}

juce::Identifier TB303FilterSubType::getStaticId()
{
    RETURN_STATIC_IDENTIFIER("tb303");
}

juce::StringArray TB303FilterSubType::getModes() const
{
    return { "Low Pass" };
}

Array<hise::FilterHelpers::CoefficientType> TB303FilterSubType::getCoefficientTypeList() const
{
    return { FilterHelpers::LowPassReso };
}

void TB303FilterSubType::setType(int t)
{
    mode = (Mode)jlimit(0, (int)numModes - 1, t);
}

void TB303FilterSubType::reset(int numChannels)
{
    memset(s,        0, sizeof(s));
    memset(y4prev,   0, sizeof(double) * numChannels);
    memset(hp_s,     0, sizeof(double) * numChannels);
    memset(hpf_prev, 0, sizeof(double) * numChannels);
}

void TB303FilterSubType::updateCoefficients(double sampleRate, double frequency, double q, double /*gain*/)
{
    q = jmin(9.0, q);
    storedSampleRate = sampleRate;

    // Fixed 35 Hz input HPF coefficient (approximates the 303 input capacitor)
    const double g_hp = std::tan(double_Pi * 35.0 / sampleRate);
    G_hp = g_hp / (1.0 + g_hp);

    // Bilinear pre-warped ladder cutoff: g = tan(pi*fc/fs), G = g/(1+g)
    const double fc = jmax(20.0, FilterLimits::limitFrequency(frequency));
    const double g  = std::tan(double_Pi * fc / sampleRate);
    G  = g / (1.0 + g);

    // Resonance: maps Q [0.3, 9.999] → feedback fb [0.0, 3.5]
    fb = jmin(3.5, jmax(0.0, (q - 0.3) * (4.5 / 9.699)));

    // More aggressive resonance reduction above 2kHz
    const double freqScale = 1.0 - jmax(0.0, (fc - 2000.0) / 18000.0);
    fb *= freqScale;
}

FilterCoefficientData TB303FilterSubType::getCoefficients(double freqNorm, double q, double /*gain*/) const
{
    auto* m = const_cast<TB303FilterSubType*>(this);
    if (storedSampleRate > 0.0)
        m->storedFrequency = freqNorm * storedSampleRate;
    m->storedQ = (float)q;

    FilterCoefficientData cd;
    cd.customFunction = getPlotValue;
    cd.obj = m;
    return cd;
}

double TB303FilterSubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
    auto* typed = static_cast<TB303FilterSubType*>(obj);
    const double sr   = typed->storedSampleRate;
    const double freq = jmax(20.0, jmin(sr * 0.499, typed->storedFrequency));
    const double g_v  = std::tan(double_Pi * freq / sr);
    const double G_v  = g_v / (1.0 + g_v);
    const double fb_v = jmin(3.9, jmax(0.0, ((double)typed->storedQ - 0.3) * (3.8 / 9.699)));

    // Input HPF coefficient (fixed 35 Hz)
    const double g_hp_v = std::tan(double_Pi * 35.0 / sr);
    const double G_hp_v = g_hp_v / (1.0 + g_hp_v);

    // z-domain evaluation at z = e^{jω}, ω = 2π·freqNorm
    const double omega = 2.0 * double_Pi * freqNorm;
    const std::complex<double> e_neg(std::cos(omega), -std::sin(omega));

    // Single TPT LP stage: H = G*(1+z^-1) / (1-(1-2G)*z^-1)
    const double a = 1.0 - 2.0 * G_v;
    const std::complex<double> H  = G_v * (1.0 + e_neg) / (1.0 - a * e_neg);
    const std::complex<double> H2 = H * H;
    const std::complex<double> H4 = H2 * H2;

    // Global feedback denominator: D = 1 + fb*H4
    const std::complex<double> D = 1.0 + fb_v * H4;

    // Input HPF approximation: HPF = 1 - H_hp  (LP complement of fixed-corner stage)
    const double a_hp = 1.0 - 2.0 * G_hp_v;
    const std::complex<double> H_hp = G_hp_v * (1.0 + e_neg) / (1.0 - a_hp * e_neg);
    const std::complex<double> HPF  = 1.0 - H_hp;

    const std::complex<double> response = (H4 / D) * HPF;
    return getMagnitude ? std::abs(response) : std::arg(response);
}

void TB303FilterSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
    const double G_t  = G;
    const double fb_t = fb;

    for (int c = 0; c < buffer.getNumChannels(); c++)
    {
        float* d = buffer.getWritePointer(c, startSample);

        for (int i = 0; i < numSamples; i++)
        {
            // Linear interpolation of coefficients across the buffer
            const double interp = (numSamples > 1) ? (double)i / (double)(numSamples - 1) : 1.0;
            const double G_i  = G_prev  + (G_t  - G_prev)  * interp;
            const double fb_i = fb_prev + (fb_t - fb_prev) * interp;

            const double input = (double)d[i];

            // Input HPF: fixed 35 Hz one-pole TPT highpass
            const double v_hp  = G_hp * (input - hp_s[c]);
            const double lp_hp = v_hp + hp_s[c];
            hp_s[c] = lp_hp + v_hp;
            const double hpf_out = input - lp_hp;

            // Resonance compensation: clamped bass boost at high resonance
            const double comp_coeff  = jmin(0.5, fb_i * 0.3);
            const double compensated = hpf_out + comp_coeff * hpf_prev[c];
            hpf_prev[c] = hpf_out;

            // Four-pole diode ladder with feedback
            const double u = compensated - fb_i * fastTanh(y4prev[c]);

            // Stage 0 — even, drive 1.1
            const double v0 = G_i * (fastTanh(u  * 1.1) - s[0][c]);
            const double y0 = v0 + s[0][c];
            s[0][c] = y0 + v0;

            // Stage 1 — odd, drive 0.9
            const double v1 = G_i * (fastTanh(y0 * 0.9) - s[1][c]);
            const double y1 = v1 + s[1][c];
            s[1][c] = y1 + v1;

            // Stage 2 — even, drive 1.1
            const double v2 = G_i * (fastTanh(y1 * 1.1) - s[2][c]);
            const double y2 = v2 + s[2][c];
            s[2][c] = y2 + v2;

            // Stage 3 — odd, drive 0.9
            const double v3 = G_i * (fastTanh(y2 * 0.9) - s[3][c]);
            const double y3 = v3 + s[3][c];
            s[3][c] = y3 + v3;

            y4prev[c] = y3;

            const double leak = 0.9999;
            s[0][c]     *= leak;
            s[1][c]     *= leak;
            s[2][c]     *= leak;
            s[3][c]     *= leak;
            y4prev[c]   *= leak;
            hp_s[c]     *= leak;
            hpf_prev[c] *= leak;

            d[i] = (float)y3;
        }
    }

    // Advance previous-buffer coefficients for next call
    G_prev  = G_t;
    fb_prev = fb_t;
}

void TB303FilterSubType::processFrame(float* frameData, int numChannels)
{
    for (int c = 0; c < numChannels; c++)
    {
        const double input = (double)frameData[c];

        // Input HPF: fixed 35 Hz one-pole TPT highpass
        const double v_hp  = G_hp * (input - hp_s[c]);
        const double lp_hp = v_hp + hp_s[c];
        hp_s[c] = lp_hp + v_hp;
        const double hpf_out = input - lp_hp;

        // Resonance compensation: clamped bass boost at high resonance
        const double comp_coeff  = jmin(0.5, fb * 0.3);
        const double compensated = hpf_out + comp_coeff * hpf_prev[c];
        hpf_prev[c] = hpf_out;

        // Four-pole diode ladder with feedback
        const double u = compensated - fb * fastTanh(y4prev[c]);

        // Stage 0 — even, drive 1.1
        const double v0 = G * (fastTanh(u  * 1.1) - s[0][c]);
        const double y0 = v0 + s[0][c];
        s[0][c] = y0 + v0;

        // Stage 1 — odd, drive 0.9
        const double v1 = G * (fastTanh(y0 * 0.9) - s[1][c]);
        const double y1 = v1 + s[1][c];
        s[1][c] = y1 + v1;

        // Stage 2 — even, drive 1.1
        const double v2 = G * (fastTanh(y1 * 1.1) - s[2][c]);
        const double y2 = v2 + s[2][c];
        s[2][c] = y2 + v2;

        // Stage 3 — odd, drive 0.9
        const double v3 = G * (fastTanh(y2 * 0.9) - s[3][c]);
        const double y3 = v3 + s[3][c];
        s[3][c] = y3 + v3;

        y4prev[c] = y3;

        const double leak = 0.9999;
        s[0][c]     *= leak;
        s[1][c]     *= leak;
        s[2][c]     *= leak;
        s[3][c]     *= leak;
        y4prev[c]   *= leak;
        hp_s[c]     *= leak;
        hpf_prev[c] *= leak;

        frameData[c] = (float)y3;
    }
}

DEFINE_MULTI_CHANNEL_FILTER(TB303FilterSubType);

// ============================================================================
// DiodeLadderSubType
// Doepfer A-102 / EMS VCS3 style diode ladder filter.
//   Signal flow per sample:
//     u  = input - fb * diodeSat(y4prev)          // feedback
//     Stages 0–3: TPT one-pole LP with diodeSat nonlinearity
//     Output modes via binomial complement of the LP taps:
//       LP: y3
//       HP: tu - 4*y0 + 6*y1 - 4*y2 + y3
//       BP: tu - 2*y0 + y1
// ============================================================================

DiodeLadderSubType::DiodeLadderSubType()
{
    reset(NUM_MAX_CHANNELS);
    // Initialise coefficients so the first buffer interpolates from a valid state
    updateCoefficients(44100.0, 20000.0, 0.3, 0.0);
    G_prev  = G;
    fb_prev = fb;
}

hise::FilterHelpers::FilterSubType DiodeLadderSubType::getFilterType()
{
    return FilterHelpers::FilterSubType::DiodeLadderSubType;
}

juce::Identifier DiodeLadderSubType::getStaticId()
{
    RETURN_STATIC_IDENTIFIER("diode_ladder");
}

juce::StringArray DiodeLadderSubType::getModes() const
{
    return { "Low Pass", "High Pass", "Band Pass" };
}

Array<hise::FilterHelpers::CoefficientType> DiodeLadderSubType::getCoefficientTypeList() const
{
    return { FilterHelpers::LowPassReso, FilterHelpers::HighPass, FilterHelpers::BandPass };
}

void DiodeLadderSubType::setType(int t)
{
    mode = (Mode)jlimit(0, (int)numModes - 1, t);
}

void DiodeLadderSubType::reset(int numChannels)
{
    memset(s,      0, sizeof(s));
    memset(y4prev, 0, sizeof(double) * numChannels);
}

void DiodeLadderSubType::updateCoefficients(double sampleRate, double frequency, double q, double /*gain*/)
{
    q = jmin(9.0, q);
    storedSampleRate = sampleRate;

    const double fc = jmax(20.0, FilterLimits::limitFrequency(frequency));
    const double g  = std::tan(double_Pi * fc / sampleRate);
    G  = g / (1.0 + g);

    // Resonance: maps Q [0.3, 9.999] → feedback fb [0.0, 3.5]
    fb = jmin(3.5, jmax(0.0, (q - 0.3) * (4.5 / 9.699)));

    // More aggressive resonance reduction above 2kHz
    const double freqScale = 1.0 - jmax(0.0, (fc - 2000.0) / 18000.0);
    fb *= freqScale;
}

FilterCoefficientData DiodeLadderSubType::getCoefficients(double freqNorm, double q, double /*gain*/) const
{
    auto* m = const_cast<DiodeLadderSubType*>(this);
    if (storedSampleRate > 0.0)
        m->storedFrequency = freqNorm * storedSampleRate;
    m->storedQ = (float)q;

    FilterCoefficientData cd;
    cd.customFunction = getPlotValue;
    cd.obj = m;
    return cd;
}

double DiodeLadderSubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
    auto* typed = static_cast<DiodeLadderSubType*>(obj);
    const double sr   = typed->storedSampleRate;
    const double freq = jmax(20.0, jmin(sr * 0.499, typed->storedFrequency));
    const double g_v  = std::tan(double_Pi * freq / sr);
    const double G_v  = g_v / (1.0 + g_v);
    const double fb_v = jmin(3.9, jmax(0.0, ((double)typed->storedQ - 0.3) * (3.8 / 9.699)));

    // z-domain evaluation at z = e^{jω}, ω = 2π·freqNorm
    // (linear approximation: diodeSat ≈ identity for small signals)
    const double omega = 2.0 * double_Pi * freqNorm;
    const std::complex<double> e_neg(std::cos(omega), -std::sin(omega));

    // Single TPT LP stage: H = G*(1+z^-1) / (1-(1-2G)*z^-1)
    const double a = 1.0 - 2.0 * G_v;
    const std::complex<double> H  = G_v * (1.0 + e_neg) / (1.0 - a * e_neg);
    const std::complex<double> H2 = H * H;
    const std::complex<double> H4 = H2 * H2;

    // Global feedback denominator: D = 1 + fb*H4
    const std::complex<double> D = 1.0 + fb_v * H4;

    std::complex<double> N;
    switch (typed->mode)
    {
        case LP: N = H4;                                                    break;
        case HP: { auto hm = 1.0 - H; N = hm * hm * hm * hm; }            break;
        case BP: { auto hm = 1.0 - H; N = hm * hm; }                      break;
        default: N = H4;                                                    break;
    }

    const std::complex<double> response = N / D;
    return getMagnitude ? std::abs(response) : std::arg(response);
}

void DiodeLadderSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
    const double G_t  = G;
    const double fb_t = fb;

    for (int c = 0; c < buffer.getNumChannels(); c++)
    {
        float* d = buffer.getWritePointer(c, startSample);

        for (int i = 0; i < numSamples; i++)
        {
            // Linear interpolation of coefficients across the buffer
            const double interp = (numSamples > 1) ? (double)i / (double)(numSamples - 1) : 1.0;
            const double G_i  = G_prev  + (G_t  - G_prev)  * interp;
            const double fb_i = fb_prev + (fb_t - fb_prev) * interp;

            const double input = (double)d[i];

            const double u = input - fb_i * diodeSat(y4prev[c]);

            // Cache diodeSat values — reused for HP/BP output modes
            const double tu  = diodeSat(u);
            const double v0  = G_i * (tu  - s[0][c]);
            const double y0  = v0 + s[0][c];
            s[0][c] = y0 + v0;

            const double ty0 = diodeSat(y0);
            const double v1  = G_i * (ty0 - s[1][c]);
            const double y1  = v1 + s[1][c];
            s[1][c] = y1 + v1;

            const double ty1 = diodeSat(y1);
            const double v2  = G_i * (ty1 - s[2][c]);
            const double y2  = v2 + s[2][c];
            s[2][c] = y2 + v2;

            const double ty2 = diodeSat(y2);
            const double v3  = G_i * (ty2 - s[3][c]);
            const double y3  = v3 + s[3][c];
            s[3][c] = y3 + v3;

            y4prev[c] = y3;

            const double leak = 0.9999;
            s[0][c]   *= leak;
            s[1][c]   *= leak;
            s[2][c]   *= leak;
            s[3][c]   *= leak;
            y4prev[c] *= leak;

            double out;
            switch (mode)
            {
                case LP: out = y3;                                             break;
                case HP: out = tu - 4.0*y0 + 6.0*y1 - 4.0*y2 + y3;          break;
                case BP: out = tu - 2.0*y0 + y1;                             break;
                default: out = y3;                                             break;
            }

            d[i] = (float)out;
        }
    }

    // Advance previous-buffer coefficients for next call
    G_prev  = G_t;
    fb_prev = fb_t;
}

void DiodeLadderSubType::processFrame(float* frameData, int numChannels)
{
    for (int c = 0; c < numChannels; c++)
    {
        const double input = (double)frameData[c];

        const double u = input - fb * diodeSat(y4prev[c]);

        const double tu  = diodeSat(u);
        const double v0  = G * (tu  - s[0][c]);
        const double y0  = v0 + s[0][c];
        s[0][c] = y0 + v0;

        const double ty0 = diodeSat(y0);
        const double v1  = G * (ty0 - s[1][c]);
        const double y1  = v1 + s[1][c];
        s[1][c] = y1 + v1;

        const double ty1 = diodeSat(y1);
        const double v2  = G * (ty1 - s[2][c]);
        const double y2  = v2 + s[2][c];
        s[2][c] = y2 + v2;

        const double ty2 = diodeSat(y2);
        const double v3  = G * (ty2 - s[3][c]);
        const double y3  = v3 + s[3][c];
        s[3][c] = y3 + v3;

        y4prev[c] = y3;

        const double leak = 0.9999;
        s[0][c]   *= leak;
        s[1][c]   *= leak;
        s[2][c]   *= leak;
        s[3][c]   *= leak;
        y4prev[c] *= leak;

        double out;
        switch (mode)
        {
            case LP: out = y3;                                             break;
            case HP: out = tu - 4.0*y0 + 6.0*y1 - 4.0*y2 + y3;          break;
            case BP: out = tu - 2.0*y0 + y1;                             break;
            default: out = y3;                                             break;
        }

        frameData[c] = (float)out;
    }
}

DEFINE_MULTI_CHANNEL_FILTER(DiodeLadderSubType);

// ============================================================================
// CombFilterSubType
// Musical comb filter with all-pass fractional delay interpolation.
//
// Signal flow per sample:
//   1. Read integer-delay sample from circular buffer.
//   2. Apply first-order all-pass interpolation for fractional delay:
//        ap_out = η*(delayed_raw - y[n-1]) + x[n-1]
//        η = (1-d)/(1+d),  d = fractional part of delay
//   3. Feed ap_out through a one-pole lowpass (damping):
//        fb_lp_s += α*(ap_out - fb_lp_s),  α = fb_lp_coeff
//   4. Mix back: output = input + effective_fb * fb_lp_s
//   5. Write output to circular buffer, advance write head.
//
// Gain parameter arrives as linear amplitude from the framework
// (FilterNodeBase::setGain converts dB→linear before passing through).
// Converted back to dB here to give a symmetric ±18 dB → ±0.99 fb_gain.
// ============================================================================

CombFilterSubType::CombFilterSubType()
{
	delayLine.resize(NUM_MAX_CHANNELS * maxDelayLine, 0.0);
	reset(NUM_MAX_CHANNELS);
	updateCoefficients(44100.0, 440.0, 1.0, 0.0);
}

hise::FilterHelpers::FilterSubType CombFilterSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::CombFilterSubType;
}

juce::Identifier CombFilterSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("comb");
}

juce::StringArray CombFilterSubType::getModes() const
{
	return { "Comb", "Inv Comb",
	         "Feedforward", "Inv Feedforward",
	         "Flanger", "Inv Flanger",
	         "Flanger FB", "Inv Flanger FB" };
}

Array<hise::FilterHelpers::CoefficientType> CombFilterSubType::getCoefficientTypeList() const
{
	return { FilterHelpers::LowPass, FilterHelpers::LowPass,
	         FilterHelpers::LowPass, FilterHelpers::LowPass,
	         FilterHelpers::LowPass, FilterHelpers::LowPass,
	         FilterHelpers::LowPass, FilterHelpers::LowPass };
}

void CombFilterSubType::setType(int t)
{
	mode = (Mode)jlimit(0, (int)numModes - 1, t);
}

void CombFilterSubType::reset(int numChannels)
{
	std::fill(delayLine.begin(), delayLine.end(), 0.0);
	memset(writePos,  0, sizeof(int)    * numChannels);
	memset(ap_s,      0, sizeof(double) * numChannels);
	memset(ap_s_prev, 0, sizeof(double) * numChannels);
	memset(fb_lp_s,   0, sizeof(double) * numChannels);
	memset(lp_s,      0, sizeof(float)  * numChannels);
}

void CombFilterSubType::updateCoefficients(double sampleRate, double frequency, double q, double gain)
{
	storedSampleRate = sampleRate;
	storedGain       = gain;
	storedQ          = (float)q;

	// Modes that use the new parameter mapping:
	//   Q   → feedback depth (fb_gain 0–0.95)
	//   Gain → bidirectional LP/HP in the feedback/feedforward path
	// FlangerFB/FlangerFBInv are excluded — they use the legacy Gain→fb_gain mapping.
	const bool useNewMapping = (mode == Comb          || mode == InvComb          ||
	                            mode == FeedforwardComb || mode == FeedforwardInvComb ||
	                            mode == Flanger         || mode == FlangerInv);

	const double gain_dB = Decibels::gainToDecibels(jmax(gain, 1.0e-6));

	if (useNewMapping)
	{
		// Q → feedback depth: q=0.3 → 0%, q=9.9 → 95%
		fb_gain = jlimit(0.0, 0.95, 0.95 * (q - 0.3) / 9.6);

		// Gain → bidirectional feedback filter:
		//   gain_dB < 0: LP, cutoff log-sweeps 200 Hz (−18 dB) → 20 kHz (0 dB)
		//   gain_dB > 0: HP, cutoff log-sweeps  20 Hz ( 0 dB) → 10 kHz (+18 dB)
		//   gain_dB ≈ 0: passthrough (lp_coeff = 0 → one-pole update becomes identity)
		if (gain_dB < -0.1)
		{
			const double t = jlimit(0.0, 1.0, (gain_dB + 18.0) / 18.0);  // 0 at −18 dB, 1 at 0 dB
			const double lp_cutoff = 200.0 * std::pow(100.0, t);           // log-sweep 200 → 20000 Hz
			lp_coeff = (float)std::exp(-2.0 * double_Pi * lp_cutoff / sampleRate);
			fb_is_hp = false;
		}
		else if (gain_dB > 0.1)
		{
			const double t = jlimit(0.0, 1.0, gain_dB / 18.0);             // 0 at 0 dB, 1 at +18 dB
			const double hp_cutoff = 20.0 * std::pow(500.0, t);             // log-sweep 20 → 10000 Hz
			lp_coeff = (float)std::exp(-2.0 * double_Pi * hp_cutoff / sampleRate);
			fb_is_hp = true;
		}
		else
		{
			// Passthrough: lp_coeff = 0 → lp_s[c] = ap_out each sample → identity
			lp_coeff = 0.0f;
			fb_is_hp = false;
		}
	}
	else
	{
		// FlangerFB / FlangerFBInv: legacy Gain → feedback depth mapping
		fb_gain = jlimit(-0.99, 0.99, gain_dB / 18.0);
	}

	const bool isFlanger = (mode == Flanger || mode == FlangerInv ||
	                        mode == FlangerFB || mode == FlangerFBInv);

	if (isFlanger)
	{
		// Map frequency 20–20000 Hz to flanger delay range, inverted:
		//   low  freq → long  delay (~20 ms, slow whoosh)
		//   high freq → short delay (~0.1 ms, high-pitched shimmer)
		const double fc = jlimit(20.0, 20000.0, frequency);
		storedFrequency = fc;
		const double t  = (fc - 20.0) / (20000.0 - 20.0);
		delayLength     = sampleRate * 0.0001 * std::pow(200.0, 1.0 - t);
	}
	else
	{
		// Comb / feedforward: delay = period of the fundamental
		const double fc = jlimit(20.0, 5000.0, frequency);
		storedFrequency = fc;
		delayLength     = sampleRate / fc;
	}

	delayLengthInt = jlimit(1, maxDelayLine - 1, (int)std::floor(delayLength));

	// All-pass fractional interpolation coefficient: η = (1-d)/(1+d)
	const double delayFrac = delayLength - (double)delayLengthInt;
	ap_coeff = (1.0 - delayFrac) / (1.0 + delayFrac);

	// FlangerFB: lowpass in the feedback write path
	//   Low Q  → α near 0   → heavy HF damping in feedback loop
	//   High Q → α near 0.99 → light damping, HF sustained longer
	fb_lp_coeff = jlimit(0.0, 0.99, 1.0 - (1.0 / (q * 2.0)));
}

FilterCoefficientData CombFilterSubType::getCoefficients(double freqNorm, double q, double gain) const
{
	auto* m = const_cast<CombFilterSubType*>(this);
	m->storedQ    = (float)q;
	m->storedGain = gain;

	FilterCoefficientData cd;
	cd.customFunction = getPlotValue;
	cd.obj = m;
	return cd;
}

double CombFilterSubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
	if (!getMagnitude) return 0.0;

	auto* typed = static_cast<CombFilterSubType*>(obj);

	const bool isCombFB      = (typed->mode == Comb || typed->mode == InvComb);
	const bool isFeedforward = (typed->mode == FeedforwardComb    || typed->mode == FeedforwardInvComb ||
	                            typed->mode == Flanger             || typed->mode == FlangerInv);
	const bool isInv         = (typed->mode == InvComb            || typed->mode == FeedforwardInvComb ||
	                            typed->mode == FlangerInv          || typed->mode == FlangerFBInv);

	const double omega = 2.0 * double_Pi * freqNorm;

	// Helper: evaluate the one-pole feedback filter magnitude at the current omega.
	// Used by isCombFB and isFeedforward branches (both use lp_coeff / fb_is_hp).
	// Returns 1.0 for passthrough (lp_coeff < ε).
	const auto filterMag = [&]() -> double
	{
		const double c = (double)typed->lp_coeff;
		if (c < 1.0e-6) return 1.0;
		const std::complex<double> e_neg(std::cos(omega), -std::sin(omega));
		const std::complex<double> H_LP = (1.0 - c) / (1.0 - c * e_neg);
		return typed->fb_is_hp ? std::abs(std::complex<double>(1.0, 0.0) - H_LP)
		                       : std::abs(H_LP);
	};

	if (isCombFB)
	{
		// fb_gain is Q-controlled (always ≥ 0); isInv flips sign for display.
		// Effective feedback at ω: fb_eff = fb_signed × |H_filter(ω)|
		// Comb magnitude:   |H|² = 1 / (1 + fb_eff² − 2·fb_eff·cos(ωD))
		// Normalised so resonance peak = 1.
		const double fb_raw    = jlimit(0.0, 0.90, typed->fb_gain);
		const double fb_signed = isInv ? -fb_raw : fb_raw;
		const double fb_eff    = jlimit(-0.90, 0.90, fb_signed * filterMag());

		const double D       = typed->storedSampleRate / jmax(typed->storedFrequency, 1.0);
		const double mag     = 1.0 / std::sqrt(jmax(0.0001, 1.0 + fb_eff*fb_eff - 2.0*fb_eff*std::cos(omega*D)));
		const double max_mag = 1.0 / jmax(1.0 - fb_raw, 1.0e-6);
		return jlimit(0.0, 1.0, mag / max_mag);
	}

	if (isFeedforward)
	{
		// fb_gain is Q-controlled (always ≥ 0); isInv flips sign for display.
		// Effective feedforward gain at ω: fb_eff = fb_signed × |H_filter(ω)|
		// Feedforward magnitude: |H|² = 1 + fb_eff² + 2·fb_eff·cos(ωD)
		// Normalised by (1 + fb_raw) so peak = 1 when filter is flat.
		const double fb_raw    = jlimit(0.0, 0.90, typed->fb_gain);
		const double fb_signed = isInv ? -fb_raw : fb_raw;
		const double fb_eff    = fb_signed * filterMag();

		const double D   = typed->delayLength;
		const double mag = std::sqrt(jmax(0.0001, 1.0 + fb_eff*fb_eff + 2.0*fb_eff*std::cos(omega*D)));
		return jlimit(0.0, 1.0, mag / (1.0 + fb_raw + 1.0e-12));
	}

	// FlangerFB / FlangerFBInv: legacy Gain → feedback depth, Q → internal LP damping.
	// fb sourced from Gain (unchanged).
	const double gain_dB = Decibels::gainToDecibels(jmax(typed->storedGain, 1.0e-6));
	double g = jlimit(-0.99, 0.99, gain_dB / 18.0);
	if (isInv) g = -g;

	const std::complex<double> z_neg_N(std::cos(omega * typed->delayLength),
	                                   -std::sin(omega * typed->delayLength));

	// H_lp(z) = α / (1 − (1−α)·z^{−1})
	const std::complex<double> e_neg(std::cos(omega), -std::sin(omega));
	const double alpha = typed->fb_lp_coeff;
	const std::complex<double> H_fb = (alpha > 1.0e-6)
		? alpha / (1.0 - (1.0 - alpha) * e_neg)
		: std::complex<double>(0.0, 0.0);

	const std::complex<double> num = 1.0 + g * z_neg_N * (1.0 - H_fb);
	const std::complex<double> den = 1.0 - g * H_fb * z_neg_N;

	if (std::abs(den) < 1.0e-10)
		return 10.0;

	return std::abs(num / den);
}

void CombFilterSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
	const bool isInv = (mode == InvComb || mode == FeedforwardInvComb ||
	                    mode == FlangerInv || mode == FlangerFBInv);
	const double effective_fb = isInv ? -fb_gain : fb_gain;

	// FeedforwardComb / FeedforwardInvComb produce phaser-style notches
	// (comb peaks at multiples of f) without any feedback resonance.
	const bool isFeedforward = (mode == FeedforwardComb || mode == FeedforwardInvComb ||
	                            mode == Flanger          || mode == FlangerInv);
	const bool isCombFB      = (mode == Comb || mode == InvComb);

	const int numChannels = buffer.getNumChannels();

	for (int c = 0; c < numChannels; c++)
	{
		float*  d  = buffer.getWritePointer(c, startSample);
		double* dl = delayLine.data() + c * maxDelayLine;

		for (int i = 0; i < numSamples; i++)
		{
			const double input = (double)d[i];

			// Read from delay line (integer delay)
			const int readPos = (writePos[c] - delayLengthInt + maxDelayLine) % maxDelayLine;
			const double delayed_raw = dl[readPos];

			// All-pass fractional interpolation: y[n] = η*(x[n]-y[n-1]) + x[n-1]
			const double ap_out = ap_coeff * (delayed_raw - ap_s[c]) + ap_s_prev[c];
			ap_s_prev[c] = delayed_raw;
			ap_s[c]      = ap_out;

			double output;

			if (isFeedforward)
			{
				// Feedforward: y[n] = x[n] + g * filtered(x[n-D])
				// Gain-controlled LP/HP applied to the delayed signal before mixing.
				// Only dry input is written to the delay buffer (no feedback).
				lp_s[c] = (1.0f - lp_coeff) * (float)ap_out + lp_coeff * lp_s[c];
				const float ff_filtered = fb_is_hp ? ((float)ap_out - lp_s[c]) : lp_s[c];
				output = input + effective_fb * (double)ff_filtered;
				dl[writePos[c]] = input;
			}
			else if (isCombFB)
			{
				// Feedback comb: Gain-controlled LP/HP applied to delayed signal in feedback path.
				lp_s[c] = (1.0f - lp_coeff) * (float)ap_out + lp_coeff * lp_s[c];
				const float fb_filtered = fb_is_hp ? ((float)ap_out - lp_s[c]) : lp_s[c];
				output = input + effective_fb * (double)fb_filtered;
				dl[writePos[c]] = output;
			}
			else
			{
				// FlangerFB: LP in the write path via fb_lp_coeff (unchanged)
				output = input + effective_fb * ap_out;
				fb_lp_s[c] += fb_lp_coeff * (ap_out - fb_lp_s[c]);
				dl[writePos[c]] = input + effective_fb * fb_lp_s[c];
			}

			writePos[c] = (writePos[c] + 1) % maxDelayLine;
			d[i] = (float)output;
		}
	}
}

void CombFilterSubType::processFrame(float* frameData, int numChannels)
{
	const bool isInv = (mode == InvComb || mode == FeedforwardInvComb ||
	                    mode == FlangerInv || mode == FlangerFBInv);
	const double effective_fb = isInv ? -fb_gain : fb_gain;

	const bool isFeedforward = (mode == FeedforwardComb || mode == FeedforwardInvComb ||
	                            mode == Flanger          || mode == FlangerInv);
	const bool isCombFB      = (mode == Comb || mode == InvComb);

	for (int c = 0; c < numChannels; c++)
	{
		const double input = (double)frameData[c];
		double* dl = delayLine.data() + c * maxDelayLine;

		// Read from delay line (integer delay)
		const int readPos = (writePos[c] - delayLengthInt + maxDelayLine) % maxDelayLine;
		const double delayed_raw = dl[readPos];

		// All-pass fractional interpolation
		const double ap_out = ap_coeff * (delayed_raw - ap_s[c]) + ap_s_prev[c];
		ap_s_prev[c] = delayed_raw;
		ap_s[c]      = ap_out;

		double output;

		if (isFeedforward)
		{
			// Feedforward: Gain-controlled LP/HP applied to the delayed signal.
			lp_s[c] = (1.0f - lp_coeff) * (float)ap_out + lp_coeff * lp_s[c];
			const float ff_filtered = fb_is_hp ? ((float)ap_out - lp_s[c]) : lp_s[c];
			output = input + effective_fb * (double)ff_filtered;
			dl[writePos[c]] = input;
		}
		else if (isCombFB)
		{
			// Feedback comb: Gain-controlled LP/HP applied to delayed signal in feedback path.
			lp_s[c] = (1.0f - lp_coeff) * (float)ap_out + lp_coeff * lp_s[c];
			const float fb_filtered = fb_is_hp ? ((float)ap_out - lp_s[c]) : lp_s[c];
			output = input + effective_fb * (double)fb_filtered;
			dl[writePos[c]] = output;
		}
		else
		{
			// FlangerFB: LP in the write path via fb_lp_coeff (unchanged)
			output = input + effective_fb * ap_out;
			fb_lp_s[c] += fb_lp_coeff * (ap_out - fb_lp_s[c]);
			dl[writePos[c]] = input + effective_fb * fb_lp_s[c];
		}

		writePos[c] = (writePos[c] + 1) % maxDelayLine;
		frameData[c] = (float)output;
	}
}




DEFINE_MULTI_CHANNEL_FILTER(CombFilterSubType);

// ============================================================================
// FormantFilterSubType
// ============================================================================

// [voiceType][vowel][formant][0=freqHz, 1=dB, 2=bandwidthHz]
// Voices: Bass, Tenor, CTenor, Alto, Soprano
// Vowels: u, o, a, e, i
const float FormantFilterSubType::formantData[5][5][5][3] =
{
	// Bass
	{
		{{350,0,40},{600,-20,80},{2400,-32,100},{2675,-28,120},{2950,-36,120}},   // u
		{{400,0,40},{750,-11,80},{2400,-21,100},{2600,-20,120},{2900,-40,120}},   // o
		{{600,0,60},{1040,-7,70},{2250,-9,110},{2450,-9,120},{2750,-20,130}},     // a
		{{400,0,40},{1620,-12,80},{2400,-9,100},{2800,-12,120},{3100,-18,120}},   // e
		{{250,0,60},{1750,-30,90},{2600,-16,100},{3050,-22,120},{3340,-28,120}},  // i
	},
	// Tenor
	{
		{{350,0,40},{600,-20,60},{2700,-17,100},{2900,-14,120},{3300,-26,120}},   // u
		{{400,0,40},{800,-10,80},{2600,-12,100},{2800,-12,120},{3000,-26,120}},   // o
		{{650,0,80},{1080,-6,90},{2650,-7,120},{2900,-8,130},{3250,-22,140}},     // a
		{{400,0,70},{1700,-14,80},{2600,-12,100},{3200,-14,120},{3580,-20,120}},  // e
		{{290,0,40},{1870,-15,90},{2800,-18,100},{3250,-20,120},{3540,-30,120}},  // i
	},
	// CTenor
	{
		{{370,0,40},{630,-20,80},{2750,-23,100},{3000,-30,120},{3400,-34,120}},   // u
		{{430,0,40},{820,-10,80},{2700,-26,100},{3000,-22,120},{3300,-34,120}},   // o
		{{660,0,80},{1120,-6,90},{2750,-23,120},{3000,-24,130},{3350,-38,140}},   // a
		{{440,0,70},{1800,-14,80},{2700,-18,100},{3000,-20,120},{3300,-20,120}},  // e
		{{270,0,40},{1850,-24,90},{2900,-24,100},{3350,-36,120},{3590,-36,120}},  // i
	},
	// Alto
	{
		{{325,0,50},{700,-12,60},{2530,-30,170},{3500,-40,180},{4950,-64,200}},   // u
		{{450,0,70},{800,-9,80},{2830,-16,100},{3500,-28,130},{4950,-35,135}},    // o
		{{800,0,80},{1150,-4,90},{2800,-20,120},{3500,-36,130},{4950,-60,140}},   // a
		{{400,0,60},{1600,-24,80},{2700,-20,120},{3300,-35,150},{4950,-60,200}},  // e
		{{350,0,50},{1700,-20,100},{2700,-30,120},{3700,-36,150},{4950,-60,200}}, // i
	},
	// Soprano
	{
		{{325,0,50},{700,-16,60},{2700,-35,170},{3800,-40,180},{4950,-60,200}},   // u
		{{450,0,70},{800,-11,80},{2830,-22,100},{3800,-22,130},{4950,-50,135}},   // o
		{{800,0,80},{1150,-6,90},{2900,-32,120},{3900,-20,130},{4950,-50,140}},   // a
		{{350,0,60},{2000,-20,100},{2800,-15,120},{3600,-40,150},{4950,-56,200}}, // e
		{{270,0,60},{2140,-12,90},{2950,-26,100},{3900,-26,120},{4950,-44,120}},  // i
	},
};

FormantFilterSubType::FormantFilterSubType()
{
	reset(NUM_MAX_CHANNELS);
	updateCoefficients(44100.0, 440.0, 0.5, 1.0);
}

hise::FilterHelpers::FilterSubType FormantFilterSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::FormantFilterSubType;
}

juce::Identifier FormantFilterSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("formant");
}

juce::StringArray FormantFilterSubType::getModes() const
{
	return { "Bass", "Tenor", "CTenor", "Alto", "Soprano" };
}

Array<hise::FilterHelpers::CoefficientType> FormantFilterSubType::getCoefficientTypeList() const
{
	return { FilterHelpers::BandPass, FilterHelpers::BandPass,
	         FilterHelpers::BandPass, FilterHelpers::BandPass,
	         FilterHelpers::BandPass };
}

void FormantFilterSubType::setType(int t)
{
	mode = (Mode)jlimit(0, (int)numModes - 1, t);
}

void FormantFilterSubType::reset(int /*numChannels*/)
{
	memset(x1, 0, sizeof(x1));
	memset(x2, 0, sizeof(x2));
	memset(y1, 0, sizeof(y1));
	memset(y2, 0, sizeof(y2));
}

void FormantFilterSubType::updateCoefficients(double sampleRate, double frequency, double q, double gain)
{
	storedSampleRate = sampleRate;
	storedFrequency  = frequency;
	storedQ          = (float)q;
	storedGain       = gain;

	// Voice type from mode parameter
	const int voiceIndex = (int)mode;

	// Vowel position: log scale so the perceptual vowel spread feels even
	// freq=20 Hz → u (0.0),  freq=20000 Hz → i (1.0)
	const double vowelPos  = jlimit(0.0, 1.0,
		std::log(jmax(frequency, 20.0) / 20.0) / std::log(20000.0 / 20.0));
	const double vowelIdxD = vowelPos * 4.0;
	const int    v0        = jlimit(0, 3, (int)vowelIdxD);
	const int    v1        = v0 + 1;
	const double blend     = vowelIdxD - (double)v0;

	// Color (Q): bandwidth scale 1x–3x — higher Q = narrower formants
	const double bandwidthScale = 1.0 + (q - 0.3) / 9.6 * 2.0;

	// Shift (Gain): gain arrives as linear amplitude → convert to dB
	const double gainDB    = jlimit(-100.0, 100.0,
		(double)Decibels::gainToDecibels(jmax((float)gain, 1.0e-6f)));
	const double freqScale = std::pow(2.0, gainDB / 18.0);

	const float sr = (float)sampleRate;

	for (int f = 0; f < 5; f++)
	{
		// Interpolate formant parameters between adjacent vowels
		const double iFq = formantData[voiceIndex][v0][f][0] * (1.0 - blend)
		                 + formantData[voiceIndex][v1][f][0] * blend;
		const double idB = formantData[voiceIndex][v0][f][1] * (1.0 - blend)
		                 + formantData[voiceIndex][v1][f][1] * blend;
		const double iBW = formantData[voiceIndex][v0][f][2] * (1.0 - blend)
		                 + formantData[voiceIndex][v1][f][2] * blend;

		const float fc        = jlimit(20.0f, sr * 0.499f, (float)(iFq * freqScale));
		const float Q_f       = fc / jmax(1.0f, (float)(iBW / bandwidthScale));
		const float linearAmp = Decibels::decibelsToGain((float)idB);
		const float gain_f    = linearAmp * Q_f;

		const float omega = 2.0f * float_Pi * fc / sr;
		const float alpha = 0.5f * sinf(omega) / jmax(0.001f, Q_f);
		const float norm  = 1.0f / (1.0f + alpha);

		b0[f] =  alpha * gain_f * norm;
		b2[f] = -alpha * gain_f * norm;
		a1[f] = -2.0f * cosf(omega) * norm;
		a2[f] = (1.0f - alpha) * norm;
	}
}

FilterCoefficientData FormantFilterSubType::getCoefficients(double freqNorm, double q, double gain) const
{
	auto* m = const_cast<FormantFilterSubType*>(this);
	if (storedSampleRate > 0.0)
		m->storedFrequency = jlimit(20.0, 20000.0, freqNorm * storedSampleRate);
	m->storedQ    = (float)q;
	m->storedGain = gain;

	FilterCoefficientData cd;
	cd.customFunction = getPlotValue;
	cd.obj = m;
	return cd;
}

double FormantFilterSubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
	if (!getMagnitude)
		return 0.0;

	auto* typed = static_cast<FormantFilterSubType*>(obj);
	const double sr = typed->storedSampleRate;

	// Reconstruct parameters from stored display values
	const int voiceIndex = (int)typed->mode;

	const double vowelPos  = jlimit(0.0, 1.0,
		std::log(jmax(typed->storedFrequency, 20.0) / 20.0) / std::log(20000.0 / 20.0));
	const double vowelIdxD = vowelPos * 4.0;
	const int    v0        = jlimit(0, 3, (int)vowelIdxD);
	const int    v1        = v0 + 1;
	const double blend     = vowelIdxD - (double)v0;

	const double bandwidthScale = 1.0 + ((double)typed->storedQ - 0.3) / 9.6 * 2.0;

	const double gainDB    = jlimit(-100.0, 100.0,
		(double)Decibels::gainToDecibels(jmax((float)typed->storedGain, 1.0e-6f)));
	const double freqScale = std::pow(2.0, gainDB / 18.0);

	// freqNorm = inputFrequency / sampleRate (linear digital frequency, range 0–0.5).
	// The FilterGraph already converts log-scale x → Hz → normFreq before calling us,
	// so we evaluate the TF at the digital frequency omega = 2π·freqNorm directly —
	// the same convention used by MoogLadder, DiodeLadder, CombFilter, etc.
	const double w      = 2.0 * double_Pi * freqNorm;
	const double cos_w  = std::cos(w);
	const double cos_2w = std::cos(2.0 * w);

	double totalMag = 0.0;

	for (int f = 0; f < 5; f++)
	{
		const double iFq = formantData[voiceIndex][v0][f][0] * (1.0 - blend)
		                 + formantData[voiceIndex][v1][f][0] * blend;
		const double idB = formantData[voiceIndex][v0][f][1] * (1.0 - blend)
		                 + formantData[voiceIndex][v1][f][1] * blend;
		const double iBW = formantData[voiceIndex][v0][f][2] * (1.0 - blend)
		                 + formantData[voiceIndex][v1][f][2] * blend;

		const double fc    = jlimit(20.0, sr * 0.499, iFq * freqScale);
		const double Q_f   = fc / jmax(1.0, iBW / bandwidthScale);
		const double amp   = Decibels::decibelsToGain((float)idB);
		const double gain_f = amp * Q_f;

		const double omega  = 2.0 * double_Pi * fc / sr;
		const double alpha  = 0.5 * std::sin(omega) / jmax(0.001, Q_f);
		const double inv_n  = 1.0 / (1.0 + alpha);

		const double b0_d   =  alpha * gain_f * inv_n;
		const double b2_d   = -alpha * gain_f * inv_n;
		const double a1_d   = -2.0 * std::cos(omega) * inv_n;
		const double a2_d   = (1.0 - alpha) * inv_n;

		// |H(e^jw)|² with b1=0:
		// num = b0²+b2²+2*b0*b2*cos(2w)
		// den = 1+a1²+a2²+2*a1*(1+a2)*cos(w)+2*a2*cos(2w)
		const double num = b0_d*b0_d + b2_d*b2_d + 2.0*b0_d*b2_d*cos_2w;
		const double den = 1.0 + a1_d*a1_d + a2_d*a2_d
		                 + 2.0*a1_d*(1.0 + a2_d)*cos_w
		                 + 2.0*a2_d*cos_2w;

		if (den > 1.0e-12)
			totalMag += std::sqrt(jmax(0.0, num / den));
	}

	return totalMag * 0.7;
}

void FormantFilterSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
	const int numChannels = buffer.getNumChannels();

	for (int c = 0; c < numChannels; c++)
	{
		float* d = buffer.getWritePointer(c, startSample);

		for (int i = 0; i < numSamples; i++)
		{
			const float input = d[i];
			float sum = 0.0f;

			for (int f = 0; f < 5; f++)
			{
				// Direct Form I bandpass biquad (b1 = 0)
				const float y = b0[f] * input + b2[f] * x2[c][f]
				              - a1[f] * y1[c][f] - a2[f] * y2[c][f];
				x2[c][f] = x1[c][f];
				x1[c][f] = input;
				y2[c][f] = y1[c][f];
				y1[c][f] = y;
				sum += y;
			}

			d[i] = sum * 0.7f;
		}
	}
}

void FormantFilterSubType::processFrame(float* frameData, int numChannels)
{
	for (int c = 0; c < numChannels; c++)
	{
		const float input = frameData[c];
		float sum = 0.0f;

		for (int f = 0; f < 5; f++)
		{
			const float y = b0[f] * input + b2[f] * x2[c][f]
			              - a1[f] * y1[c][f] - a2[f] * y2[c][f];
			x2[c][f] = x1[c][f];
			x1[c][f] = input;
			y2[c][f] = y1[c][f];
			y1[c][f] = y;
			sum += y;
		}

		frameData[c] = sum * 0.7f;
	}
}

DEFINE_MULTI_CHANNEL_FILTER(FormantFilterSubType);


// ============================================================================
// DisperserFilterSubType
// ============================================================================

DisperserFilterSubType::DisperserFilterSubType()
{
	reset(NUM_MAX_CHANNELS);
	updateCoefficients(44100.0, 1000.0, 1.0, 1.0);
}

hise::FilterHelpers::FilterSubType DisperserFilterSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::DisperserFilterSubType;
}

juce::Identifier DisperserFilterSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("disperser");
}

juce::StringArray DisperserFilterSubType::getModes() const
{
	return { "Disperser" };
}

Array<hise::FilterHelpers::CoefficientType> DisperserFilterSubType::getCoefficientTypeList() const
{
	return { FilterHelpers::AllPass };
}

void DisperserFilterSubType::setType(int /*t*/)
{
	// Single mode only
}

void DisperserFilterSubType::reset(int /*numChannels*/)
{
	memset(x1, 0, sizeof(x1));
	memset(x2, 0, sizeof(x2));
	memset(y1, 0, sizeof(y1));
	memset(y2, 0, sizeof(y2));
}

void DisperserFilterSubType::updateCoefficients(double sampleRate, double frequency, double q, double gain)
{
	storedSampleRate = sampleRate;
	storedFrequency  = frequency;
	storedQ          = q;
	storedGain       = gain;

	const double omega    = 2.0 * double_Pi * frequency / sampleRate;
	const double alpha    = std::sin(omega) / (2.0 * jmax(0.001, q));
	const double a0       = 1.0 + alpha;

	nb0 = (float)((1.0 - alpha) / a0);
	nb1 = (float)((-2.0 * std::cos(omega)) / a0);

	// Map gain (linear amplitude) ±18 dB → 1–MAX_STAGES allpass stages
	const double gainDB = jlimit(-18.0, 18.0,
		(double)Decibels::gainToDecibels(jmax((float)gain, 1.0e-6f)));
	numStages = 1 + (int)((gainDB + 18.0) / 36.0 * (double)(MAX_STAGES - 1));
	numStages = jlimit(1, MAX_STAGES, numStages);
}

FilterCoefficientData DisperserFilterSubType::getCoefficients(double freqNorm, double q, double gain) const
{
	auto* m = const_cast<DisperserFilterSubType*>(this);
	if (storedSampleRate > 0.0)
		m->storedFrequency = jlimit(20.0, 20000.0, freqNorm * storedSampleRate);
	m->storedQ    = q;
	m->storedGain = gain;

	FilterCoefficientData cd;
	cd.customFunction = getPlotValue;
	cd.obj = m;
	return cd;
}

double DisperserFilterSubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
	// We display phase response in the magnitude slot; phase slot returns 0.
	if (!getMagnitude)
		return 0.0;

	auto* typed = static_cast<DisperserFilterSubType*>(obj);
	const double sr = typed->storedSampleRate;

	if (sr <= 0.0)
		return 0.0;

	// Reconstruct normalised biquad coefficients from stored parameters
	const double freq     = jlimit(20.0, 20000.0, typed->storedFrequency);
	const double q        = jmax(0.001, typed->storedQ);
	const double gainDB   = jlimit(-18.0, 18.0,
		(double)Decibels::gainToDecibels(jmax((float)typed->storedGain, 1.0e-6f)));

	const double omega_c  = 2.0 * double_Pi * freq / sr;
	const double alpha    = std::sin(omega_c) / (2.0 * q);
	const double a0       = 1.0 + alpha;
	const double nb0_d    = (1.0 - alpha) / a0;
	const double nb1_d    = (-2.0 * std::cos(omega_c)) / a0;

	const int ns = jlimit(1, MAX_STAGES,
		1 + (int)((gainDB + 18.0) / 36.0 * (double)(MAX_STAGES - 1)));

	// Phase of H(e^jw) for one second-order allpass stage at normalised frequency w.
	// freqNorm = plotFreq / sampleRate, so w = 2π * freqNorm.
	const double w = 2.0 * double_Pi * freqNorm;

	auto phaseSingleStage = [&](double ww) -> double
	{
		const double cos_w  = std::cos(ww);
		const double sin_w  = std::sin(ww);
		const double cos_2w = std::cos(2.0 * ww);
		const double sin_2w = std::sin(2.0 * ww);

		// H(e^jw) = (nb0 + nb1*e^-jw + e^-2jw) / (1 + nb1*e^-jw + nb0*e^-2jw)
		const double num_re = nb0_d + nb1_d * cos_w + cos_2w;
		const double num_im =        -nb1_d * sin_w - sin_2w;
		const double den_re = 1.0   + nb1_d * cos_w + nb0_d * cos_2w;
		const double den_im =        -nb1_d * sin_w - nb0_d * sin_2w;

		return std::atan2(num_im, num_re) - std::atan2(den_im, den_re);
	};

	// Single-stage phase is in [-π, 0]. Map to [0, 1] using cos to avoid
	// wrapping artifacts from multiplying by ns before normalising:
	//   DC (phase=0):      cos(ns*0   + ns*π) = cos(ns*π) → alternates ±1
	//   Nyquist (phase=-π): cos(ns*(-π) + ns*π) = cos(0) = 1
	// Offset by ns*π folds all stages so DC=1, Nyquist=0 regardless of ns.
	const double phase1     = phaseSingleStage(w);   // single stage, range [-π, 0]
	const double normalised = 0.5 + 0.5 * std::cos((double)ns * phase1 + double_Pi * (double)ns);
	return jlimit(0.0, 1.0, normalised);
}

void DisperserFilterSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
	const int   numChannels = buffer.getNumChannels();
	const float _nb0        = nb0;
	const float _nb1        = nb1;
	const int   ns          = numStages;

	for (int c = 0; c < numChannels; c++)
	{
		float* d = buffer.getWritePointer(c, startSample);

		for (int i = 0; i < numSamples; i++)
		{
			float x = d[i];

			for (int s = 0; s < ns; s++)
			{
				// Direct Form I: y = nb0*x + nb1*x1 + 1*x2 - nb1*y1 - nb0*y2
				const float y = _nb0 * x + _nb1 * x1[c][s] + x2[c][s]
				              - _nb1 * y1[c][s] - _nb0 * y2[c][s];
				x2[c][s] = x1[c][s];
				x1[c][s] = x;
				y2[c][s] = y1[c][s];
				y1[c][s] = y;
				x = y;   // stage output feeds next stage
			}

			d[i] = x;
		}
	}
}

void DisperserFilterSubType::processFrame(float* frameData, int numChannels)
{
	const float _nb0 = nb0;
	const float _nb1 = nb1;
	const int   ns   = numStages;

	for (int c = 0; c < numChannels; c++)
	{
		float x = frameData[c];

		for (int s = 0; s < ns; s++)
		{
			const float y = _nb0 * x + _nb1 * x1[c][s] + x2[c][s]
			              - _nb1 * y1[c][s] - _nb0 * y2[c][s];
			x2[c][s] = x1[c][s];
			x1[c][s] = x;
			y2[c][s] = y1[c][s];
			y1[c][s] = y;
			x = y;
		}

		frameData[c] = x;
	}
}

DEFINE_MULTI_CHANNEL_FILTER(DisperserFilterSubType);


// ============================================================================
// ZDFDiodeLadderSubType implementation
// ============================================================================

hise::FilterHelpers::FilterSubType ZDFDiodeLadderSubType::getFilterType()
{
    return FilterHelpers::FilterSubType::ZDFDiodeLadderSubType;
}

juce::Identifier ZDFDiodeLadderSubType::getStaticId()
{
    RETURN_STATIC_IDENTIFIER("zdf_diode");
}

ZDFDiodeLadderSubType::ZDFDiodeLadderSubType()
{
    memset(z0, 0, sizeof(z0));
    memset(z1, 0, sizeof(z1));
    memset(z2, 0, sizeof(z2));
    memset(z3, 0, sizeof(z3));
}

juce::StringArray ZDFDiodeLadderSubType::getModes() const
{
    return { "ZDF Diode LP"};
}

Array<hise::FilterHelpers::CoefficientType> ZDFDiodeLadderSubType::getCoefficientTypeList() const
{
    return { FilterHelpers::LowPassReso };
}

FilterCoefficientData ZDFDiodeLadderSubType::getCoefficients(double freqNorm, double q, double /*gain*/) const
{
    auto* m = const_cast<ZDFDiodeLadderSubType*>(this);
    if (storedSampleRate > 0.0)
        m->storedFrequency = freqNorm * storedSampleRate;
    m->storedQ = q;

    FilterCoefficientData cd;
    cd.customFunction = getPlotValue;
    cd.obj = m;
    return cd;
}

void ZDFDiodeLadderSubType::setType(int t)
{
    mode = 0;
}

void ZDFDiodeLadderSubType::reset(int /*numChannels*/)
{
    memset(z0, 0, sizeof(z0));
    memset(z1, 0, sizeof(z1));
    memset(z2, 0, sizeof(z2));
    memset(z3, 0, sizeof(z3));
}

void ZDFDiodeLadderSubType::updateCoefficients(double sampleRate, double frequency,
                                                double q, double /*gain*/)
{
    storedSampleRate = sampleRate;
    storedFrequency  = frequency;
    storedQ          = q;

    k        = jlimit(0.0, 20.0, (q - 0.3) / (9.9 - 0.3) * 20.0);
    amp_comp = 1.0 + 0.5 * k;

    const double wc_raw = double_Pi * frequency / sampleRate;
    wc4  = 2.0 * std::tan(0.5 * wc_raw);
    wc2  = wc4 * wc4;
    wc3  = wc2 * wc4;
    wc4p = wc3 * wc4;

    const double denom = 1.0 + 8.0*wc4 + 20.0*wc2 + 16.0*wc3 + 2.0*wc4p;
    b = (denom == 0.0) ? 0.0 : 1.0 / denom;
    g = 2.0 * wc4p * b;
}

void ZDFDiodeLadderSubType::processSamples(AudioSampleBuffer& buffer,
                                            int startSample, int numSamples)
{
    const int numChannels = buffer.getNumChannels();

    for (int c = 0; c < numChannels; c++)
    {
        float* d = buffer.getWritePointer(c, startSample);

        for (int i = 0; i < numSamples; i++)
        {
            const double interp     = (numSamples > 1)
                                    ? (double)i / (double)(numSamples - 1) : 1.0;
            const double k_i        = k_prev        + (k        - k_prev)        * interp;
            const double amp_comp_i = amp_comp_prev + (amp_comp - amp_comp_prev) * interp;

            const double xn = (double)d[i];

            const double sv = (z0[c]*wc3
                             + z1[c]*(wc2 + 2.0*wc3)
                             + z2[c]*(wc4 + 4.0*wc2 + 2.0*wc3)
                             + z3[c]*(1.0 + 6.0*wc4 + 9.0*wc2
                                          + 2.0*wc3)) * b;

            const double denom_fb = 1.0 + g * k_i;
            const double y4 = (denom_fb == 0.0) ? 0.0
                            : (g * xn + sv) / denom_fb;
            const double y0 = std::tanh(xn - k_i * y4);

            const double y1 = (y0*(2.0*wc4 + 12.0*wc2 + 20.0*wc3 + 8.0*wc4p)
                             + z0[c]*(1.0 + 6.0*wc4 + 10.0*wc2 + 4.0*wc3)
                             + z1[c]*(2.0*wc4 + 8.0*wc2 + 6.0*wc3)
                             + z2[c]*(2.0*wc2 + 4.0*wc3)
                             + z3[c]*2.0*wc3) * b;

            const double y2 = (y0*(2.0*wc2 + 8.0*wc3 + 6.0*wc4p)
                             + z0[c]*(wc4 + 4.0*wc2 + 3.0*wc3)
                             + z1[c]*(1.0 + 6.0*wc4 + 11.0*wc2 + 6.0*wc3)
                             + z2[c]*(wc4 + 4.0*wc2 + 4.0*wc3)
                             + z3[c]*(wc2 + 2.0*wc3)) * b;

            const double y3 = (y0*(2.0*wc3 + 4.0*wc4p)
                             + z0[c]*(wc2 + 2.0*wc3)
                             + z1[c]*(wc4 + 4.0*wc2 + 4.0*wc3)
                             + z2[c]*(1.0 + 6.0*wc4 + 10.0*wc2 + 4.0*wc3)
                             + z3[c]*(wc4 + 4.0*wc2 + 2.0*wc3)) * b;

            const double y4out = g * y0 + sv;

            z0[c] += 4.0*wc4*(y0 - y1 + y2);
            z1[c] += 2.0*wc4*(y1 - 2.0*y2 + y3);
            z2[c] += 2.0*wc4*(y2 - 2.0*y3 + y4out);
            z3[c] += 2.0*wc4*(y3 - 2.0*y4out);

            const double lp = amp_comp_i * y4out;
            const double hp = xn - y4out;
            d[i] = (float)(mode == 1 ? hp : lp);
        }
    }

    k_prev        = k;
    amp_comp_prev = amp_comp;
}

void ZDFDiodeLadderSubType::processFrame(float* frameData, int numChannels)
{
    const double k_i        = k;
    const double amp_comp_i = amp_comp;

    for (int c = 0; c < numChannels; c++)
    {
        const double xn = (double)frameData[c];

        const double sv = (z0[c]*wc3
                         + z1[c]*(wc2 + 2.0*wc3)
                         + z2[c]*(wc4 + 4.0*wc2 + 2.0*wc3)
                         + z3[c]*(1.0 + 6.0*wc4 + 9.0*wc2
                                      + 2.0*wc3)) * b;

        const double denom_fb = 1.0 + g * k_i;
        const double y4 = (denom_fb == 0.0) ? 0.0
                        : (g * xn + sv) / denom_fb;
        const double y0 = std::tanh(xn - k_i * y4);

        const double y1 = (y0*(2.0*wc4 + 12.0*wc2 + 20.0*wc3 + 8.0*wc4p)
                         + z0[c]*(1.0 + 6.0*wc4 + 10.0*wc2 + 4.0*wc3)
                         + z1[c]*(2.0*wc4 + 8.0*wc2 + 6.0*wc3)
                         + z2[c]*(2.0*wc2 + 4.0*wc3)
                         + z3[c]*2.0*wc3) * b;

        const double y2 = (y0*(2.0*wc2 + 8.0*wc3 + 6.0*wc4p)
                         + z0[c]*(wc4 + 4.0*wc2 + 3.0*wc3)
                         + z1[c]*(1.0 + 6.0*wc4 + 11.0*wc2 + 6.0*wc3)
                         + z2[c]*(wc4 + 4.0*wc2 + 4.0*wc3)
                         + z3[c]*(wc2 + 2.0*wc3)) * b;

        const double y3 = (y0*(2.0*wc3 + 4.0*wc4p)
                         + z0[c]*(wc2 + 2.0*wc3)
                         + z1[c]*(wc4 + 4.0*wc2 + 4.0*wc3)
                         + z2[c]*(1.0 + 6.0*wc4 + 10.0*wc2 + 4.0*wc3)
                         + z3[c]*(wc4 + 4.0*wc2 + 2.0*wc3)) * b;

        const double y4out = g * y0 + sv;

        z0[c] += 4.0*wc4*(y0 - y1 + y2);
        z1[c] += 2.0*wc4*(y1 - 2.0*y2 + y3);
        z2[c] += 2.0*wc4*(y2 - 2.0*y3 + y4out);
        z3[c] += 2.0*wc4*(y3 - 2.0*y4out);

        const double lp = amp_comp_i * y4out;
        const double hp = xn - y4out;
        frameData[c] = (float)(mode == 1 ? hp : lp);
    }
}

double ZDFDiodeLadderSubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
    auto* typed = static_cast<ZDFDiodeLadderSubType*>(obj);
    const double sr = typed->storedSampleRate;

    const double freq  = jmax(20.0, jmin(sr * 0.499, typed->storedFrequency));
    const double g_val = std::tan(double_Pi * freq / sr);
    const double G     = g_val / (1.0 + g_val);
    const double fb    = jmin(3.9, jmax(0.0, (typed->storedQ - 0.3) * (3.8 / 9.699)));

    const double omega = 2.0 * double_Pi * freqNorm;
    const double a     = 1.0 - 2.0 * G;
    const std::complex<double> e_neg(std::cos(omega), -std::sin(omega));
    const std::complex<double> H  = G * (1.0 + e_neg) / (1.0 - a * e_neg);
    const std::complex<double> H2 = H * H;
    const std::complex<double> H4 = H2 * H2;

    if (typed->mode == 1)
    {
        const std::complex<double> hm = 1.0 - H;
        const std::complex<double> N  = hm*hm*hm*hm;
        const std::complex<double> D  = 1.0 + fb * H4;
        return getMagnitude ? std::abs(N / D) : std::arg(N / D);
    }

    const std::complex<double> D        = 1.0 + fb * H4;
    const std::complex<double> response = H4 / D;
    return getMagnitude ? std::abs(response) : std::arg(response);
}

DEFINE_MULTI_CHANNEL_FILTER(ZDFDiodeLadderSubType);



hise::FilterHelpers::FilterSubType SimpleOnePoleSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::SimpleOnePoleSubType;
}

juce::Identifier SimpleOnePoleSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("one_pole");
}

juce::StringArray SimpleOnePoleSubType::getModes() const
{
	return { "LP", "HP" };
}

Array<hise::FilterHelpers::CoefficientType> SimpleOnePoleSubType::getCoefficientTypeList() const
{
	return { FilterHelpers::LowPass, FilterHelpers::HighPass };
}

double SimpleOnePoleSubType::getPlotValue(void* obj, bool getMagnitude, double normX)
{
	auto typed = static_cast<FilterCoefficientData*>(obj);

	auto freqNorm = typed->first.coefficients[0];
	auto isLP = typed->first.coefficients[1] == 1.0;

	const auto w = freqNorm;

	freqNorm *= freqNorm;
	normX *= normX;

	if(isLP)
	{
		return 1.0 - w / std::sqrt(freqNorm + normX);
	}
	else
	{
		return w / std::sqrt(freqNorm + normX);
	}
}

FilterCoefficientData SimpleOnePoleSubType::getCoefficients(double freqNorm, double, double) const
{
	FilterCoefficientData d;
	d.first.coefficients[0] = freqNorm;
	d.first.coefficients[1] = (float)(int)onePoleType;

	d.customFunction = getPlotValue;
	return d;
}

SimpleOnePoleSubType::SimpleOnePoleSubType()
{
	onePoleType = SimpleOnePoleSubType::LP;
	memset(lastValues, 0, sizeof(float)*NUM_MAX_CHANNELS);
}

void SimpleOnePoleSubType::setType(int t)
{
	onePoleType = (FilterType)t;
}

void SimpleOnePoleSubType::reset(int numChannels)
{
	memset(lastValues, 0, sizeof(float)*numChannels);
}

void SimpleOnePoleSubType::updateCoefficients(double sampleRate, double frequency, double /*q*/, double /*gain*/)
{
	if(sampleRate > 0.0)
	{
		const double sr_inv = 1.0 / sampleRate;
		const double x = exp(-2.0*double_Pi*frequency * sr_inv);

		a0 = (float)(1.0 - x);
		b1 = (float)-x;
	}

}

void SimpleOnePoleSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
	lastChannelAmount = buffer.getNumChannels();

	switch (onePoleType)
	{
	case FilterType::HP:
	{
		for (int c = 0; c < lastChannelAmount; c++)
		{
			float *d = buffer.getWritePointer(c, startSample);

			for (int i = 0; i < numSamples; i++)
			{
				const float tmp = a0 * d[i] - b1 * lastValues[c];
				lastValues[c] = tmp;
				d[i] = d[i] - tmp;

			}
		}

		break;
	}
	case FilterType::LP:
	{
		for (int c = 0; c < lastChannelAmount; c++)
		{
			float *d = buffer.getWritePointer(c, startSample);

			for (int i = 0; i < numSamples; i++)
			{
				d[i] = a0 * d[i] - b1 * lastValues[c];
				lastValues[c] = d[i];
			}
		}

		break;
	}
	default:
		jassertfalse;
		break;
	}
}

void SimpleOnePoleSubType::processFrame(float* d, int numChannels)
{
	switch (onePoleType)
	{
	case FilterType::HP:
	{
		for (int c = 0; c < numChannels; c++)
		{
			const float tmp = a0 * d[c] - b1 * lastValues[c];
			lastValues[c] = tmp;
			d[c] = d[c] - tmp;
		}

		break;
	}
	case FilterType::LP:
	{
		for (int c = 0; c < numChannels; c++)
		{
			d[c] = a0 * d[c] - b1 * lastValues[c];
			lastValues[c] = d[c];
		}

		break;
	}
	default:
		jassertfalse;
		break;
	}
}

DEFINE_MULTI_CHANNEL_FILTER(SimpleOnePoleSubType);

hise::FilterHelpers::FilterSubType RingmodFilterSubType::getFilterType()
{
    return FilterHelpers::FilterSubType::RingmodFilterSubType;
}

juce::Identifier RingmodFilterSubType::getStaticId()
{
    RETURN_STATIC_IDENTIFIER("ring_mod");
}

juce::StringArray RingmodFilterSubType::getModes() const
{
    return { "RingMod" };
}

Array<hise::FilterHelpers::CoefficientType> RingmodFilterSubType::getCoefficientTypeList() const
{
    return { FilterHelpers::AllPass };
}

void RingmodFilterSubType::updateCoefficients(double sampleRate, double frequency, double q, double /*gain*/)
{
    uptimeDelta = frequency / sampleRate * 2.0 * double_Pi;
    oscGain = jmap<float>((float)q, 0.3f, 9.9f, 0.0f, 1.0f);
    oscGain = jlimit<float>(0.0f, 1.0f, oscGain);
}

void RingmodFilterSubType::reset(int /*numChannels*/)
{
    uptime = 0.0;
}

void RingmodFilterSubType::setType(int /*t*/)
{

}

void RingmodFilterSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
    const float invGain = 1.0f - oscGain;

    int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; i++)
    {
        const float oscValue = oscGain * (float)std::sin(uptime);;

        for (int c = 0; c < numChannels; c++)
        {
            const float input = buffer.getSample(c, i + startSample);
            buffer.setSample(c, i + startSample, invGain * input + input * oscValue);
        }

        uptime += uptimeDelta;
    }
}

void RingmodFilterSubType::processFrame(float* d, int numChannels)
{
    const float invGain = 1.0f - oscGain;
    const float oscValue = oscGain * (float)std::sin(uptime);;

    for (int c = 0; c < numChannels; c++)
    {
        const float input = d[c];
        d[c] = invGain * input + input * oscValue;
    }

    uptime += uptimeDelta;
}

DEFINE_MULTI_CHANNEL_FILTER(RingmodFilterSubType);

hise::FilterHelpers::FilterSubType StaticBiquadSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::StaticBiquadSubType;
}

juce::Identifier StaticBiquadSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("biquad");
}

juce::StringArray StaticBiquadSubType::getModes() const
{
	return { "LowPass", "High Pass", "Low Shelf", "High Shelf", "Peak", "Reso Low" };
}

Array<hise::FilterHelpers::CoefficientType> StaticBiquadSubType::getCoefficientTypeList() const
{
	return { FilterHelpers::LowPass, FilterHelpers::HighPass,
			 FilterHelpers::LowShelf, FilterHelpers::HighShelf,
			 FilterHelpers::Peak, FilterHelpers::LowPassReso };
}

void StaticBiquadSubType::updateCoefficients(double sampleRate, double frequency, double q, double gain)
{
	switch (biquadType)
	{
	case LowPass:		currentCoefficients = IIRCoefficients::makeLowPass(sampleRate, frequency); break;
	case HighPass:		currentCoefficients = IIRCoefficients::makeHighPass(sampleRate, frequency); break;
	case LowShelf:		currentCoefficients = IIRCoefficients::makeLowShelf(sampleRate, frequency, q, (float)gain); break;
	case HighShelf:		currentCoefficients = IIRCoefficients::makeHighShelf(sampleRate, frequency, q, (float)gain); break;
	case Peak:			currentCoefficients = IIRCoefficients::makePeakFilter(sampleRate, frequency, q, (float)gain); break;
	case ResoLow:		currentCoefficients = IIRCoefficients::makeLowPass(sampleRate, frequency, q); break;
	default:							jassertfalse; break;
	}

	for (int i = 0; i < numChannels; i++)
	{
		filters[i].setCoefficients(currentCoefficients);
	}
}

void StaticBiquadSubType::setType(int newType)
{
	biquadType = (FilterType)newType;
	reset(numChannels);
}

void StaticBiquadSubType::reset(int numNewChannels)
{
	numChannels = numNewChannels;

	for (int i = 0; i < numChannels; i++)
	{
		filters[i].reset();
	}
}

void StaticBiquadSubType::processSamples(AudioSampleBuffer& b, int startSample, int numSamples)
{
	int channelAmount = b.getNumChannels();

	for (int i = 0; i < channelAmount; i++)
	{
		float* d = b.getWritePointer(i, startSample);
		filters[i].processSamples(d, numSamples);
	}
}

void StaticBiquadSubType::processFrame(float* d, int channels)
{
	for (int i = 0; i < channels; i++)
	{
		d[i] = filters[i].processSingleSampleRaw(d[i]);
	}
}

DEFINE_MULTI_CHANNEL_FILTER(StaticBiquadSubType);

hise::FilterHelpers::FilterSubType PhaseAllpassSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::PhaseAllpassSubType;
}

juce::Identifier PhaseAllpassSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("allpass");
}

juce::StringArray PhaseAllpassSubType::getModes() const
{
	return { "Phaser 3",     "Phaser 6",     "Phaser 12",
	         "Phaser 3 Inv", "Phaser 6 Inv", "Phaser 12 Inv" };
}

Array<hise::FilterHelpers::CoefficientType> PhaseAllpassSubType::getCoefficientTypeList() const
{
	return { FilterHelpers::AllPass, FilterHelpers::AllPass, FilterHelpers::AllPass,
	         FilterHelpers::AllPass, FilterHelpers::AllPass, FilterHelpers::AllPass };
}

void PhaseAllpassSubType::processSamples(AudioSampleBuffer& b, int startSample, int numSamples)
{
	// numStages inside each InternalFilter controls 6-stage (AP12) or 12-stage (AP24) operation.
	for (int c = 0; c < numFilters; c++)
		for (int i = 0; i < numSamples; i++)
		{
			float* d = b.getWritePointer(c, i + startSample);
			*d = filters[c].getNextSample(*d);
		}
}

void PhaseAllpassSubType::processFrame(float* d, int numChannels)
{
	for (int c = 0; c < numChannels; c++)
		d[c] = filters[c].getNextSample(d[c]);
}

void PhaseAllpassSubType::setType(int t)
{
	apMode = (Mode)jlimit(0, (int)numModes - 1, t);

	const bool isInv = (apMode == Phaser3Inv || apMode == Phaser6Inv || apMode == Phaser12Inv);
	const int  ns    = (apMode == Phaser12 || apMode == Phaser12Inv) ? 12
	               : (apMode == Phaser3  || apMode == Phaser3Inv)  ? 3
	               : 6;

	for (int i = 0; i < numFilters; i++)
	{
		filters[i].numStages = ns;
		filters[i].fb_inv    = isInv;
		filters[i].reset();
	}
}

void PhaseAllpassSubType::updateCoefficients(double sampleRate, double frequency, double q, double /*gain*/)
{
	if (sampleRate <= 0.0)
		return;

	storedSampleRate = sampleRate;
	storedFrequency  = frequency;
	storedQ          = q;

	const bool isInv = (apMode == Phaser3Inv || apMode == Phaser6Inv || apMode == Phaser12Inv);
	const int  ns    = (apMode == Phaser12 || apMode == Phaser12Inv) ? 12
	               : (apMode == Phaser3  || apMode == Phaser3Inv)  ? 3
	               : 6;

	for (int i = 0; i < numFilters; i++)
	{
		filters[i].fMin      = (float)frequency;
		filters[i].minDelay  = (float)(frequency / (sampleRate / 2.0));
		filters[i].feedback  = (float)jmap<double>(q, 0.3, 9.9, 0.0, 0.95);
		filters[i].numStages = ns;
		filters[i].fb_inv    = isInv;
	}
}

void PhaseAllpassSubType::reset(int newNumChannels)
{
	numFilters = newNumChannels;

	for (int i = 0; i < newNumChannels; i++)
		filters[i].reset();
}

FilterCoefficientData PhaseAllpassSubType::getCoefficients(double freqNorm, double q, double /*gain*/) const
{
	auto* m = const_cast<PhaseAllpassSubType*>(this);
	if (storedSampleRate > 0.0)
		m->storedFrequency = jlimit(20.0, 20000.0, freqNorm * storedSampleRate);
	m->storedQ = q;

	FilterCoefficientData cd;
	cd.customFunction = getPlotValue;
	cd.obj = m;
	return cd;
}

double PhaseAllpassSubType::getPlotValue(void* obj, bool getMagnitude, double freqNorm)
{
	if (!getMagnitude)
		return 0.0;

	auto* typed = static_cast<PhaseAllpassSubType*>(obj);
	const double sr   = typed->storedSampleRate;
	const double freq = jlimit(20.0, 20000.0, typed->storedFrequency);
	const double q    = jmax(0.001, typed->storedQ);

	const bool inv = (typed->apMode == Phaser3Inv ||
	                  typed->apMode == Phaser6Inv  ||
	                  typed->apMode == Phaser12Inv);

	const int ns = (typed->apMode == Phaser3 || typed->apMode == Phaser3Inv) ? 3 :
	               (typed->apMode == Phaser6 || typed->apMode == Phaser6Inv) ? 6 : 12;

	if (sr <= 0.0)
		return 0.5;

	// Q maps to feedback depth: higher Q = more feedback = deeper notches.
	const double fb = jmap(jlimit(0.3, 9.9, q), 0.3, 9.9, 0.0, 0.95);

	const double w = 2.0 * double_Pi * freqNorm;

	// Phase of one Schroeder first-order allpass stage at w.
	// AllpassDelay: H(z) = (-a + z^{-1}) / (1 - a*z^{-1})
	// where a = (1 - d) / (1 + d),  d = freq / (sr/2)
	const double d      = jlimit(0.0, 1.0, freq / (sr / 2.0));
	const double a      = (1.0 - d) / (1.0 + d);
	const double cw     = std::cos(w);
	const double sw     = std::sin(w);
	const double num_re = -a + cw;
	const double num_im = -sw;
	const double den_re = 1.0 - a * cw;
	const double den_im = a * sw;
	const double phi1   = std::atan2(num_im, num_re) - std::atan2(den_im, den_re);

	// Total phase of ns cascaded stages
	const double Phi    = (double)ns * phi1;
	const double cosPhi = std::cos(Phi);

	// Standard non-inv phaser magnitude formula:
	// |H|^2 = (1 + 2*(1-fb)*cosPhi + (1-fb)^2) / (1 - 2*fb*cosPhi + fb^2)
	const double f1       = 1.0 - fb;
	const double num_mag2 = 1.0 + 2.0 * f1 * cosPhi + f1 * f1;
	const double den_mag2 = 1.0 - 2.0 * fb * cosPhi + fb * fb;

	if (den_mag2 < 1.0e-12)
		return inv ? 1.0 : 0.0;

	const double mag    = std::sqrt(jmax(0.0, num_mag2 / den_mag2));
	const double dcGain = (f1 > 1.0e-6) ? (2.0 - fb) / f1 : 1.0e6;
	const double norm   = jlimit(0.0, 1.0, mag / dcGain);

	// Inv modes: show 1 - normalised non-inv value so notches become peaks
	// (baseline at bottom, peaks where normal mode has notches).
	if (inv)
		return jlimit(0.0, 1.0, 1.0 - norm);

	return norm;
}

PhaseAllpassSubType::InternalFilter::InternalFilter()
{
	reset();
}

void PhaseAllpassSubType::InternalFilter::reset()
{
	currentValue = 0.0f;

	for (int s = 0; s < 12; s++)
	{
		allpassFilters[s].delay        = 0.0f;
		allpassFilters[s].currentValue = 0.0f;
	}
}

float PhaseAllpassSubType::InternalFilter::getNextSample(float input)
{
	const float delayCoefficient = AllpassDelay::getDelayCoefficient(minDelay);

	for (int s = 0; s < numStages; s++)
		allpassFilters[s].setDelay(delayCoefficient);

	// currentValue holds pre-scaled feedback from the previous sample.
	float x = input + currentValue;

	for (int s = numStages - 1; s >= 0; s--)
		x = allpassFilters[s].getNextSample(x);

	const float rawFb = x * feedback;
	currentValue = fb_inv ? -rawFb : rawFb;

	return input + x;
}

DEFINE_MULTI_CHANNEL_FILTER(PhaseAllpassSubType);

hise::FilterHelpers::FilterSubType LadderSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::LadderSubType;
}

juce::Identifier LadderSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("ladder");
}

Array<hise::FilterHelpers::CoefficientType> LadderSubType::getCoefficientTypeList() const
{
	return { FilterHelpers::Ladder24db };
}

juce::StringArray LadderSubType::getModes() const
{
	return { "LP24" };
}

void LadderSubType::reset(int newNumChannels)
{
	memset(buf, 0, sizeof(float) * newNumChannels * 4);
}

void LadderSubType::setType(int /*t*/)
{

}

void LadderSubType::processSamples(AudioSampleBuffer& b, int startSample, int numSamples)
{
	for (int c = 0; c < b.getNumChannels(); c++)
	{
		for (int i = 0; i < numSamples; i++)
		{
			float* d = b.getWritePointer(c, i + startSample);
			*d = processSample(*d, c);
		}
	}
}

void LadderSubType::processFrame(float* d, int numChannels)
{
	for (int c = 0; c < numChannels; c++)
	{
		d[c] = processSample(d[c], c);
	}
}

void LadderSubType::updateCoefficients(double sampleRate, double frequency, double q, double /*gain*/)
{
	float inFreq = (float)FilterLimits::limitFrequency(frequency);

	const float x = 2.0f * float_Pi*inFreq / (float)sampleRate;

	cut = jlimit<float>(0.0f, 0.8f, x);
	res = jlimit<float>(0.3f, 4.0f, (float)q / 2.0f);
}

float LadderSubType::processSample(float input, int channel)
{
	float* buffer = buf[channel];

	float resoclip = buffer[3];

	const float in = input - (resoclip * res);
	buffer[0] = ((in - buffer[0]) * cut) + buffer[0];
	buffer[1] = ((buffer[0] - buffer[1]) * cut) + buffer[1];
	buffer[2] = ((buffer[1] - buffer[2]) * cut) + buffer[2];
	buffer[3] = ((buffer[2] - buffer[3]) * cut) + buffer[3];
	return 2.0f * buffer[3];
}

DEFINE_MULTI_CHANNEL_FILTER(LadderSubType);

hise::FilterHelpers::FilterSubType StateVariableFilterSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::StateVariableFilterSubType;
}

juce::Identifier StateVariableFilterSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("svf");
}

juce::StringArray StateVariableFilterSubType::getModes() const
{
	return { "LP", "HP", "BP", "Notch", "Allpass",
	         "LP 24dB", "LP 36dB", "LP 48dB",
	         "HP 24dB", "HP 36dB", "HP 48dB",
	         "BP 24dB", "Notch 24dB", "Morph" };
}

Array<hise::FilterHelpers::CoefficientType> StateVariableFilterSubType::getCoefficientTypeList() const
{
	return { FilterHelpers::LowPassReso, FilterHelpers::HighPass,
	         FilterHelpers::BandPass, FilterHelpers::Notch, FilterHelpers::AllPass,
	         FilterHelpers::LowPassReso, FilterHelpers::LowPassReso, FilterHelpers::LowPassReso,
	         FilterHelpers::HighPass, FilterHelpers::HighPass, FilterHelpers::HighPass,
	         FilterHelpers::BandPass, FilterHelpers::Notch, FilterHelpers::BandPass };
}

StateVariableFilterSubType::StateVariableFilterSubType()
{
	memset(v0z, 0, sizeof(float)*NUM_MAX_CHANNELS);
	memset(z1_A, 0, sizeof(float)*NUM_MAX_CHANNELS);
	memset(v2, 0, sizeof(float)*NUM_MAX_CHANNELS);
	memset(v0z_s, 0, sizeof(v0z_s));
	memset(z1_A_s, 0, sizeof(z1_A_s));
	memset(v2_s, 0, sizeof(v2_s));
}

void StateVariableFilterSubType::reset(int numChannels)
{
	memset(v0z, 0, sizeof(float)*numChannels);
	memset(z1_A, 0, sizeof(float)*numChannels);
	memset(v2, 0, sizeof(float)*numChannels);
	for (int s = 0; s < 3; ++s)
	{
		memset(v0z_s[s], 0, sizeof(float)*numChannels);
		memset(z1_A_s[s], 0, sizeof(float)*numChannels);
		memset(v2_s[s], 0, sizeof(float)*numChannels);
	}
}

void StateVariableFilterSubType::setType(int t)
{
	type = (FilterType)t;
}

// Helper: compute SVF stage coefficients for a given g (bilinear gain) and k (1/Q)
static void computeSVFStageCoeffs(float g, float k_b,
	float& g1_, float& g2_, float& g3_, float& g4_, float& k_out)
{
	k_out = k_b;
	const float ginv = g / (1.0f + g * (g + k_b));
	g1_ = ginv;
	g2_ = 2.0f * (g + k_b) * ginv;
	g3_ = g * ginv;
	g4_ = 2.0f * ginv;
}

// Butterworth k values (k = 1/Q) for secondary stages
// LP24/HP24: 1 extra stage (stage 2 Q = 1.3066)
static constexpr float kButterworth24[1] = { 1.0f / 1.3066f };
// LP36/HP36: 2 extra stages (Q = 0.7071, 1.9319)
static constexpr float kButterworth36[2] = { 1.0f / 0.7071f, 1.0f / 1.9319f };
// LP48/HP48: 3 extra stages (Q = 0.6013, 0.8999, 2.5629)
static constexpr float kButterworth48[3] = { 1.0f / 0.6013f, 1.0f / 0.8999f, 1.0f / 2.5629f };

void StateVariableFilterSubType::updateCoefficients(double sampleRate, double frequency, double q, double gain)
{
    outputGain = (type == Morph) ? 1.0f : (float)gain;
	storedSampleRate = sampleRate;
	storedFrequency  = frequency;
	storedUserQ      = (float)q;

	const float scaledQ = jlimit<float>(0.0f, 9.999f, (float)q * 0.1f);

	if (type == FilterType::ALLPASS)
	{
		// pre-warp the cutoff (for bilinear-transform filters)
		float wd = static_cast<float>(frequency * 2.0f * float_Pi);
		float T = 1.0f / (float)sampleRate;
		float wa = (2.0f / T) * tan(wd * T / 2.0f);

		// Calculate g (gain element of integrator)
		gCoeff = wa * T / 2.0f;

		// Calculate Zavalishin's R from Q (referred to as damping parameter)
		RCoeff = 1.0f / (2.0f * (float)q);

		x1 = (2.0f * RCoeff + gCoeff);
		x2 = 1.0f / (1.0f + (2.0f * RCoeff * gCoeff) + gCoeff * gCoeff);
	}
	else
	{
		float g = (float)tan(double_Pi * frequency / sampleRate);

		if (type == LP24 || type == HP24 ||
		    type == LP36 || type == HP36 ||
		    type == LP48 || type == HP48)
		{
			// Proportional Butterworth scaling: all stage k values scale together.
			// sf = raw user Q param; neutral at 1.0 → pure Butterworth on every stage.
			static constexpr float kBw1_24 = 1.0f / 0.5412f;
			static constexpr float kBw1_36 = 1.0f / 0.5176f;
			static constexpr float kBw1_48 = 1.0f / 0.5098f;

			const float rawSf = jmax(0.001f, (float)q);
			const float sf = (rawSf <= 1.0f) ? rawSf : 1.0f + (rawSf - 1.0f) * 0.5f;
			const float* kTable;
			int numExtra;
			if      (type == LP24 || type == HP24) { k = kBw1_24 / sf; kTable = kButterworth24; numExtra = 1; }
			else if (type == LP36 || type == HP36) { k = kBw1_36 / sf; kTable = kButterworth36; numExtra = 2; }
			else                                   { k = kBw1_48 / sf; kTable = kButterworth48; numExtra = 3; }

			float ginv = g / (1.0f + g * (g + k));
			g1 = ginv;
			g2 = 2.0f * (g + k) * ginv;
			g3 = g * ginv;
			g4 = 2.0f * ginv;

			for (int s = 0; s < numExtra; ++s)
				computeSVFStageCoeffs(g, kTable[s] / sf, g1_s[s], g2_s[s], g3_s[s], g4_s[s], k_s[s]);
		}
		else
		{
			k = 1.0f - 0.99f * scaledQ;
			float ginv = g / (1.0f + g * (g + k));
			g1 = ginv;
			g2 = 2.0f * (g + k) * ginv;
			g3 = g * ginv;
			g4 = 2.0f * ginv;

			if (type == BP24 || type == NOTCH24)
			{
				// Both stages use user Q — copy stage-1 coefficients
				g1_s[0] = g1; g2_s[0] = g2; g3_s[0] = g3; g4_s[0] = g4; k_s[0] = k;
			}
		}
	}
}

FilterCoefficientData StateVariableFilterSubType::getCoefficients(double freqNorm, double q, double gain) const
{
    if (type >= LP24 || type == Morph)
    {
        auto* m = const_cast<StateVariableFilterSubType*>(this);
        if (storedSampleRate > 0.0)
            m->storedFrequency = freqNorm * storedSampleRate;
        m->storedUserQ = (float)q;

        const double gain_dB = Decibels::gainToDecibels(jmax((float)gain, 1.0e-6f));
        m->morph = jlimit(0.0, 1.0, (gain_dB + 18.0) / 36.0);

        FilterCoefficientData cd;
        cd.first.coefficients[0] = Random::getSystemRandom().nextFloat();
        cd.customFunction = getPlotValueHighOrder;
        cd.obj = m;
        return cd;
    }
    return {};
}

double StateVariableFilterSubType::getPlotValueHighOrder(void* obj, bool getMagnitude, double freqNorm)
{
	auto* typed = static_cast<StateVariableFilterSubType*>(obj);
	const double sr = typed->storedSampleRate;
	const double fc = typed->storedFrequency;
	const double rawUserQ = (double)typed->storedUserQ;
	const double userQ = (rawUserQ <= 1.0) ? rawUserQ : 1.0 + (rawUserQ - 1.0) * 0.5;

	static const double q24[] = { 0.5412, 1.3066 };
	static const double q36[] = { 0.5176, 0.7071, 1.9319 };
	static const double q48[] = { 0.5098, 0.6013, 0.8999, 2.5629 };

	int numStages;
	const double* qTable;

    if (typed->type == Morph)
    {
        const double q = (double)typed->storedUserQ;
        const double stageQ = q;

        FilterCoefficientData lp_cd, hp_cd, bp_cd, notch_cd;
        lp_cd.second = hp_cd.second = bp_cd.second = notch_cd.second = 1;
        lp_cd.first    = IIRCoefficients::makeLowPass(sr, fc, stageQ);
        hp_cd.first    = IIRCoefficients::makeHighPass(sr, fc, stageQ);
        bp_cd.first    = IIRCoefficients::makeBandPass(sr, fc, stageQ);
        notch_cd.first = IIRCoefficients::makeNotchFilter(sr, fc, stageQ);

        const double lp_v    = FilterCoefficientData::getFilterPlotValueForIIRCoefficients(&lp_cd,    getMagnitude, freqNorm);
        const double hp_v    = FilterCoefficientData::getFilterPlotValueForIIRCoefficients(&hp_cd,    getMagnitude, freqNorm);
        const double bp_v    = FilterCoefficientData::getFilterPlotValueForIIRCoefficients(&bp_cd,    getMagnitude, freqNorm);
        const double notch_v = FilterCoefficientData::getFilterPlotValueForIIRCoefficients(&notch_cd, getMagnitude, freqNorm);

        const double mt = typed->morph * 3.0;
        if (mt <= 1.0)
            return lp_v * (1.0 - mt) + bp_v * mt;
        else if (mt <= 2.0)
            return bp_v * (2.0 - mt) + hp_v * (mt - 1.0);
        else
            return hp_v * (3.0 - mt) + notch_v * (mt - 2.0);
    }
    
	switch (typed->type)
	{
	case LP24: case HP24:
		numStages = 2; qTable = q24; break;
	case BP24: case NOTCH24:
		numStages = 2; qTable = nullptr; break;
	case LP36: case HP36:
		numStages = 3; qTable = q36; break;
	case LP48: case HP48:
		numStages = 4; qTable = q48; break;
	default:
		return getMagnitude ? 1.0 : 0.0;
	}

	double result = getMagnitude ? 1.0 : 0.0;

	for (int s = 0; s < numStages; ++s)
	{
		const double stageQ = (qTable != nullptr) ? qTable[s] * userQ : userQ;

 		FilterCoefficientData stageCd;
		stageCd.second = 1;
		switch (typed->type)
		{
		case LP24: case LP36: case LP48:
			stageCd.first = IIRCoefficients::makeLowPass(sr, fc, stageQ);  break;
		case HP24: case HP36: case HP48:
			stageCd.first = IIRCoefficients::makeHighPass(sr, fc, stageQ); break;
		case BP24:
			stageCd.first = IIRCoefficients::makeBandPass(sr, fc, stageQ); break;
		case NOTCH24:
			stageCd.first = IIRCoefficients::makeNotchFilter(sr, fc, stageQ); break;
		default: break;
		}

		const double v = FilterCoefficientData::getFilterPlotValueForIIRCoefficients(&stageCd, getMagnitude, freqNorm);
		if (getMagnitude) result *= v;
		else              result += v;
	}

	return result;
}

// ---------------------------------------------------------------------------
// Inline helpers for single-sample SVF stage processing
// ---------------------------------------------------------------------------

static inline void runLPStage(float in, float& out, int c,
	float* v0z_, float* z1A_, float* v2_,
	float g1_, float g2_, float g3_, float g4_)
{
	const float v1z = z1A_[c], v2z = v2_[c];
	const float v3 = in + v0z_[c] - 2.0f * v2z;
	z1A_[c] += g1_ * v3 - g2_ * v1z;
	v2_[c]  += g3_ * v3 + g4_ * v1z;
	v0z_[c]  = in;
	out = v2_[c];
}

static inline void runHPStage(float in, float& out, int c,
	float* v0z_, float* z1A_, float* v2_,
	float g1_, float g2_, float g3_, float g4_, float k_)
{
	const float v1z = z1A_[c], v2z = v2_[c];
	const float v3 = in + v0z_[c] - 2.0f * v2z;
	z1A_[c] += g1_ * v3 - g2_ * v1z;
	v2_[c]  += g3_ * v3 + g4_ * v1z;
	v0z_[c]  = in;
	out = in - k_ * z1A_[c] - v2_[c];
}

static inline void runBPStage(float in, float& out, int c,
	float* v0z_, float* z1A_, float* v2_,
	float g1_, float g2_, float g3_, float g4_)
{
	const float v1z = z1A_[c], v2z = v2_[c];
	const float v3 = in + v0z_[c] - 2.0f * v2z;
	z1A_[c] += g1_ * v3 - g2_ * v1z;
	v2_[c]  += g3_ * v3 + g4_ * v1z;
	v0z_[c]  = in;
	out = z1A_[c];
}

static inline void runNotchStage(float in, float& out, int c,
	float* v0z_, float* z1A_, float* v2_,
	float g1_, float g2_, float g3_, float g4_, float k_)
{
	const float v1z = z1A_[c], v2z = v2_[c];
	const float v3 = in + v0z_[c] - 2.0f * v2z;
	z1A_[c] += g1_ * v3 - g2_ * v1z;
	v2_[c]  += g3_ * v3 + g4_ * v1z;
	v0z_[c]  = in;
	out = in - k_ * z1A_[c];
}

static inline void runAPStage(float in, float& out, int c,
	float* z1A_, float* v2_,
	float gCoeff_, float x1_, float x2_, float RCoeff_)
{
	const float HP = (in - x1_ * z1A_[c] - v2_[c]) * x2_;
	const float BP = HP * gCoeff_ + z1A_[c];
	const float LP = BP * gCoeff_ + v2_[c];
	z1A_[c] = gCoeff_ * HP + BP;
	v2_[c]  = gCoeff_ * BP + LP;
	out = in - 4.0f * RCoeff_ * BP;
}

void StateVariableFilterSubType::processSamples(AudioSampleBuffer& buffer, int startSample, int numSamples)
{
	auto numChannels = buffer.getNumChannels();

	switch (type)
	{
	case LP:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);

			for (int i = 0; i < numSamples; i++)
			{
				float v0 = d[i];
				float v1z = z1_A[c];
				float v2z = v2[c];
				float v3 = v0 + v0z[c] - 2.0f * v2z;
				z1_A[c] += g1 * v3 - g2 * v1z;
				v2[c] += g3 * v3 + g4 * v1z;
				v0z[c] = v0;
				d[i] = v2[c] * outputGain;
			}
		}

		break;
	}
	case BP:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);

			for (int i = 0; i < numSamples; i++)
			{
				float v0 = d[i];
				float v1z = z1_A[c];
				float v2z = v2[c];
				float v3 = v0 + v0z[c] - 2.0f * v2z;
				z1_A[c] += g1 * v3 - g2 * v1z;
				v2[c] += g3 * v3 + g4 * v1z;
				v0z[c] = v0;
				d[i] = z1_A[c] * outputGain;
			}
		}

		break;
	}

	case HP:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);

			for (int i = 0; i < numSamples; i++)
			{
				float v0 = d[i];
				float v1z = z1_A[c];
				float v2z = v2[c];
				float v3 = v0 + v0z[c] - 2.0f * v2z;
				z1_A[c] += g1 * v3 - g2 * v1z;
				v2[c] += g3 * v3 + g4 * v1z;
				v0z[c] = v0;
				d[i] = (v0 - k * z1_A[c] - v2[c]) * outputGain;
			}
		}

		break;
	}
	case FilterType::ALLPASS:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);

			for (int i = 0; i < numSamples; ++i)
			{
				const float input = d[i];
				const float HP = (input - x1 * z1_A[c] - v2[c]) * x2;
				const float BP = HP * gCoeff + z1_A[c];
				const float LP = BP * gCoeff + v2[c];

				z1_A[c] = gCoeff * HP + BP;
				v2[c] = gCoeff * BP + LP;

				const float AP = input - (4.0f * RCoeff * BP);
				d[i] = AP * outputGain;
			}
		}
		break;
	}
	case NOTCH:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);

			for (int i = 0; i < numSamples; i++)
			{
				float v0 = d[i];
				float v1z = z1_A[c];
				float v2z = v2[c];
				float v3 = v0 + v0z[c] - 2.0f * v2z;
				z1_A[c] += g1 * v3 - g2 * v1z;
				v2[c] += g3 * v3 + g4 * v1z;
				v0z[c] = v0;
				d[i] = (v0 - k * z1_A[c]) * outputGain;
			}
		}

		break;
	}
	case LP24:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);
			for (int i = 0; i < numSamples; i++)
			{
				float s;
				runLPStage(d[i], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4);
				runLPStage(s,    d[i], c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0]);
				d[i] *= outputGain;
			}
		}
		break;
	}
	case LP36:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);
			for (int i = 0; i < numSamples; i++)
			{
				float s, s2;
				runLPStage(d[i], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4);
				runLPStage(s,    s2,   c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0]);
				runLPStage(s2,   d[i], c, v0z_s[1], z1_A_s[1], v2_s[1], g1_s[1], g2_s[1], g3_s[1], g4_s[1]);
				d[i] *= outputGain;
			}
		}
		break;
	}
	case LP48:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);
			for (int i = 0; i < numSamples; i++)
			{
				float s, s2, s3;
				runLPStage(d[i], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4);
				runLPStage(s,    s2,   c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0]);
				runLPStage(s2,   s3,   c, v0z_s[1], z1_A_s[1], v2_s[1], g1_s[1], g2_s[1], g3_s[1], g4_s[1]);
				runLPStage(s3,   d[i], c, v0z_s[2], z1_A_s[2], v2_s[2], g1_s[2], g2_s[2], g3_s[2], g4_s[2]);
				d[i] *= outputGain;
			}
		}
		break;
	}
	case HP24:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);
			for (int i = 0; i < numSamples; i++)
			{
				float s;
				runHPStage(d[i], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4,      k);
				runHPStage(s,    d[i], c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0], k_s[0]);
				d[i] *= outputGain;
			}
		}
		break;
	}
	case HP36:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);
			for (int i = 0; i < numSamples; i++)
			{
				float s, s2;
				runHPStage(d[i], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4,      k);
				runHPStage(s,    s2,   c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0], k_s[0]);
				runHPStage(s2,   d[i], c, v0z_s[1], z1_A_s[1], v2_s[1], g1_s[1], g2_s[1], g3_s[1], g4_s[1], k_s[1]);
				d[i] *= outputGain;
			}
		}
		break;
	}
	case HP48:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);
			for (int i = 0; i < numSamples; i++)
			{
				float s, s2, s3;
				runHPStage(d[i], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4,      k);
				runHPStage(s,    s2,   c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0], k_s[0]);
				runHPStage(s2,   s3,   c, v0z_s[1], z1_A_s[1], v2_s[1], g1_s[1], g2_s[1], g3_s[1], g4_s[1], k_s[1]);
				runHPStage(s3,   d[i], c, v0z_s[2], z1_A_s[2], v2_s[2], g1_s[2], g2_s[2], g3_s[2], g4_s[2], k_s[2]);
				d[i] *= outputGain;
			}
		}
		break;
	}
	case BP24:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);
			for (int i = 0; i < numSamples; i++)
			{
				float s;
				runBPStage(d[i], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4);
				runBPStage(s,    d[i], c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0]);
				d[i] *= outputGain;
			}
		}
		break;
	}
	case NOTCH24:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float* d = buffer.getWritePointer(c, startSample);
			for (int i = 0; i < numSamples; i++)
			{
				float s;
				runNotchStage(d[i], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4,      k);
				runNotchStage(s,    d[i], c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0], k_s[0]);
				d[i] *= outputGain;
			}
		}
		break;
	}
            
        case Morph:
        {
            for (int c = 0; c < numChannels; c++)
            {
                float* d = buffer.getWritePointer(c, startSample);
                for (int i = 0; i < numSamples; i++)
                {
                    const float v0  = d[i];
                    const float v1z = z1_A[c];
                    const float v2z = v2[c];
                    const float v3  = v0 + v0z[c] - 2.0f * v2z;
                    z1_A[c] += g1 * v3 - g2 * v1z;
                    v2[c]   += g3 * v3 + g4 * v1z;
                    v0z[c]   = v0;

                    const float lp   = v2[c];
                    const float bp   = z1_A[c];
                    const float hp   = v0 - k * z1_A[c] - v2[c];
                    const float notch = v0 - k * z1_A[c];

                    const float mt = (float)(morph * 3.0);
                    float out;
                    if (mt <= 1.0f)
                        out = lp * (1.0f - mt) + bp * mt;
                    else if (mt <= 2.0f)
                        out = bp * (2.0f - mt) + hp * (mt - 1.0f);
                    else
                        out = hp * (3.0f - mt) + notch * (mt - 2.0f);

                    d[i] = out * outputGain;
                }
            }
            break;
        }
            
	default:
		jassertfalse;
		break;
	}
}

void StateVariableFilterSubType::processFrame(float* d, int numChannels)
{
	switch (type)
	{
	case LP:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float v0 = d[c];
			float v1z = z1_A[c];
			float v2z = v2[c];
			float v3 = v0 + v0z[c] - 2.0f * v2z;
			z1_A[c] += g1 * v3 - g2 * v1z;
			v2[c] += g3 * v3 + g4 * v1z;
			v0z[c] = v0;
			d[c] = v2[c] * outputGain;
		}

		break;
	}
	case BP:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float v0 = d[c];
			float v1z = z1_A[c];
			float v2z = v2[c];
			float v3 = v0 + v0z[c] - 2.0f * v2z;
			z1_A[c] += g1 * v3 - g2 * v1z;
			v2[c] += g3 * v3 + g4 * v1z;
			v0z[c] = v0;
			d[c] = z1_A[c] * outputGain;
		}

		break;
	}

	case HP:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float v0 = d[c];
			float v1z = z1_A[c];
			float v2z = v2[c];
			float v3 = v0 + v0z[c] - 2.0f * v2z;
			z1_A[c] += g1 * v3 - g2 * v1z;
			v2[c] += g3 * v3 + g4 * v1z;
			v0z[c] = v0;
			d[c] = (v0 - k * z1_A[c] - v2[c]) * outputGain;
		}

		break;
	}
	case FilterType::ALLPASS:
	{
		for (int c = 0; c < numChannels; c++)
		{
			const float input = d[c];
			const float HP = (input - x1 * z1_A[c] - v2[c]) * x2;
			const float BP = HP * gCoeff + z1_A[c];
			const float LP = BP * gCoeff + v2[c];

			z1_A[c] = gCoeff * HP + BP;
			v2[c] = gCoeff * BP + LP;

			const float AP = input - (4.0f * RCoeff * BP);
			d[c] = AP * outputGain;
		}
		break;
	}

	case NOTCH:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float v0 = d[c];
			float v1z = z1_A[c];
			float v2z = v2[c];
			float v3 = v0 + v0z[c] - 2.0f * v2z;
			z1_A[c] += g1 * v3 - g2 * v1z;
			v2[c] += g3 * v3 + g4 * v1z;
			v0z[c] = v0;
			d[c] = (v0 - k * z1_A[c]) * outputGain;
		}

		break;
	}
	case LP24:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float s;
			runLPStage(d[c], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4);
			runLPStage(s,    d[c], c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0]);
			d[c] *= outputGain;
		}
		break;
	}
	case LP36:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float s, s2;
			runLPStage(d[c], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4);
			runLPStage(s,    s2,   c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0]);
			runLPStage(s2,   d[c], c, v0z_s[1], z1_A_s[1], v2_s[1], g1_s[1], g2_s[1], g3_s[1], g4_s[1]);
			d[c] *= outputGain;
		}
		break;
	}
	case LP48:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float s, s2, s3;
			runLPStage(d[c], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4);
			runLPStage(s,    s2,   c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0]);
			runLPStage(s2,   s3,   c, v0z_s[1], z1_A_s[1], v2_s[1], g1_s[1], g2_s[1], g3_s[1], g4_s[1]);
			runLPStage(s3,   d[c], c, v0z_s[2], z1_A_s[2], v2_s[2], g1_s[2], g2_s[2], g3_s[2], g4_s[2]);
			d[c] *= outputGain;
		}
		break;
	}
	case HP24:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float s;
			runHPStage(d[c], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4,      k);
			runHPStage(s,    d[c], c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0], k_s[0]);
			d[c] *= outputGain;
		}
		break;
	}
	case HP36:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float s, s2;
			runHPStage(d[c], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4,      k);
			runHPStage(s,    s2,   c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0], k_s[0]);
			runHPStage(s2,   d[c], c, v0z_s[1], z1_A_s[1], v2_s[1], g1_s[1], g2_s[1], g3_s[1], g4_s[1], k_s[1]);
			d[c] *= outputGain;
		}
		break;
	}
	case HP48:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float s, s2, s3;
			runHPStage(d[c], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4,      k);
			runHPStage(s,    s2,   c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0], k_s[0]);
			runHPStage(s2,   s3,   c, v0z_s[1], z1_A_s[1], v2_s[1], g1_s[1], g2_s[1], g3_s[1], g4_s[1], k_s[1]);
			runHPStage(s3,   d[c], c, v0z_s[2], z1_A_s[2], v2_s[2], g1_s[2], g2_s[2], g3_s[2], g4_s[2], k_s[2]);
			d[c] *= outputGain;
		}
		break;
	}
	case BP24:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float s;
			runBPStage(d[c], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4);
			runBPStage(s,    d[c], c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0]);
			d[c] *= outputGain;
		}
		break;
	}
	case NOTCH24:
	{
		for (int c = 0; c < numChannels; c++)
		{
			float s;
			runNotchStage(d[c], s,    c, v0z,      z1_A,      v2,      g1,      g2,      g3,      g4,      k);
			runNotchStage(s,    d[c], c, v0z_s[0], z1_A_s[0], v2_s[0], g1_s[0], g2_s[0], g3_s[0], g4_s[0], k_s[0]);
			d[c] *= outputGain;
		}
		break;
	}
            
        case Morph:
        {
            for (int c = 0; c < numChannels; c++)
            {
                const float v0  = d[c];
                const float v1z = z1_A[c];
                const float v2z = v2[c];
                const float v3  = v0 + v0z[c] - 2.0f * v2z;
                z1_A[c] += g1 * v3 - g2 * v1z;
                v2[c]   += g3 * v3 + g4 * v1z;
                v0z[c]   = v0;

                const float lp    = v2[c];
                const float bp    = z1_A[c];
                const float hp    = v0 - k * z1_A[c] - v2[c];
                const float notch = v0 - k * z1_A[c];

                const float mt = (float)(morph * 3.0);
                float out;
                if (mt <= 1.0f)
                    out = lp * (1.0f - mt) + bp * mt;
                else if (mt <= 2.0f)
                    out = bp * (2.0f - mt) + hp * (mt - 1.0f);
                else
                    out = hp * (3.0f - mt) + notch * (mt - 2.0f);

                d[c] = out * outputGain;
            }
            break;
        }
            
	default:
		jassertfalse;
		break;
	}
}

DEFINE_MULTI_CHANNEL_FILTER(StateVariableFilterSubType);

hise::FilterHelpers::FilterSubType LinkwitzRiley::getFilterType()
{
	return FilterHelpers::FilterSubType::LinkwitzRiley;
}

juce::Identifier LinkwitzRiley::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("linkwitzriley");
}

juce::StringArray LinkwitzRiley::getModes() const
{
	return { "LP", "HP", "AP" };
}

Array<hise::FilterHelpers::CoefficientType> LinkwitzRiley::getCoefficientTypeList() const
{

	return { FilterHelpers::LowPass, FilterHelpers::HighPass, FilterHelpers::AllPass };
}

void LinkwitzRiley::setType(int type)
{
	mode = type;
}

void LinkwitzRiley::reset(int numChannels)
{
	memset(hpData, 0, sizeof(Data)*numChannels);
	memset(lpData, 0, sizeof(Data)*numChannels);
}

void LinkwitzRiley::processSamples(AudioSampleBuffer& buffer, int startSample, int)
{
	for (int c = 0; c < buffer.getNumChannels(); c++)
	{
		auto ptr = buffer.getWritePointer(c + startSample);

		for (int i = 0; i < buffer.getNumSamples(); i++)
		{
			ptr[i] = process(ptr[i], c);
		}
	}
}

void LinkwitzRiley::processFrame(float* frameData, int numChannels)
{
	for (int i = 0; i < numChannels; i++)
	{
		frameData[i] = process(frameData[i], i);
	}
}

void LinkwitzRiley::updateCoefficients(double sampleRate, double frequency, double /*q*/, double /*gain*/)
{


	const double pi = double_Pi;
	const double cowc = 2.0 * pi*frequency;
	const double cowc2 = cowc * cowc;
	const double cowc3 = cowc2 * cowc;
	const double cowc4 = cowc2 * cowc2;
	const double cok = cowc / std::tan(pi*frequency / sampleRate);
	const double cok2 = cok * cok;
	const double cok3 = cok2 * cok;
	const double cok4 = cok2 * cok2;
	const double sqrt2 = std::sqrt(2.0);
	const double sq_tmp1 = sqrt2 * cowc3 * cok;
	const double sq_tmp2 = sqrt2 * cowc * cok3;
	const double a_tmp = 4.0 * cowc2*cok2 + 2.0 * sq_tmp1 + cok4 + 2.0 * sq_tmp2 + cowc4;

	SpinLock::ScopedLockType sl(lock);

	b1co = (4.0 * (cowc4 + sq_tmp1 - cok4 - sq_tmp2)) / a_tmp;
	b2co = (6.0 * cowc4 - 8.0 * cowc2*cok2 + 6.0 * cok4) / a_tmp;
	b3co = (4.0 * (cowc4 - sq_tmp1 + sq_tmp2 - cok4)) / a_tmp;
	b4co = (cok4 - 2.0 * sq_tmp1 + cowc4 - 2.0 * sq_tmp2 + 4.0 * cowc2*cok2) / a_tmp;

	//================================================
	// low-pass
	//================================================
	lpco.coefficients[0] = cowc4 / a_tmp;
	lpco.coefficients[1] = 4.0 * cowc4 / a_tmp;
	lpco.coefficients[2] = 6.0 * cowc4 / a_tmp;
	lpco.coefficients[3] = lpco.coefficients[1];
	lpco.coefficients[4] = lpco.coefficients[0];

	//=====================================================
	// high-pass
	//=====================================================
	hpco.coefficients[0] = cok4 / a_tmp;
	hpco.coefficients[1] = -4.0 * cok4 / a_tmp;
	hpco.coefficients[2] = 6.0 * cok4 / a_tmp;
	hpco.coefficients[3] = hpco.coefficients[1];
	hpco.coefficients[4] = hpco.coefficients[0];
}

float LinkwitzRiley::process(float input, int channel)
{
	SpinLock::ScopedLockType sl(lock);

	double tempx, hp, lp;
	tempx = (double)input;

	auto& hptemp = hpData[channel];
	auto& lptemp = lpData[channel];

	// High pass
	hp = hpco.coefficients[0] * tempx +
		hpco.coefficients[1] * hptemp.xm1 +
		hpco.coefficients[2] * hptemp.xm2 +
		hpco.coefficients[3] * hptemp.xm3 +
		hpco.coefficients[4] * hptemp.xm4 -
		b1co * hptemp.ym1 -
		b2co * hptemp.ym2 -
		b3co * hptemp.ym3 -
		b4co * hptemp.ym4;

	hptemp.xm4 = hptemp.xm3;
	hptemp.xm3 = hptemp.xm2;
	hptemp.xm2 = hptemp.xm1;
	hptemp.xm1 = tempx;
	hptemp.ym4 = hptemp.ym3;
	hptemp.ym3 = hptemp.ym2;
	hptemp.ym2 = hptemp.ym1;
	hptemp.ym1 = hp;

	// Low pass

	lp =
		lpco.coefficients[0] * tempx +
		lpco.coefficients[1] * lptemp.xm1 +
		lpco.coefficients[2] * lptemp.xm2 +
		lpco.coefficients[3] * lptemp.xm3 +
		lpco.coefficients[4] * lptemp.xm4 -
		b1co * lptemp.ym1 -
		b2co * lptemp.ym2 -
		b3co * lptemp.ym3 -
		b4co * lptemp.ym4;

	lptemp.xm4 = lptemp.xm3; // these are the same as hptemp and could be optimised away
	lptemp.xm3 = lptemp.xm2;
	lptemp.xm2 = lptemp.xm1;
	lptemp.xm1 = tempx;
	lptemp.ym4 = lptemp.ym3;
	lptemp.ym3 = lptemp.ym2;
	lptemp.ym2 = lptemp.ym1;
	lptemp.ym1 = lp;

	switch (mode)
	{
	case LP: return static_cast<float>(lp);
	case HP: return static_cast<float>(hp);
	case Allpass: return static_cast<float>(lp + hp);
	default: return 0.0f;
	}
}

DEFINE_MULTI_CHANNEL_FILTER(LinkwitzRiley);

PhaseAllpassSubType::InternalFilter::AllpassDelay::AllpassDelay() :
	delay(0.f),
	currentValue(0.f)
{

}

float PhaseAllpassSubType::InternalFilter::AllpassDelay::getDelayCoefficient(float delaySamples)
{
	return (1.f - delaySamples) / (1.f + delaySamples);
}

void PhaseAllpassSubType::InternalFilter::AllpassDelay::setDelay(float newDelay) noexcept
{
	delay = newDelay;
}

float PhaseAllpassSubType::InternalFilter::AllpassDelay::getNextSample(float input) noexcept
{
	float y = input * -delay + currentValue;
	currentValue = y * delay + input;

	return y;
}

juce::Identifier StateVariableEqSubType::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("svf_eq");
}

hise::FilterHelpers::FilterSubType StateVariableEqSubType::getFilterType()
{
	return FilterHelpers::FilterSubType::StateVariableEqSubType;
}

juce::Array<hise::FilterHelpers::CoefficientType> StateVariableEqSubType::getCoefficientTypeList() const
{
	return
	{
		FilterHelpers::LowPassReso,
		FilterHelpers::HighPass,
		FilterHelpers::LowShelf,
		FilterHelpers::HighShelf,
		FilterHelpers::Peak,
	};
}

juce::StringArray StateVariableEqSubType::getModes() const
{
	return
	{
		"LowPass",
		"HighPass",
		"LowShelf",
		"HighShelf",
		"Peak"
	};
}

void StateVariableEqSubType::reset(int newNumChannels)
{
	for (int i = 0; i < newNumChannels; i++)
		states[i].reset();
}

void StateVariableEqSubType::setType(int newType)
{
	t = (Mode)newType;
}

void StateVariableEqSubType::processSamples(AudioSampleBuffer& b, int startSample, int numSamples)
{
	auto numChannels = b.getNumChannels();
	auto ptrs = b.getArrayOfWritePointers();

	for (int i = startSample; i < startSample + numSamples; i++)
	{
		coefficients.tick();

		for (int c = 0; c < numChannels; c++)
		{
			auto v = states[c].tick(ptrs[c][i], coefficients);
			ptrs[c][i] = v;
		}
	}
}

void StateVariableEqSubType::processFrame(float* frameData, int numChannels)
{
	coefficients.tick();

	for (int c = 0; c < numChannels; c++)
	{
		frameData[c] = states[c].tick(frameData[c], coefficients);
	}
}

void StateVariableEqSubType::updateCoefficients(double sampleRate, double frequency, double q, double gain)
{
	coefficients.setGain(Decibels::gainToDecibels(gain));
	coefficients.update(frequency, q, t, sampleRate);
}

StateVariableEqSubType::Coefficients::Coefficients()
{
	memset(a, 0, sizeof(double) * 4);
	memset(ap, 0, sizeof(double) * 4);
	memset(m, 0, sizeof(double) * 4);
	memset(mp, 0, sizeof(double) * 4);
	gain = gain_sqrt = 1;
}

void StateVariableEqSubType::Coefficients::update(double freq, double q, StateVariableEqSubType::Mode type, double sampleRate)
{
	double g = std::tan((freq / sampleRate) * double_Pi);
	double k = computeK(q, type == Peak);

	switch (type) {
	case LowPass:
		m[0] = 0.0;
		m[1] = 0.0;
		m[2] = 1.0;
		break;
	case HighPass:
		m[0] = 1.0;
		m[1] = -k;
		m[2] = -1.0;
		break;
	case Peak:
		m[0] = 1.0;
		m[1] = k * (gain*gain - 1.0);
		m[2] = 0.0;
		break;
	case LowShelf:
		g /= gain_sqrt;
		m[0] = 1.0;
		m[1] = k * (gain - 1.0);
		m[2] = gain * gain - 1.0;
		break;
	case HighShelf:
		g *= gain_sqrt;
		m[0] = gain * gain;
		m[1] = k * (1.0 - gain)*gain;
		m[2] = 1.0 - gain * gain;
		break;
	case numModes:
	default:
		jassertfalse;
	}

	computeA(g, k);
}

void StateVariableEqSubType::Coefficients::setGain(double gainDb)
{
	gain = std::pow(10.0, gainDb / 40.0);
	gain_sqrt = std::sqrt(gain);
}

double StateVariableEqSubType::Coefficients::computeK(double q, bool useGain /*= false*/)
{
	return 1.f / (useGain ? (q*gain) : q);
}

void StateVariableEqSubType::Coefficients::computeA(double g, double k)
{
	a[0] = 1 / (1 + g * (g + k));
	a[1] = g * a[0];
	a[2] = g * a[1];
}

void StateVariableEqSubType::Coefficients::tick()
{
	const double gain = 0.99;
	const double invGain = 1.0 - gain;

#if 1 || HI_ENABLE_LEGACY_CPU_SUPPORT || !JUCE_WINDOWS

	mp[0] *= gain;
	mp[1] *= gain;
	mp[2] *= gain;

	mp[0] += m[0] * invGain;
	mp[1] += m[1] * invGain;
	mp[2] += m[2] * invGain;

	ap[0] *= gain;
	ap[1] *= gain;
	ap[2] *= gain;

	ap[0] += a[0] * invGain;
	ap[1] += a[1] * invGain;
	ap[2] += a[2] * invGain;

#else
	const auto gain_ = _mm256_set1_pd(gain);
	const auto invGain_ = _mm256_set1_pd(invGain);

	auto _mp_ = _mm256_load_pd(mp);
	auto _m_ = _mm256_load_pd(m);
	auto _ap_ = _mm256_load_pd(ap);
	auto _a_ = _mm256_load_pd(a);

	_mp_ = _mm256_mul_pd(_mp_, gain_);
	_m_ = _mm256_mul_pd(_m_, invGain_);
	_mp_ = _mm256_add_pd(_mp_, _m_);
	_mm256_store_pd(mp, _mp_);

	_ap_ = _mm256_mul_pd(_ap_, gain_);
	_a_ = _mm256_mul_pd(_a_, invGain_);
	_ap_ = _mm256_add_pd(_ap_, _a_);
	_mm256_store_pd(ap, _ap_);
#endif
}

StateVariableEqSubType::State::State()
{
	reset();
}

void StateVariableEqSubType::State::reset()
{
	memset(v, 0, sizeof(double) * 4);
	_ic1eq = _ic2eq = 0.0;
}

float StateVariableEqSubType::State::tick(float inp, const Coefficients& c)
{
	v[0] = (double)inp;

	v[3] = v[0] - _ic2eq;
	v[1] = c.ap[0] * _ic1eq + c.ap[1] * v[3];
	v[2] = _ic2eq + c.ap[1] * _ic1eq + c.ap[2] * v[3];
	_ic1eq = 2 * v[1] - _ic1eq;
	_ic2eq = 2 * v[2] - _ic2eq;

	return (float)(c.mp[0] * v[0] + c.mp[1] * v[1] + c.mp[2] * v[2]);
}

DEFINE_MULTI_CHANNEL_FILTER(StateVariableEqSubType);


double FilterHelpers::RenderData::applyModValue(double f) const
{
	bool calcModulation = !HISE_LOG_FILTER_FREQMOD || ((1.0 + bipolarDelta) * freqModValue != 1.0);

	if (!calcModulation)
		return f;

	f -= 20.0;
	f *= 1.0 / 19980.0;

#if HISE_LOG_FILTER_FREQMOD
	const double skew = 0.2299045622348785;
	f = hmath::pow(f, skew);
#endif

	f += bipolarDelta;
	f *= freqModValue;

#if HISE_LOG_FILTER_FREQMOD
	f = hmath::pow(jmax(0.0, f), 1.0 / skew);
#endif

	f *= 19980.0;
	f += 20.0;

	return f;
}

}
