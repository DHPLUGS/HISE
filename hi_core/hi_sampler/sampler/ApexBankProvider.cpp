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
*   ===========================================================================
*/

namespace hise { using namespace juce;

namespace ApexBankIds
{
static const char kMagic[8] = { 'A', 'P', 'E', 'X', 'B', 'A', 'N', 'K' };
static const char kCheckWord[4] = { 'A', 'P', 'O', 'K' };

// A bank is read into memory as a whole. The largest one today is 13 MB; anything near
// this limit is not a bank.
static const int64 kMaxFileSize = 1024 * 1024 * 1024;
}

XYZBankProvider::XYZBankProvider(MainController* mc) :
	XYZSampleMapProvider(mc)
{
}

juce::Identifier XYZBankProvider::getStaticId()
{
	RETURN_STATIC_IDENTIFIER("Bank");
}

juce::Identifier XYZBankProvider::getId() const
{
	return getStaticId();
}

juce::String XYZBankProvider::getFileExtension()
{
	return ".apxbk";
}

juce::File XYZBankProvider::getBankFile(const String& reference) const
{
	// Same folder that FileSystem.getFolder(FileSystem.Samples) returns to the script, which
	// is where the script looks for banks.
	auto sampleFolder = getMainController()->getCurrentFileHandler().getSubDirectory(FileHandlerBase::Samples);

	if (reference.isEmpty() || reference.contains("..") || File::isAbsolutePath(reference))
		return {};

	auto f = sampleFolder.getChildFile(reference + getFileExtension());

	if (!f.isAChildOf(sampleFolder))
		return {};

	return f;
}

juce::MemoryBlock XYZBankProvider::createKey(const String& projectKey, int keyId)
{
	String s;
	s << "APXBK1:" << projectKey << ":" << String(keyId);

	SHA256 hash(s.toRawUTF8(), s.getNumBytesAsUTF8());

	return hash.getRawData();
}

juce::String XYZBankProvider::getProjectKey() const
{
#if USE_BACKEND
	auto settings = dynamic_cast<const GlobalSettingManager*>(getMainController());

	if (settings == nullptr)
		return {};

	return settings->getSettingsObject().getSetting(HiseSettings::Project::EncryptionKey).toString();
#else
	return FrontendHandler::getExpansionKey();
#endif
}

bool XYZBankProvider::fail(const String& reference, const String& reason) const
{
	String message;
	message << "Bank " << reference << ": " << reason;

	DBG(message);

	// Kept for the script: ScriptAudioFile.getLastLoadError() after a loadFile() that returned false.
	lastError = message;

#if USE_BACKEND
	auto mc = const_cast<MainController*>(getMainController());
	debugError(mc->getMainSynthChain(), message);
#endif

	return false;
}

String XYZBankProvider::getLastError() const
{
	return lastError;
}

hise::MultiChannelAudioBuffer::SampleReference::Ptr XYZBankProvider::readZone(const ValueTree& s, const String& zoneReference,
	const void* audioData, size_t audioSize) const
{
	// This mirrors HlacMonolithInfo::createUserInterfaceReader() and
	// XYZSampleMapProvider::MonolithDataProvider::loadFile(). The only difference is that
	// the monolith is read from memory instead of from a file.
	MultiChannelAudioBuffer::SampleReference::Ptr lr = new MultiChannelAudioBuffer::SampleReference(true, zoneReference);

	lr->sampleRate = (double)s[MonolithIds::SampleRate];

	const int64 start = (int64)s[MonolithIds::MonolithOffset];
	const int64 length = jmin<int64>((int64)INT_MAX, (int64)s[MonolithIds::MonolithLength]);

	// The reader owns the stream. The stream does not copy the data, which stays alive
	// until parse() has returned.
	ScopedPointer<hlac::HiseLosslessAudioFormatReader> reader = new hlac::HiseLosslessAudioFormatReader(new MemoryInputStream(audioData, audioSize, false));

	reader->setTargetAudioDataType(AudioDataConverters::float32BE);
	reader->sampleRate = lr->sampleRate;

	AudioSubsectionReader afs(reader.release(), start, length, true);

	Range<int> sampleRange((int)s[SampleIds::SampleStart], (int)s[SampleIds::SampleEnd]);

	if (sampleRange.isEmpty())
		sampleRange = Range<int>(0, (int)afs.lengthInSamples);

	lr->buffer.setSize(afs.numChannels, sampleRange.getLength());
	afs.read(&lr->buffer, 0, jmin(sampleRange.getLength(), (int)afs.lengthInSamples), sampleRange.getStart(), true, true);

	return lr;
}

bool XYZBankProvider::parse(const String& v, MultiChannelAudioBuffer::XYZItem::List& list)
{
	lastError = {};

	auto reference = v.fromFirstOccurrenceOf(getWildcard(), false, false);
	auto bankFile = getBankFile(reference);

	if (!bankFile.existsAsFile())
		return fail(reference, "file not found");

	if (bankFile.getSize() > ApexBankIds::kMaxFileSize)
		return fail(reference, "file is too big");

	MemoryBlock mb;

	if (!bankFile.loadFileAsData(mb))
		return fail(reference, "file can't be read");

	auto data = static_cast<uint8*>(mb.getData());
	const size_t size = mb.getSize();

	// check word + map length + codec + audio length + hash
	const size_t minBodySize = 4 + 4 + 1 + 8 + (size_t)kHashSize;

	if (size < (size_t)kHeaderSize + minBodySize || memcmp(data, ApexBankIds::kMagic, sizeof(ApexBankIds::kMagic)) != 0)
		return fail(reference, "not a bank file");

	const int version = (int)ByteOrder::littleEndianShort(data + 8);
	const int keyId = (int)ByteOrder::littleEndianShort(data + 10);

	if (version != kFormatVersion)
		return fail(reference, "unsupported format version " + String(version));

	auto body = data + kHeaderSize;
	const size_t bodySize = size - (size_t)kHeaderSize;

	{
		auto key = createKey(getProjectKey(), keyId);

		jassert(key.getSize() == ApexBankCipher::kKeySize);

		ApexBankCipher::process(body, bodySize, static_cast<const uint8*>(key.getData()), data + 12, 0);
	}

	if (memcmp(body, ApexBankIds::kCheckWord, sizeof(ApexBankIds::kCheckWord)) != 0)
		return fail(reference, "wrong key or damaged file");

	size_t pos = 4;

	const size_t mapLength = (size_t)ByteOrder::littleEndianInt(body + pos);
	pos += 4;

	if (mapLength > bodySize - minBodySize)
		return fail(reference, "damaged file (map length)");

	auto mapText = String::fromUTF8(reinterpret_cast<const char*>(body + pos), (int)mapLength);
	pos += mapLength;

	const int codec = (int)body[pos];
	pos += 1;

	const uint64 audioLength = ByteOrder::littleEndianInt64(body + pos);
	pos += 8;

	if (codec != kCodecHlacMonolith)
		return fail(reference, "unsupported codec " + String(codec));

	if (audioLength != (uint64)(bodySize - pos - (size_t)kHashSize))
		return fail(reference, "damaged file (audio length)");

	auto audioData = body + pos;

	// The plugin hashes too (2026-10-07). The check word and the lengths above only prove
	// the key and the layout; a damaged byte inside the audio would pass them and reach the
	// HLAC decoder, whose guards are asserts. Costs one SHA-256 over the file per map load.
	{
		SHA256 hash(body, bodySize - (size_t)kHashSize);
		auto raw = hash.getRawData();

		if (raw.getSize() != (size_t)kHashSize || memcmp(raw.getData(), body + bodySize - (size_t)kHashSize, (size_t)kHashSize) != 0)
			return fail(reference, "damaged file (hash mismatch)");
	}

	auto xml = XmlDocument::parse(mapText);

	if (xml == nullptr)
		return fail(reference, "damaged file (map)");

	auto sampleMap = ValueTree::fromXml(*xml);

	if (!sampleMap.isValid())
		return fail(reference, "damaged file (map)");

	// The zones are not put into the XYZ pool: nothing looks them up by name, and the pool
	// would keep every zone of every bank that was ever loaded.
	for (int i = 0; i < sampleMap.getNumChildren(); i++)
	{
		auto s = sampleMap.getChild(i);

		if (!s.hasProperty(MonolithIds::MonolithLength) || !s.hasProperty(MonolithIds::MonolithOffset))
		{
			list.clear();
			return fail(reference, "zone " + String(i) + " has no monolith position");
		}

		// Same as XYZSampleMapProvider::parseValueTree()
		auto d = hise::StreamingHelpers::getBasicMappingDataFromSample(s);
		MultiChannelAudioBuffer::XYZItem mi;

		mi.keyRange = { d.lowKey, d.highKey + 1 };
		mi.veloRange = { d.lowVelocity, d.highVelocity + 1 };
		mi.root = d.rootNote;
		mi.rrGroup = (int)s[SampleIds::RRGroup];
		mi.data = readZone(s, v + "#" + String(i), audioData, (size_t)audioLength);

		parseAdditionalProperties(s, mi);

		list.add(std::move(mi));
	}

	return true;
}

} // namespace hise
