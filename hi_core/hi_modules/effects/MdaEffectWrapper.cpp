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

namespace hise { using namespace juce;

// The mda limiter stores every parameter normalised 0...1 and does its own
// dB / ms conversion internally (see mdaLimiter::setParameter), so all five are
// declared as normalised percentages here. Defaults match mdaLimiter's ctor.
hise::ProcessorMetadata MdaLimiterEffect::createMetadata()
{
	using Par = ProcessorMetadata::ParameterMetadata;
	using Range = scriptnode::InvertableParameterRange;

	return ProcessorMetadata()
		.withStandardMetadata<MdaLimiterEffect>()
		.withDescription("The legacy mda limiter (deprecated - kept so that pre-2024 projects still load)")
		.withParameter(Par(Threshhold)
			.withId("Threshhold")
			.withDescription("Limiting threshold, -40dB to 0dB across the range")
			.withSliderMode(HiSlider::NormalizedPercentage, Range())
			.withDefault(0.6f))
		.withParameter(Par(Output)
			.withId("Output")
			.withDescription("Output trim, -20dB to +20dB across the range")
			.withSliderMode(HiSlider::NormalizedPercentage, Range())
			.withDefault(0.6f))
		.withParameter(Par(Attack)
			.withId("Attack")
			.withDescription("Attack time")
			.withSliderMode(HiSlider::NormalizedPercentage, Range())
			.withDefault(0.15f))
		.withParameter(Par(Release)
			.withId("Release")
			.withDescription("Release time")
			.withSliderMode(HiSlider::NormalizedPercentage, Range())
			.withDefault(0.5f))
		.withParameter(Par(Knee)
			.withId("Knee")
			.withDescription("Below 0.5 the knee is hard, above it is soft")
			.withSliderMode(HiSlider::NormalizedPercentage, Range())
			.withDefault(0.4f));
}

MdaLimiterEffect::MdaLimiterEffect(MainController *mc, const String &id):
	MdaEffectWrapper(mc, id)
{
	effect = new mdaLimiter();

	finaliseModChains();

	updateParameterSlots();
};

ProcessorEditorBody *MdaLimiterEffect::createEditor(ProcessorEditor *parentEditor)
{
#if USE_BACKEND

	return new MdaLimiterEditor(parentEditor);

	
#else

	ignoreUnused(parentEditor);
	jassertfalse;

	return nullptr;

#endif
};

} // namespace hise
