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

namespace scriptnode {
namespace dynamics {

using namespace hise;
using namespace juce;



void ott::RMSDetector::prepare(PrepareSpecs ps)
{
	sampleRate = ps.sampleRate;
	updateCoefficients();
	reset();
}

void ott::RMSDetector::processFrame(span<float, 1>& peak)
{
	if (!enabled)
		return;

	auto x = (double)peak[0];
	x *= x;

	// asymmetric smoothing: the rise coefficient sets the detector attack, the fall
	// side always averages over the full window
	auto c = (x > meanSquare) ? riseCoeff : fallCoeff;
	meanSquare = c * (meanSquare - x) + x;

	peak[0] = (float)hmath::sqrt(jmax(0.0, meanSquare));
}

void ott::RMSDetector::setAmount(double v)
{
	amount = jlimit(0.0, 1.0, v);
	enabled = amount > 0.0;
	updateCoefficients();
}

void ott::RMSDetector::updateCoefficients()
{
	if (sampleRate <= 0.0 || !enabled)
		return;

	// detector-attack mapping: the fall side always averages over the full window
	// (peak-hold-like, so the tight end stays low-ripple even on bass), while the
	// amount only scales the rise time - the knob controls how much of a transient
	// leaks through before the compressor clamps it
	auto riseMs = jmax(0.05, amount * MaxWindowMs);

	riseCoeff = exp(log(0.01) / (riseMs * sampleRate * 0.001));
	fallCoeff = exp(log(0.01) / (MaxWindowMs * sampleRate * 0.001));
}

void ott::RMSDetector::reset()
{
	meanSquare = 0.0;
}

void ott::updown_envelope_follower::prepare(PrepareSpecs ps)
{
	sampleRate = ps.sampleRate;
	updateCoefficients();
}

void ott::updown_envelope_follower::reset()
{
	lastValue = 0.0f;
}

void ott::updown_envelope_follower::processFrame(span<float, 1>& input)
{
	double inputDouble = (double)input[0];

	auto isHigher = (inputDouble > lastValue);
	auto coeffToUse = isHigher ? atk : rel;

	// always smooth with atk/rel; the old below-threshold smoothing-bypass set the
	// coefficient to 0 (instant per-sample tracking) which modulated the gain at
	// audio rate -> aliasing/distortion. Removed 2026-06-09.

	lastValue = coeffToUse * (lastValue - inputDouble) + inputDouble;

	input[0] = (float)lastValue;
}

void ott::updown_envelope_follower::setLowThreshold(double v)
{
	lo_t = v;
}

void ott::updown_envelope_follower::setAttack(double attack_)
{
	attack = attack_;
	updateCoefficients();
}

void ott::updown_envelope_follower::setRelease(double release_)
{
	release = release_;
	updateCoefficients();
}

void ott::updown_envelope_follower::updateCoefficients()
{
	if (sampleRate > 0.0)
	{
		atk = exp(log(0.01) / (attack * sampleRate * 0.001));
		rel = exp(log(0.01) / (release * sampleRate * 0.001));
	}
}

void ott::prepare(PrepareSpecs specs)
{
	display_buffer_base<true>::prepare(specs);

	for (auto& s : state)
		s.prepare(specs.sampleRate, 50.0);

	rmsDetector.prepare(specs);
	envelopeFollower.prepare(specs);
}

void ott::reset()
{
	rmsDetector.reset();
	envelopeFollower.reset();

	for (auto& s : state)
		s.reset();
}

float ott::getGainReduction(float input)
{
	

	const auto lo_t = state[(int)Parameters::LowThreshold].advance();
	const auto hi_t = state[(int)Parameters::HighThreshold].advance();

	auto k = state[(int)Parameters::Knee].advance();

	const auto lo_w = jmin((hi_t - lo_t) * 0.5f, k);
	const auto hi_w = jmin((hi_t - lo_t) * 0.5f, k);

	const auto hi_r = state[(int)Parameters::HighRatio].advance();
	const auto lo_r = state[(int)Parameters::LowRatio].advance();

	auto hi_r_mapped = 1.0f + hi_r * 19.0f;

	auto depth = state[(int)Parameters::Depth].advance();
	auto hi_r_d = 1.0f + (hi_r_mapped - 1.0f) * depth;
    auto lo_r_d = lo_r + (1.0f - lo_r) * (1.0f - depth);

    if (hmath::abs(input - lo_t) < (lo_w * 0.5f))
    {
        auto blend = (input - (lo_t - lo_w * 0.5f)) / lo_w;
        auto v_compressed = input + (lo_t - input) * (1.0f - lo_r_d);
        return input + (v_compressed - input) * (1.0f - blend);
    }
    else if (input < (lo_t - lo_w*0.5f))
    {
        const auto noise_floor = Decibels::decibelsToGain(-120.0f);

        auto v = input + (lo_t - input) * (1.0f - lo_r_d);

        if (input < noise_floor)
        {
            auto alpha = input / noise_floor;
            auto invAlpha = 1.0f - alpha;
            return input * invAlpha + v * alpha;
        }
        else
            return v;
    }
	else if (hmath::abs(input - hi_t) < (hi_w / 2)) // apply soft knee
	{
		return input + (1.0f / hi_r_d - 1.0f) * hmath::sqr(input - hi_t + hi_w * 0.5f) / (2.0f * hi_w);
	}
	else if (input > (hi_t + hi_w*0.5f))
	{
		return jmin(1.0f, hi_t + (input - hi_t) / hi_r_d);
	}
	else
	{
		return input;
	}
}

void ott::createParameters(ParameterDataList& data)
{
	{
		parameter::data p("LowThreshold", { -100.0, 0.0 });
		registerCallback<(int)Parameters::LowThreshold>(p);
		p.setSkewForCentre(-18.0);
		p.info.textConverter = parameter::pod::Decibel;
		p.setDefaultValue(-100.0);
		data.add(std::move(p));
	}
	{
        parameter::data p("LowRatio", { 0.0, 1.0 });
        registerCallback<(int)Parameters::LowRatio>(p);
        p.setSkewForCentre(0.5);
        p.setDefaultValue(1.0);
        data.add(std::move(p));
	}
	{
		// Create a parameter like this
		parameter::data p("HighThreshold", { -100.0, 0.0 });
		registerCallback<(int)Parameters::HighThreshold>(p);
		p.setSkewForCentre(-6.0);
		p.info.textConverter = parameter::pod::Decibel;
		p.setDefaultValue(0.0);
		data.add(std::move(p));
	}
	{
		parameter::data p("HighRatio", { 0.0, 1.0 });
		registerCallback<(int)Parameters::HighRatio>(p);
		p.setSkewForCentre(0.3);
		p.setDefaultValue(0.0);
		data.add(std::move(p));
	}
	{
		parameter::data p("Knee", { 0.0, 0.3 });
		registerCallback<(int)Parameters::Knee>(p);
		p.setSkewForCentre(0.15);
		p.setDefaultValue(0.1);
		data.add(std::move(p));
	}
	{
		// Create a parameter like this
		parameter::data p("Attack", { 0.0, 1000.0 });
		registerCallback<(int)Parameters::Attack>(p);
		p.info.textConverter = parameter::pod::Time;
		p.setSkewForCentre(50.0);
		p.setDefaultValue(50.0);
		data.add(std::move(p));
	}
	{
		// Create a parameter like this
		parameter::data p("Release", { 0.0, 1000.0 });
		registerCallback<(int)Parameters::Release>(p);
		p.info.textConverter = parameter::pod::Time;
		p.setSkewForCentre(50.0);
		p.setDefaultValue(50.0);
		data.add(std::move(p));
	}
	{
		parameter::data p("RMS", { 0.0, 1.0 });
		registerCallback<(int)Parameters::RMS>(p);
		p.setDefaultValue(1.0);
		data.add(std::move(p));
	}
	{
		parameter::data p("Depth", { 0.0, 1.0 });
		registerCallback<(int)Parameters::Depth>(p);
		p.setDefaultValue(1.0);
		data.add(std::move(p));
	}
}

void ott::calculateGraph(block values)
{
	for(auto& v: values)
	{
		v = getGainReduction(v);
	}
}

// ============================================================================
// updown_comp — original pre-OTT behaviour
// ============================================================================

void updown_comp::prepare(PrepareSpecs specs)
{
	display_buffer_base<true>::prepare(specs);

	for (auto& s : state)
		s.prepare(specs.sampleRate, 50.0);

	rmsDetector.prepare(specs);
	envelopeFollower.prepare(specs);
}

void updown_comp::reset()
{
	rmsDetector.reset();
	envelopeFollower.reset();

	for (auto& s : state)
		s.reset();
}

float updown_comp::getGainReduction(float input)
{
	const auto lo_t = state[(int)Parameters::LowThreshold].advance();
	const auto hi_t = state[(int)Parameters::HighThreshold].advance();

	auto k = state[(int)Parameters::Knee].advance();

	const auto lo_w = jmin((hi_t - lo_t) * 0.5f, k);
	const auto hi_w = jmin((hi_t - lo_t) * 0.5f, k);

	const auto hi_r = state[(int)Parameters::HighRatio].advance();
	const auto lo_r = state[(int)Parameters::LowRatio].advance();

	if (hmath::abs(input - lo_t) < (lo_w * 0.5f))
	{
		return input - (1.0f / lo_r - 1.0f) * hmath::sqr(input - lo_t - lo_w * 0.5f) / (2.0f * lo_w);
	}
	else if (input < (lo_t - lo_w*0.5f))
	{
		auto v = jmax(0.0f, lo_t + (input - lo_t) / lo_r);

		const auto noise_floor = Decibels::decibelsToGain(-82.0f);

		if (input < noise_floor)
		{
			auto alpha = input / noise_floor;
			auto invAlpha = 1.0f - alpha;

			return input * invAlpha + v * alpha;
		}
		else
			return v;
	}
	else if (hmath::abs(input - hi_t) < (hi_w / 2)) // apply soft knee
	{
		return input + (1.0f / hi_r - 1.0f) * hmath::sqr(input - hi_t + hi_w * 0.5f) / (2.0f * hi_w);
	}
	else if (input > (hi_t + hi_w*0.5f))
	{
		return jmin(1.0f, hi_t + (input - hi_t) / hi_r);
	}
	else
	{
		return input;
	}
}

void updown_comp::createParameters(ParameterDataList& data)
{
	{
		parameter::data p("LowThreshold", { -100.0, 0.0 });
		registerCallback<(int)Parameters::LowThreshold>(p);
		p.setSkewForCentre(-18.0);
		p.info.textConverter = parameter::pod::Decibel;
		p.setDefaultValue(-100.0);
		data.add(std::move(p));
	}
	{
		parameter::data p("LowRatio", { 0.2, 100.0 });
		registerCallback<(int)Parameters::LowRatio>(p);
		p.setSkewForCentre(1.0);
		p.setDefaultValue(1.0);
		data.add(std::move(p));
	}
	{
		parameter::data p("HighThreshold", { -100.0, 0.0 });
		registerCallback<(int)Parameters::HighThreshold>(p);
		p.setSkewForCentre(-6.0);
		p.info.textConverter = parameter::pod::Decibel;
		p.setDefaultValue(0.0);
		data.add(std::move(p));
	}
	{
		parameter::data p("HighRatio", { 0.2, 100.0 });
		registerCallback<(int)Parameters::HighRatio>(p);
		p.setSkewForCentre(1.0);
		p.setDefaultValue(1.0);
		data.add(std::move(p));
	}
	{
		parameter::data p("Knee", { 0.0, 0.3 });
		registerCallback<(int)Parameters::Knee>(p);
		p.setSkewForCentre(0.15);
		p.setDefaultValue(0.15);
		data.add(std::move(p));
	}
	{
		parameter::data p("Attack", { 0.0, 1000.0 });
		registerCallback<(int)Parameters::Attack>(p);
		p.info.textConverter = parameter::pod::Time;
		p.setSkewForCentre(50.0);
		p.setDefaultValue(50.0);
		data.add(std::move(p));
	}
	{
		parameter::data p("Release", { 0.0, 1000.0 });
		registerCallback<(int)Parameters::Release>(p);
		p.info.textConverter = parameter::pod::Time;
		p.setSkewForCentre(50.0);
		p.setDefaultValue(50.0);
		data.add(std::move(p));
	}
	{
		parameter::data p("RMS", { 0.0, 1.0 });
		registerCallback<(int)Parameters::RMS>(p);
		p.setDefaultValue(1.0);
		data.add(std::move(p));
	}
}

void updown_comp::calculateGraph(block values)
{
	for(auto& v: values)
	{
		v = getGainReduction(v);
	}
}

} // dynamics
} // scriptnode
