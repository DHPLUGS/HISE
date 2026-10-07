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

#pragma once

namespace hise { using namespace juce;

/** Loads a "bank": one encrypted file that holds a sample map and the monolith audio it
*   points to. Only compiled with HISE_APEX_BANK_FORMAT.
*
*   It is registered next to the "SampleMap" and "SFZ" providers, so a reference looks like
*
*       {XYZ::Bank}FACTORY/BASS/ANTEEFA
*
*   and resolves to <Samples>/FACTORY/BASS/ANTEEFA.apxbk. Nothing about the bank lives in
*   the project's sample map pool, so banks can be added or removed without an export.
*
*   The audio inside is a HISE monolith (the bytes of a .ch1 file) and is read with the same
*   reader the sample map provider uses. The zones are built by the same code too
*   (parseAdditionalProperties), so a bank sounds exactly like the map it was packed from.
*   The existing sample map and monolith loading paths are not touched by this class.
*
*   File layout, all numbers little-endian:
*
*       offset  size  content
*       0       8     "APEXBANK"
*       8       2     format version (kFormatVersion)
*       10      2     key id (0 = the key derived from the project's encryption key)
*       12      12    nonce
*       24      ...   body, encrypted with ApexBankCipher (block counter starts at 0)
*
*   Body, after decrypting:
*
*       4     "APOK" (a wrong key or a damaged file fails here)
*       4     length of the map text
*       n     the <samplemap> XML as UTF-8, without the FileName attributes
*       1     codec (kCodecHlacMonolith)
*       8     length of the audio data
*       n     audio data
*       32    SHA-256 of all body bytes before this field
*
*   The key is never stored in this repository. It is derived from the project's
*   EncryptionKey setting (see createKey()).
*/
struct XYZBankProvider : public XYZSampleMapProvider
{
	static constexpr int kFormatVersion = 1;
	static constexpr int kCodecHlacMonolith = 1;
	static constexpr int kHeaderSize = 24;
	static constexpr int kHashSize = 32;

	XYZBankProvider(MainController* mc);

	static Identifier getStaticId();
	Identifier getId() const override;

	/** Reads the bank and fills the list with its zones. Returns false (and leaves the list
	*   empty) if the file is missing, damaged or was packed with another key.
	*/
	bool parse(const String& v, MultiChannelAudioBuffer::XYZItem::List& list) override;

	/** Why the last parse() returned false ("Bank <reference>: <reason>"), empty after a good one. */
	String getLastError() const override;

	/** The file extension including the dot. */
	static String getFileExtension();

	/** Resolves a reference (the part after the wildcard) to a file inside the sample folder.
	*   Returns a non-existent file for a reference that points outside of that folder.
	*/
	File getBankFile(const String& reference) const;

	/** Derives the 32 byte cipher key: SHA-256 of "APXBK1:<projectKey>:<keyId>" (UTF-8). */
	static MemoryBlock createKey(const String& projectKey, int keyId);

private:

	/** The project's EncryptionKey setting (backend) or its compiled-in copy (frontend). */
	String getProjectKey() const;

	/** Reads the audio of one zone exactly like MonolithDataProvider::loadFile() does. */
	MultiChannelAudioBuffer::SampleReference::Ptr readZone(const ValueTree& sample, const String& zoneReference,
		const void* audioData, size_t audioSize) const;

	bool fail(const String& reference, const String& reason) const;

	mutable String lastError;
};

} // namespace hise
