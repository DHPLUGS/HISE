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

namespace hise { using namespace juce;


#define DECLARE_ID(x) static const Identifier x(#x);
namespace MatrixIds
{
DECLARE_ID(Connection);
DECLARE_ID(TargetId);
DECLARE_ID(SourceIndex);
DECLARE_ID(Intensity);
DECLARE_ID(Mode);
DECLARE_ID(AuxIndex);
DECLARE_ID(AuxIntensity);
DECLARE_ID(Inverted);
DECLARE_ID(Bypassed);   // APEX: the connection stays in the matrix but is skipped by the engine
DECLARE_ID(MatrixData);

DECLARE_ID(IsNormalized);
DECLARE_ID(ValueRange);
DECLARE_ID(IsModulator);
DECLARE_ID(InputRange);
DECLARE_ID(OutputRange);
DECLARE_ID(TextConverter);
DECLARE_ID(UseMidPositionAsZero);
DECLARE_ID(LogInput); // APEX: see Properties::RangeData::logInput

DECLARE_ID(DefaultInitValues);
DECLARE_ID(SelectableSources);
DECLARE_ID(RangeProperties);

struct Helpers
{
	enum class TargetType
	{
		All,
		Modulators,
		Parameters
	};

	enum class DragTargetType
	{
		Inactive = 0,
		Dragging,
		Hover
	};

	enum class RangePresets
	{
		NormalizedPercentage,
		Gain0dB,
		Gain6dB,
		Pitch1Octave,
		Pitch2Octaves,
		Pitch1Semitone,
		PitchOctaveStep,
		PitchSemitoneStep,
		FilterFreq,
		FilterFreqLog,
		Stereo,
		LinearAbsolute,
		numPresets
	};

	struct Properties
	{
		Properties()
		{
			propertyUpdateBroadcaster.sendMessage(dontSendNotification, this, "");
		}

		struct DefaultInitValue
		{
			float intensity = 0.0f;
			scriptnode::modulation::TargetMode defaultMode = scriptnode::modulation::TargetMode::Raw;
			bool isNormalised = true;

			operator bool() const { return defaultMode != scriptnode::modulation::TargetMode::Raw; }
		};

		struct RangeData
		{
			static constexpr scriptnode::RangeHelpers::IdSet IDSET = scriptnode::RangeHelpers::IdSet::ScriptComponents;

			RangeData():
			  converter("NormalizedPercentage"),
			  inputRange({0.0, 1.0, 0.0}),
			  outputRange({0.0, 1.0, 0.0})
			{
				inputRange.checkIfIdentity();
				outputRange.checkIfIdentity();
			}

			var toJSON() const;
			static RangeData fromValueTrees(const ValueTree& ri, const ValueTree& ro);
			void writeToValueTrees(ValueTree& ri, ValueTree& ro);
			static RangeData fromJSON(const var& jsonOrPresetName);

			String rangePreset;
			String converter;
			bool useMidPositionAsZero = false;

			// APEX: opt-in (InputRange property "LogInput"). The modulation is then added on a
			// logarithmic scale between the input range's min and max (even in octaves for a
			// frequency) instead of on the skewed knob position. Off by default, in which case
			// nothing about the modulator changes.
			bool logInput = false;

			scriptnode::InvertableParameterRange inputRange;
			scriptnode::InvertableParameterRange outputRange;
		};

		void sendChangeMessage(const String& targetId);
		void fromJSON(const var& obj);
		var toJSON(const MainController* mc) const;

		std::pair<bool, RangeData> getRangeData(const String& targetId) const;
		DefaultInitValue getConvertedIntensityValue(const String& targetId) const;

		std::map<String, DefaultInitValue> initValues;
		std::map<String, RangeData> rangeData;
		bool selectableSources = false;

		LambdaBroadcaster<Properties*, String> propertyUpdateBroadcaster;
	};

	static StringArray getRangePresetNames();
	static Properties::RangeData getRangePreset(RangePresets p);

	// APEX: Bypassed comes first because this order is also the matrix table's column order (the ON lamp is the first cell).
	static Array<Identifier> getWatchableIds() { return { Bypassed, SourceIndex, TargetId, Mode, Inverted, Intensity, AuxIndex, AuxIntensity }; };

	// APEX: a connection restored with a Mode the matrix table does not offer gets the target's
	// init-table default, else Unipolar. Presets written before the mode list settled carry 4 (Raw),
	// which the engine renders as nothing and the table shows as an empty MODE cell. Returns true
	// if the property was rewritten, so a later save stores the mode the table shows.
	static bool sanitiseConnectionMode(ValueTree& connection, const Properties& properties, UndoManager* um);
	static ValueTree addConnection(ValueTree& v, MainController* um, const String& targetId, int sourceIndex=-1);
	static ValueTree getMatrixDataFromGlobalContainer(MainController* mc);
	static bool removeConnection(ValueTree& data, UndoManager* um, const String& targetId, int sourceIndex);
	static bool removeLastConnection(ValueTree& data, UndoManager* um, const String& targetId);
	static bool removeAllConnections(ValueTree& data, UndoManager* um, const String& targetId);
	static bool matchesTarget(const ValueTree& data, const String& targetId);

	static ValueTree getConnection(const ValueTree& matrixData, int sourceIndex, const String& targetId);

	static void fillModSourceList(const MainController* mc, StringArray& items);
	static void fillModTargetList(const MainController* mc, StringArray& items, TargetType t);

	static int getModulationSourceDragIndex(const var& data);
	static void updateDragState(HiSlider* s, DragTargetType state);
	static void repaintMatrixSlidersOnDrag(Component* c, const var& data, DragTargetType state);

	static void startDragging(Component* c, int sourceIndex);
	static void stopDragging(Component* c);

	static TargetType getTargetType(const MainController* mc, const String& targetId);

	struct IntensityTextConverter: public ValueToTextConverter::CustomConverter,
	                               public dispatch::Processor::AttributeListener
	{
		struct ConstructData
		{
			WeakReference<Processor> p;
			int parameterIndex;
			String prettifierMode;
			scriptnode::modulation::TargetMode tm;
			NormalisableRange<double> inputRange;
			Component::SafePointer<Slider> connectedSlider;

			/** Show the intensity as a plain percentage of the target's range instead of
			    translating it into the target's units (used by the matrix rows). */
			bool showAsPercentage = false;
		};

		IntensityTextConverter(const ConstructData& cd);
		~IntensityTextConverter() override;

		void onAttributeUpdate(Processor* , uint16 index) override;
		double getValue(const String& text) const override;
		String getText(double v) const override;

		bool showInactive = false;
		ConstructData data;
		ValueToTextConverter prettifier;
	};
};

}
#undef DECLARE_ID


} // namespace hise
