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

hise::ProcessorMetadata MatrixModulator::createMetadata()
{
	using Par = ProcessorMetadata::ParameterMetadata;
	using Range = scriptnode::InvertableParameterRange;

	return EnvelopeModulator::createBaseMetadata()
		.withId(getClassType())
		.withPrettyName("Matrix Modulator")
		.withDescription("Combines multiple global modulators into a single modulation source with a base value and smoothed output.")
		.withType<hise::EnvelopeModulator>()
		.withParameter(Par(Value)
			.withId("Value")
			.withDescription("The base value of the modulation output")
			.withSliderMode(HiSlider::NormalizedPercentage, {})
			.withDefault(0.5f)
			.withDynamicDefault([](const Processor* p){
				return dynamic_cast<const MatrixModulator*>(p)->getInitialValue();
			}))
		.withParameter(Par(SmoothingTime)
			.withId("SmoothingTime")
			.withDescription("The smoothing time for the value parameter")
			.withSliderMode(HiSlider::Time, Range(0.0, 2000.0, 0.1).withCentreSkew(200.0))
			.withDefault(50.0f));
}

Array<Identifier> MatrixModulator::getRangeIds(bool isInput)
{

	if(isInput)
	{
		static const Array<Identifier> ip({
			Identifier("min"),
			Identifier("max"),
			Identifier("stepSize"),
			Identifier("middlePosition"),
			MatrixIds::TextConverter,
			MatrixIds::LogInput
		});

		return ip;
		
	}
	else
	{
		static const Array<Identifier> op({
			Identifier("min"),
			Identifier("max"),
			Identifier("stepSize"),
			Identifier("middlePosition"),
			MatrixIds::UseMidPositionAsZero
		});

		return op;
	}
}

MatrixModulator::Item::Item(MatrixModulator& parent_, const ValueTree& v, UndoManager* um_):
  displayBuffer(new SimpleRingBuffer()),
  parent(parent_),
  data(v),
  um(um_)
{
	{
		displayBuffer->setProperty("BufferLength", 44110 / HISE_CONTROL_RATE_DOWNSAMPLING_FACTOR);
		SimpleReadWriteLock::ScopedWriteLock sl(displayBuffer->getDataLock());
		ExternalData d(displayBuffer.get(), 0);
		mod.setExternalData(d, 0);
	}

	watcher.setCallback(data, 
	                    MatrixIds::Helpers::getWatchableIds(), 
	                    valuetree::AsyncMode::Synchronously,
	                    BIND_MEMBER_FUNCTION_2(Item::onUpdate));
}

void MatrixModulator::Item::onUpdate(const Identifier& id, const var& newValue)
{
	if(id == MatrixIds::Mode)
	{
		auto tm = getModeValue(newValue);
		mod.setMode(tm);
		isScale = tm == 0.0;
		isBipolar = tm == 2.0;
	}
				
	else if (id == MatrixIds::Intensity)
	{
		intensity = jlimit(-1.0f, 1.0f, (float)newValue);
		FloatSanitizers::sanitizeFloatNumber(intensity);
		mod.setIntensity(intensity);
	}
	else if (id == MatrixIds::AuxIntensity)
	{
		auxIntensity = jlimit(-1.0f, 1.0f, (float)newValue);
		FloatSanitizers::sanitizeFloatNumber(auxIntensity);
		mod.setAuxIntensity(auxIntensity);
	}
	else if (id == MatrixIds::Inverted)
	{
		inverted = (bool)newValue;
		mod.setInverted(inverted ? 1.0 : 0.0);
	}
	else if (id == MatrixIds::SourceIndex)
	{
		sourceIndex = (int)newValue;
		mod.setSourceIndex((double)newValue);
	}
	else if (id == MatrixIds::AuxIndex)
	{
		mod.setAuxIndex((double)newValue);
	}
	else if (id == MatrixIds::Bypassed)
	{
		// APEX: a bypassed connection keeps its data (and its runtime cable) but drops out of
		// the render lists, which rebuildModList() swaps under the matrix lock.
		auto shouldBeBypassed = (bool)newValue;

		if(bypassed != shouldBeBypassed)
		{
			bypassed = shouldBeBypassed;
			parent.rebuildModList();
		}
	}
}

void MatrixModulator::Item::handleScaleDrag(bool isDown, float delta)
{
	if(!isConnected())
		return;

	if(isDown)
		startIntensity = intensity;
	else
	{
		auto thisIntensity = startIntensity + delta * JUCE_LIVE_CONSTANT_OFF(0.333f);
		auto mv = isScale ? 0.0f : -1.0f;
		thisIntensity = jlimit(mv, 1.0f, thisIntensity);
		data.setProperty(MatrixIds::Intensity, thisIntensity, um);
	}
}

void MatrixModulator::Item::handleDisplayValue(ModulationDisplayValue& mv, scriptnode::InvertableParameterRange outputRange, double fullRangeFactor)
{
	if(!isConnected())
		return;

	// APEX: a bypassed row still reports its RANGE (the knob draws it in the bypass colour)
	// but contributes nothing to the modulated VALUE - it is out of the render lists.
	auto v = mod.getLastModValue();

	if(isScale)
	{
		if(!bypassed)
		{
			mv.scaledValue -= mod.zeroDelta;
			mv.scaledValue *= v;
			mv.scaledValue += mod.zeroDelta;
		}

		if(mod.zeroDelta != 0.0f)
		{
			auto v1 = (float)mv.normalisedValue;
			auto v2 = (float)mod.zeroDelta;

			mv.modulationRange.setStart(jmin(v1, v2));
			mv.modulationRange.setEnd(jmax(v1, v2));
		}
		else
		{
			auto minValue =  mv.modulationRange.getStart() * (1.0f - intensity);
			mv.modulationRange.setStart(minValue);
		}
			

		
	}
	else
	{
		auto factor = fullRangeFactor / outputRange.rng.getRange().getLength();

		auto thisIntensity = intensity * factor;

		auto thisV = v * factor;

		if(!bypassed)
			mv.addValue += thisV;

		auto minValue = mv.modulationRange.getStart();
		auto maxValue = mv.modulationRange.getEnd();

		if(isBipolar)
		{
			minValue -= thisIntensity;
			maxValue += thisIntensity;
		}
		else
		{
			minValue += thisIntensity;
		}

		if(minValue > maxValue)
			std::swap(minValue, maxValue);

		mv.modulationRange = { minValue, maxValue };
	}
}

void MatrixModulator::Item::prepare(double sampleRate, int blockSize, scriptnode::PolyHandler& ph)
{
	PrepareSpecs ps;
	ps.numChannels = 1;
	ps.blockSize = blockSize / HISE_CONTROL_RATE_DOWNSAMPLING_FACTOR;
	ps.sampleRate = sampleRate / HISE_CONTROL_RATE_DOWNSAMPLING_FACTOR;
	ps.voiceIndex = &ph;
	mod.prepare(ps);
}

SimpleRingBuffer* MatrixModulator::Item::getRingBuffer() const
{
	return mod.rb.get();
}

int MatrixModulator::getNumDataObjects(ExternalData::DataType t) const
{
	if (t == ExternalData::DataType::DisplayBuffer)
		return items.size();

	return 0;
}

SimpleRingBuffer* MatrixModulator::getDisplayBuffer(int index)
{
	if(isPositiveAndBelow(index, items.size()))
	{
		return items[index]->getRingBuffer();
	}

	return nullptr;
}

void MatrixModulator::onModulationDrop(int parameterIndex, int modulationSourceIndex)
{
	if(container != nullptr && parameterIndex == SpecialParameters::Value)
	{
		MatrixIds::Helpers::addConnection(globalMatrixData, getMainController(), getMatrixTargetId(), modulationSourceIndex);
	}
}

float MatrixModulator::getInactiveModValue() const
{

	// APEX: with LogInput the base value is a position on the log scale, the output range
	// still expects the input range's own (skewed) position.
	auto baseNorm = isLogInput() ? logNormToInputNorm(baseValue.targetValue) : baseValue.targetValue;

	auto iv = rangeData.outputRange.convertFrom0to1(baseNorm, false);

	// convert it to bipolar -1 ... 1 to match the applyModulation function
	// APEX: a pitch mod is always bipolar (init() hardwires it), so don't trust the flag
	// here - it is skipped on restore while the parent Extra chain is still in
	// CombinedMode (see the mode listener in the constructor).
	if (isBipolar() || getMode() == Modulation::PitchMode)
	{
		iv *= 2.0f;
		iv -= 1.0f;
	}

	if (getMode() == Modulation::PitchMode)
		iv = PitchConverters::normalisedRangeToPitchFactor(iv);

	return iv;
}

String MatrixModulator::getModulationTargetId(int parameterIndex) const
{
	if(parameterIndex == SpecialParameters::Value)
		return getMatrixTargetId();

	return Processor::getModulationTargetId(parameterIndex);
}


void MatrixModulator::setValueRange(bool isInputRange, scriptnode::InvertableParameterRange nr)
{
	if(isInputRange)
	{
		rangeData.inputRange = nr;
		rangeData.inputRange.checkIfIdentity();
	}
	else
	{
		rangeData.outputRange = nr;
		rangeData.outputRange.checkIfIdentity();
	}

	setValueInternal(inputValue);

	if(!isInputRange)
		unbypassIfLiveValuePathNeeded();
}

bool MatrixModulator::needsLiveValuePath() const
{
	auto r = rangeData.outputRange.getRange();
	return r.getStart() != 0.0 || r.getEnd() != 1.0;
}

void MatrixModulator::unbypassIfLiveValuePathNeeded()
{
	// APEX: a bypassed MatrixModulator delivers its knob value through getInactiveModValue(),
	// which maps it through the full output range. The live path does not, so for any output
	// range other than 0...1 the two disagree: a -1...1 PAN slot read R50 as centre and
	// C as hard left while bypassed. Such a slot got bypassed whenever its last matrix
	// connection went away (a preset load clearing the matrix, a deleted row) and the flag
	// was saved with the project. Keep these slots live instead (2026-09-29).
	if(isBypassed() && needsLiveValuePath())
		setBypassed(false, sendNotificationSync);
}

bool MatrixModulator::isLogInput() const
{
	if(!rangeData.logInput)
		return false;

	auto r = rangeData.inputRange.rng;
	return r.start > 0.0 && r.end > r.start;
}

float MatrixModulator::valueToLogNorm(double valueWithinInputRange) const
{
	auto r = rangeData.inputRange.rng;
	auto v = jlimit(r.start, r.end, valueWithinInputRange);
	return (float)(std::log(v / r.start) / std::log(r.end / r.start));
}

float MatrixModulator::logNormToInputNorm(float logNorm) const
{
	auto r = rangeData.inputRange.rng;
	auto v = r.start * std::exp((double)jlimit(0.0f, 1.0f, logNorm) * std::log(r.end / r.start));
	return (float)rangeData.inputRange.convertTo0to1(v, false);
}

void MatrixModulator::setValueInternal(float valueWithinInputRange)
{
	const bool valueChanged = inputValue != valueWithinInputRange;
	inputValue = valueWithinInputRange;

	// APEX: LogInput keeps the base value (and therefore every connection that is added to
	// it) on a logarithmic scale; calculateBlock() converts back before the output range.
	auto norm = isLogInput() ? valueToLogNorm(valueWithinInputRange)
	                         : (float)rangeData.inputRange.convertTo0to1(valueWithinInputRange, false);
	baseValue.set(norm);

	if(auto parentModChain = dynamic_cast<ModulatorChain*>(getParentProcessor(false, false)))
	{
		// APEX: recompute the chain's initial value from EVERY bypassed sibling, whether this
		// modulator is bypassed or not. The active branch used to call
		// setInitialValue(getInitialValue()), and Modulation::getInitialValue() is the NEUTRAL
		// value (1.0 in PitchMode), which setInitialValue() then stores with its "not set" flag -
		// so writing an ACTIVE slot's value threw away a BYPASSED sibling's contribution.
		//
		// Each OSC pitch Extra chain holds three slots: OCTAVE (output range -1.5...2.5) and
		// FINE TUNE (0.458...0.542) are live because their ranges are not 0...1, while SEMITONES
		// (exactly 0...1) stays bypassed and therefore delivers its knob value through the
		// chain's initial value. Re-sending octave or fine after the semitones - which the
		// post-load pitch re-apply in CustomPresetBrowser.js does in exactly that order - wiped
		// the semitone offset, so an osc set to +5 st loaded at the root. The same chain layout
		// (and the same wipe) applies to SUB OCTAVE / SUB PITCH. updateInitialValueFromChildMods()
		// starts from neutral and folds in only the bypassed children, so an active slot's own
		// contribution is not double counted here (2026-09-29).

		// APEX: bypassed (no matrix connection), the value reaches the target through the
		// chain's initial value and never sees baseValue's smoothing, so let the chain glide
		// to it over the same SmoothingTime. Only for a real change while a voice plays.
		if(isBypassed() && valueChanged && smoothingTime > 0.0f &&
		   getMainController()->getNumActiveVoices() > 0)
			parentModChain->requestInitialValueGlide(smoothingTime);

		parentModChain->updateInitialValueFromChildMods();
	}
}


MatrixModulator::MatrixModulator(MainController* mc, const String& id, int voiceAmount, Modulation::Mode m):
  EnvelopeModulator(mc, id, voiceAmount, m),
  Modulation(m),
  polyHandler(voiceAmount > 1),
  
  valueRangeData(MatrixIds::ValueRange)
{
	switch(m)
	{
	case GainMode:
	case CombinedMode:
		rangeData.outputRange = { 0.0, 1.0, 0.0 };
		break;
	case PitchMode:
	case PanMode:
	case GlobalMode:
	case OffsetMode:
		rangeData.outputRange = { -1.0, 1.0, 0.0 };
		break;
	case numModes:
		jassertfalse;
		break;
	default: ;
	}

	rangeData.outputRange.checkIfIdentity();

	{
		auto set = scriptnode::RangeHelpers::IdSet::ScriptComponents;
		ValueTree ir(MatrixIds::InputRange);
		ValueTree outr(MatrixIds::OutputRange);

		RangeHelpers::storeDoubleRange(ir, rangeData.inputRange, nullptr, set);
		RangeHelpers::storeDoubleRange(outr, rangeData.outputRange, nullptr, set);

		ir.setProperty(MatrixIds::TextConverter, "NormalizedPercentage", nullptr);
		outr.setProperty(MatrixIds::UseMidPositionAsZero, false, nullptr);

		valueRangeData.addChild(ir, -1, nullptr);
		valueRangeData.addChild(outr, -1, nullptr);

		inputRangeWatcher.setCallback(ir, getRangeIds(true), valuetree::AsyncMode::Synchronously,
		[this](const Identifier& id, const var& newValue)
		{
			this->onRangeUpdate(id, newValue, true);
		});

		outputRangeWatcher.setCallback(outr, getRangeIds(false), valuetree::AsyncMode::Synchronously,
		[this](const Identifier& id, const var& newValue)
		{
			this->onRangeUpdate(id, newValue, false);
		});

	}

	ModulatorSynthChain *chain = mc->getMainSynthChain();

	chain->getHandler()->addListener(this);

	updateParameterSlots();

	init();

	// APEX: a hardcoded synth creates its Extra chains in CombinedMode and only switches one
	// to PitchMode in updateModulationProperties(), after the children were restored. A
	// pitch MatrixModulator in such a chain is therefore constructed and restored as a
	// unipolar gain modulator (the Bipolar attribute is skipped for GainMode) and keeps
	// that flag when the chain re-modes it. Bypassed, its inactive value then read
	// exp2(0.5) instead of exp2(0): APEX's GLOBAL PITCH slot sat a quarter octave up,
	// +2 octaves after the engine's two squarings, on every preset load (2026-09-22).
	// Re-run the pitch / pan hardwiring whenever the mode changes.
	modeBroadcaster.addListener(*this, [](MatrixModulator& m, int) { m.init(); }, false);
}

MatrixModulator::~MatrixModulator()
{
	{
		LockHelpers::SafeLock sl(getMainController(), LockHelpers::Type::AudioLock, getMainController()->isInitialised());
		for(auto i: items)
		{
			i->mod.disconnect();
		}
	}

	getMainController()->getMainSynthChain()->getHandler()->removeListener(this);
}

void MatrixModulator::setInternalAttribute(int parameterIndex, float newValue)
{
	if(parameterIndex < EnvelopeModulator::numParameters)
	{
		EnvelopeModulator::setInternalAttribute(parameterIndex, newValue);
		return;
	}

	switch(parameterIndex)
	{
	case SpecialParameters::Value:
		setValueInternal(newValue);

		// APEX: a knob bound to this modulator never touches the owning effect's attributes, so the
		// silence-suspended network it feeds would not wake for the change (the direct path is
		// HardcodedMasterFX::setInternalAttribute). The chain's owner is the effect. Not from the
		// audio thread: a scripted matrix target pushes its modulated value here at control rate
		// while its source moves, which must not keep a sleeping network awake.
		if (getMainController()->getKillStateHandler().getCurrentThread() == MainController::KillStateHandler::TargetThread::AudioThread)
			break;

		if (auto chain = dynamic_cast<ModulatorChain*>(getParentProcessor(false, false)))
		{
			if (auto fx = dynamic_cast<EffectProcessor*>(chain->getParentProcessor()))
				fx->wakeFromSilence();
		}

		break;
	case SpecialParameters::SmoothingTime:
		baseValue.prepare(getSampleRate() / HISE_CONTROL_RATE_DOWNSAMPLING_FACTOR, newValue);
		smoothingTime = newValue;
		break;
	}
}

float MatrixModulator::getAttribute(int parameterIndex) const
{
	if(parameterIndex < EnvelopeModulator::numParameters)
	{
		return EnvelopeModulator::getAttribute(parameterIndex);
	}

	switch(parameterIndex)
	{
	case SpecialParameters::Value:
		return inputValue;
	case SpecialParameters::SmoothingTime:
		return smoothingTime;
	default:
		jassertfalse;
		return 0.0f;
	}
}

ModulationDisplayValue::QueryFunction::Ptr MatrixModulator::getModulationQueryFunction(int parameterIndex) const
{
	if(parameterIndex == SpecialParameters::Value)
	{
		struct QueryFunction: public ModulationDisplayValue::QueryFunction
		{
			bool onScaleDrag(Processor* p, bool isDown, float delta) override
			{
				return dynamic_cast<MatrixModulator*>(p)->onScaleDrag(isDown, delta);
			}

			ModulationDisplayValue getDisplayValue(Processor* p, double nv, NormalisableRange<double> nr, int sourceIndex) const override
			{
				auto mm = dynamic_cast<MatrixModulator*>(p);

				ScopedValueSetter<int> sv(mm->displaySourceIndex, sourceIndex);
				return mm->getDisplayValue(nv, nr);
			}
		};

		return new QueryFunction();
	}

	return EnvelopeModulator::getModulationQueryFunction(parameterIndex);
}

void MatrixModulator::setBypassed(bool shouldBeBypassed, NotificationType notifyChangeHandler) noexcept
{
	EnvelopeModulator::setBypassed(shouldBeBypassed, notifyChangeHandler);
	setValueInternal(inputValue);
}

void MatrixModulator::restoreFromValueTree(const ValueTree& v)
{
	EnvelopeModulator::restoreFromValueTree(v);

	loadAttribute(Value, "Value");
	loadAttribute(SmoothingTime, "SmoothingTime");

	customTargetId = v.getProperty("CustomId", "").toString();

	auto vr = v.getChildWithName(MatrixIds::ValueRange);
	auto ri = vr.getChildWithName(MatrixIds::InputRange);
	auto ro = vr.getChildWithName(MatrixIds::OutputRange);

	setRangeData(MatrixIds::Helpers::Properties::RangeData::fromValueTrees(ri, ro));

	// APEX: projects / presets saved while such a slot was auto-bypassed restore it bypassed
	unbypassIfLiveValuePathNeeded();

#if 0
	rangeData.fromValueTrees(ri, ro);

	if(ri.isValid())
		getRangeData(true).copyPropertiesFrom(ri, nullptr);
		
	if(ro.isValid())
		getRangeData(false).copyPropertiesFrom(ro, nullptr);
#endif
}

ValueTree MatrixModulator::exportAsValueTree() const
{
	auto v = EnvelopeModulator::exportAsValueTree();

	v.setProperty("CustomId", customTargetId, nullptr);
	saveAttribute(Value, "Value");
	saveAttribute(SmoothingTime, "SmoothingTime");

	v.addChild(valueRangeData.createCopy(), -1, nullptr);

	return v;
}

float MatrixModulator::startVoice(int voiceIndex)
{
	if(auto sl = SimpleReadWriteLock::ScopedTryReadLock(matrixLock))
	{
		PolyHandler::ScopedVoiceSetter svs(polyHandler, voiceIndex);

		auto ownerSynth = static_cast<ModulatorSynth*>(getParentProcessor(true));
		auto ownerVoice = static_cast<ModulatorSynthVoice*>(ownerSynth->getVoice(voiceIndex));
		auto e = ownerVoice->getCurrentHiseEvent();

		active[voiceIndex] = true;

		baseValue.reset();
		float dv = baseValue.get();

		for(auto m: allMods)
		{
			m->handleHiseEvent(e);
			m->calculateVoiceStart(dv);
		}
			
		return dv;
	}

	return EnvelopeModulator::startVoice(voiceIndex);
}

void MatrixModulator::stopVoice(int voiceIndex)
{
	EnvelopeModulator::stopVoice(voiceIndex);

	active[voiceIndex] = false;

	// APEX: tell the connections a key was released so the knob display can move on to the
	// newest held note instead of following this one's release.
	if(auto sl = SimpleReadWriteLock::ScopedTryReadLock(matrixLock))
	{
		PolyHandler::ScopedVoiceSetter svs(polyHandler, voiceIndex);

		for(auto m: allMods)
			m->onVoiceReleased();
	}
}

void MatrixModulator::reset(int voiceIndex)
{
	EnvelopeModulator::reset(voiceIndex);

	// only call reset in poly mode because it will reset
	// the uptime in the target mods and the monophonic envelope
	// will have a continuous stream of samples.
	if(!isInMonophonicMode())
	{
		SimpleReadWriteLock::ScopedReadLock sl(matrixLock);
		PolyHandler::ScopedVoiceSetter svs(polyHandler, voiceIndex);

		for(auto m: allMods)
			m->reset();
	}
}

bool MatrixModulator::isPlaying(int voiceIndex) const
{
	if(hasScaleEnvelopes)
	{
		if(auto sl = SimpleReadWriteLock::ScopedTryReadLock(matrixLock))
		{
			PolyHandler::ScopedVoiceSetter svs(polyHandler, voiceIndex);

			auto ok = true;

			for(auto m: allMods)
			{
				ok &= m->isPlaying();
			}
				
			return ok;
		}

		return false;
	}
	else
	{
		return active[voiceIndex];
	}
}

void MatrixModulator::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	if(sampleRate > 0.0)
	{
		init();

		EnvelopeModulator::prepareToPlay(sampleRate, samplesPerBlock);

		SimpleReadWriteLock::ScopedReadLock sl(matrixLock);

		baseValue.prepare(sampleRate / HISE_CONTROL_RATE_DOWNSAMPLING_FACTOR, smoothingTime);

		for(auto m: items)
		{
			m->prepare(sampleRate, samplesPerBlock, polyHandler);
		}
	}
}

void MatrixModulator::calculateBlock(int startSample, int numSamples)
{
	auto ptr = internalBuffer.getWritePointer(0, startSample);

	if(baseValue.isActive())
	{
		for(int i = 0; i < numSamples; i++)
			ptr[i] = baseValue.advance();
	}
	else
		FloatVectorOperations::fill(ptr, baseValue.get(), numSamples);

	if(auto sl = SimpleReadWriteLock::ScopedTryReadLock(matrixLock))
	{
		auto voiceIndex = polyManager.getCurrentVoice();
		scriptnode::PolyHandler::ScopedVoiceSetter sv(polyHandler, voiceIndex);
		ProcessData<1> pd(&ptr, numSamples, 1);

		// APEX (HISE_HOLD_MODS_WITHOUT_VOICES): no plot writes while no voice is active anywhere.
		const bool plot = !(getMainController()->shouldHoldModsWithoutVoices() &&
		                    getMainController()->getNumActiveVoices() == 0);

		for(auto sl: scaleMods)
		{
			sl->plotEnabled = plot;
			sl->process(pd);
		}

		for(auto al: addMods)
		{
			al->plotEnabled = plot;
			al->process(pd);
		}
	}

	// APEX: LogInput - the sum above lives on the log scale (base + connections, so a
	// connection moves the value by the same number of octaves wherever the base sits).
	// Bring it back to the input range's own position, which is what the output range and
	// everything downstream were set up for.
	if(isLogInput())
	{
		for(int i = 0; i < numSamples; i++)
			ptr[i] = logNormToInputNorm(ptr[i]);
	}

	if(rangeData.outputRange.isNonDefault())
	{
		ModBufferExpansion::applySkewFactor(ptr, numSamples, rangeData.outputRange.rng);
	}
}

void MatrixModulator::processorChanged(EventType, Processor* p)
{
	if(dynamic_cast<GlobalModulatorContainer*>(p) != nullptr)
		init();
}

ModulationDisplayValue MatrixModulator::getDisplayValue(double nv, NormalisableRange<double> nr) const
{
	// APEX: LogInput - run the same bookkeeping on the log scale, then map the results onto
	// the slider's own range below so the ring and the live value sit where the sound is.
	const bool logDisplay = isLogInput();

	ModulationDisplayValue mv;
	mv.normalisedValue = logDisplay ? (double)valueToLogNorm(nv) : nr.convertTo0to1(nv);
	mv.scaledValue = mv.normalisedValue;
	mv.modulationRange = { mv.normalisedValue, mv.normalisedValue };

	auto fullRange = getMode() == Modulation::PitchMode ? 2.0f : 1.0f;

	bool anyConnected = false;
	bool anyActive = false;

	for (auto i : items)
	{
		if (displaySourceIndex == -1 || i->sourceIndex == displaySourceIndex)
		{
			anyConnected |= i->isConnected();
			anyActive |= i->isActive();
			i->handleDisplayValue(mv, rangeData.outputRange, fullRange);
		}
	}

	// APEX: the queried rows count as "active" even when every one is bypassed, so the knob keeps
	// its ring and dragger; modulationBypassed tells the LAF to draw that ring in the bypass colour.
	mv.modulationActive = !allMods.empty() || anyConnected;
	mv.modulationBypassed = anyConnected && !anyActive;

	if(logDisplay)
	{
		auto ir = rangeData.inputRange.rng;
		auto ratio = std::log(ir.end / ir.start);

		auto toSlider = [&](double logNorm)
		{
			auto v = ir.start * std::exp(jlimit(0.0, 1.0, logNorm) * ratio);
			return nr.convertTo0to1(nr.snapToLegalValue(v));
		};

		auto scaled = toSlider(mv.scaledValue);
		auto live = toSlider(mv.scaledValue + mv.addValue);
		auto rStart = toSlider(mv.modulationRange.getStart());
		auto rEnd = toSlider(mv.modulationRange.getEnd());

		mv.normalisedValue = nr.convertTo0to1(nr.snapToLegalValue(nv));
		mv.scaledValue = scaled;
		mv.addValue = live - scaled;
		mv.modulationRange = { jmin(rStart, rEnd), jmax(rStart, rEnd) };
	}

	mv.clipTo0To1();

	return mv;
}

bool MatrixModulator::onScaleDrag(bool isDown, float delta)
{
	for(auto i: items)
		i->handleScaleDrag(isDown, delta);

	return true;
}

void MatrixModulator::rebuildModList()
{
	auto signal = container->getMatrixModulatorConnection();

	std::vector<ModType*> newScaleMods;
	std::vector<ModType*> newAddMods;
	std::vector<ModType*> newAllMods;

	newScaleMods.reserve(items.size());
	newAddMods.reserve(items.size());
	newAllMods.reserve(items.size());

	hasScaleEnvelopes = false;

	if(container != nullptr)
	{
		auto mc = container->getChildProcessor(ModulatorSynth::InternalChains::GainModulation);

		for(auto i: items)
		{
			if(i->bypassed)
				continue;

			if(isPositiveAndBelow(i->sourceIndex, mc->getNumChildProcessors()))
			{
				auto mod = dynamic_cast<EnvelopeModulator*>(mc->getChildProcessor(i->sourceIndex));

				auto isPolyphonicScaleMod = mod != nullptr && !mod->isInMonophonicMode();
				hasScaleEnvelopes |= isPolyphonicScaleMod;
			}
		}
	}

	for(auto i: items)
	{
		if(!i->isActive())
			continue;

		auto m = &i->mod;
		newAllMods.push_back(m);

		if(i->isScale)
			newScaleMods.push_back(m);
		else
			newAddMods.push_back(m);
	}

	{
		LockHelpers::SafeLock sl(getMainController(), LockHelpers::Type::AudioLock);

		for(auto i: items)
			i->mod.connectToRuntimeTarget(true, signal);
	}

	{
		SimpleReadWriteLock::ScopedWriteLock sl(matrixLock);
		std::swap(allMods, newAllMods);
		std::swap(scaleMods, newScaleMods);
		std::swap(addMods, newAddMods);
	}
}

void MatrixModulator::onMatrixChange(const ValueTree& v, bool wasAdded)
{
	// APEX: an item that exists for this connection must be removable even when the row no
	// longer targets this modulator (its TargetId was changed in the matrix table).
	bool hasItem = false;

	for(auto i: items)
		hasItem |= i->data == v;

	if(wasAdded && hasItem)
		return;

	if(MatrixIds::Helpers::matchesTarget(v, getMatrixTargetId()) || (!wasAdded && hasItem))
	{
		ScopedPointer<Item> toBeDeleted;

		if(wasAdded)
		{
			items.add(new Item(*this, v, getMainController()->getControlUndoManager()));
			items.getLast()->mod.setZeroPosition((double)rangeData.useMidPositionAsZero);
			items.getLast()->displayBuffer->setGlobalUIUpdater(getMainController()->getGlobalUIUpdater());
			items.getLast()->prepare(getSampleRate(), getLargestBlockSize(), polyHandler);
		}
		else
		{
			for(auto i: items)
			{
				if(i->watcher.isRegisteredTo(v))
				{
					toBeDeleted = i;
					items.removeObject(i, false);
					break;
				}
			}
		}

		rebuildModList();

		if(wasAdded)
			prepareToPlay(getSampleRate(), getLargestBlockSize());

		if(toBeDeleted != nullptr)
		{
			LockHelpers::SafeLock sl(getMainController(), LockHelpers::Type::AudioLock);
			toBeDeleted->mod.disconnect();
		}

		toBeDeleted = nullptr;

		if(items.isEmpty())
		{
			auto pChain = getParentProcessor(false, false);
			auto ownerSynth = getParentProcessor(true, false);

			bool canBeBypassed = true;

			if(ownerSynth->getChildProcessor(ModulatorSynth::InternalChains::GainModulation) == pChain)
			{
				canBeBypassed = false;

				for(int i = 0; i < pChain->getNumChildProcessors(); i++)
				{
					auto c = pChain->getChildProcessor(0);

					if(c == this)
						continue;

					if(auto e = dynamic_cast<EnvelopeModulator*>(c))
					{
						canBeBypassed |= !e->isBypassed();

						if(canBeBypassed)
							break;
					}
				}
			}

			// APEX: see unbypassIfLiveValuePathNeeded()
			if(needsLiveValuePath())
				canBeBypassed = false;

			if(canBeBypassed)
				setBypassed(true, sendNotificationSync);
		}
		else
		{
			setBypassed(false, sendNotificationSync);
		}
	}
}

void MatrixModulator::onModeChange(const ValueTree& v, const Identifier& id)
{
	// APEX: the matrix page's ADD button creates a blank row (no target, no source) and the
	// target is picked in the row afterwards. Connections were only ever matched against this
	// modulator when the row was ADDED, so such a row never got an Item here and did nothing
	// until the preset was reloaded. Treat a TargetId change as the row arriving / leaving.
	if(id == MatrixIds::TargetId)
	{
		auto matches = MatrixIds::Helpers::matchesTarget(v, getMatrixTargetId());
		onMatrixChange(v, matches);
		return;
	}

	if(MatrixIds::Helpers::matchesTarget(v, getMatrixTargetId()))
	{
		jassert(id == MatrixIds::Mode || id == MatrixIds::SourceIndex);

		// APEX: this watcher was registered before the item's own watcher, so it fires
		// first. A connection created as "add the row, then set SourceIndex" (the matrix
		// page does this) was rebuilt while its item still had sourceIndex -1, left out
		// of the mod lists, and stayed dead until something else rebuilt them (a Mode
		// change). Sync the item from the tree before rebuilding so order doesn't matter.
		for(auto i: items)
		{
			if(i->data == v)
				i->onUpdate(id, v[id]);
		}

		rebuildModList();
	}
}

double MatrixModulator::getModeValue(const var& v)
{
	return (double)v;
}

void MatrixModulator::init()
{
	// hardwire these settings into any mod that is loaded into a pitch chain
	auto m = getMode();

	if(m == Modulation::PitchMode || m == Modulation::PanMode)
	{
		setIntensity(1.0f);
		setIsBipolar(true);
	}

	if(container != nullptr)
		return;

	if(auto gc = ProcessorHelpers::getFirstProcessorWithType<GlobalModulatorContainer>(getMainController()->getMainSynthChain()))
	{
		container = gc;

		{
			SimpleReadWriteLock::ScopedWriteLock sl(matrixLock);
			items.clear();
		}

		globalMatrixData = gc->getMatrixModulatorData();
		connectionWatcher.setCallback(globalMatrixData, 
		                              valuetree::AsyncMode::Synchronously, 
		                              BIND_MEMBER_FUNCTION_2(MatrixModulator::onMatrixChange));

		modeWatcher.setCallback(globalMatrixData,
		                        { MatrixIds::SourceIndex, MatrixIds::Mode, MatrixIds::TargetId },
		                        valuetree::AsyncMode::Synchronously,
		                        BIND_MEMBER_FUNCTION_2(MatrixModulator::onModeChange));

		container->matrixProperties.propertyUpdateBroadcaster.addListener(*this, [](MatrixModulator& mm, MatrixIds::Helpers::Properties* p, const String& changedTarget)
		{
			if(mm.rangeRecursiveChecker)
				return;

			auto targetId = mm.getMatrixTargetId();

			if(changedTarget.isEmpty() || targetId == changedTarget)
			{
				auto tr = p->getRangeData(targetId);

				if(tr.first)
				{
					mm.setRangeData(tr.second);
				}
			}
		});
	}
}

void MatrixModulator::onRangeUpdate(const Identifier& id, const var& newValue, bool isInputRange)
{
	auto set = scriptnode::RangeHelpers::IdSet::ScriptComponents;
	auto r = RangeHelpers::getDoubleRange(getRangeData(isInputRange), set);

	if(isInputRange)
	{
		if(id == MatrixIds::TextConverter)
			rangeData.converter = newValue.toString();
		else if(id == MatrixIds::LogInput)
		{
			rangeData.logInput = (bool)newValue;
			setValueInternal(inputValue);
		}
		else
			setValueRange(true, r);
	}
	else
	{
		if(id == MatrixIds::UseMidPositionAsZero)
		{
			rangeData.useMidPositionAsZero = (bool)newValue;

			for(auto a: items)
			{
				a->mod.setZeroPosition((double)rangeData.useMidPositionAsZero);
			}
		}
			
		else
			setValueRange(false, r);
	}

	if(container != nullptr && !rangeRecursiveChecker)
	{
		container->matrixProperties.rangeData[getMatrixTargetId()] = rangeData;

		ScopedValueSetter<bool> svs(rangeRecursiveChecker, true);
		container->matrixProperties.sendChangeMessage(getMatrixTargetId());
	}
}

ProcessorEditorBody* MatrixModulator::createEditor(ProcessorEditor* parentEditor)
{
#if USE_BACKEND
	return new MatrixModulatorBody(parentEditor);
#else

	ignoreUnused(parentEditor);
	jassertfalse;
	return nullptr;
#endif
}

} // namespace hise
