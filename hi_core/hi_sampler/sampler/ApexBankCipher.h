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

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace hise
{

/** The stream cipher used by the APEX bank format (ChaCha20 as described in RFC 8439,
*   256 bit key, 96 bit nonce, 32 bit block counter).
*
*   This file deliberately uses nothing but the standard library so that it can be compiled
*   and checked against the RFC test vectors on its own, outside of a HISE build.
*
*   Encrypting and decrypting are the same operation.
*/
struct ApexBankCipher
{
	static constexpr size_t kKeySize = 32;
	static constexpr size_t kNonceSize = 12;

	/** Processes the data in place.
	*
	*   key must point to kKeySize bytes, nonce to kNonceSize bytes. counter is the index of
	*   the first 64 byte block. A bank file always starts at 0.
	*/
	static void process(uint8_t* data, size_t numBytes, const uint8_t* key, const uint8_t* nonce, uint32_t counter = 0)
	{
		uint32_t state[16];

		// "expand 32-byte k"
		state[0] = 0x61707865;
		state[1] = 0x3320646e;
		state[2] = 0x79622d32;
		state[3] = 0x6b206574;

		for (int i = 0; i < 8; i++)
			state[4 + i] = readLittleEndian(key + 4 * i);

		state[12] = counter;

		for (int i = 0; i < 3; i++)
			state[13 + i] = readLittleEndian(nonce + 4 * i);

		uint8_t keyStream[64];

		while (numBytes > 0)
		{
			createBlock(state, keyStream);
			state[12]++;

			const size_t numThisTime = numBytes < 64 ? numBytes : 64;

			for (size_t i = 0; i < numThisTime; i++)
				data[i] ^= keyStream[i];

			data += numThisTime;
			numBytes -= numThisTime;
		}
	}

private:

	static uint32_t readLittleEndian(const uint8_t* d)
	{
		return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
	}

	static uint32_t rotateLeft(uint32_t v, int numBits)
	{
		return (v << numBits) | (v >> (32 - numBits));
	}

	static void quarterRound(uint32_t* x, int a, int b, int c, int d)
	{
		x[a] += x[b]; x[d] = rotateLeft(x[d] ^ x[a], 16);
		x[c] += x[d]; x[b] = rotateLeft(x[b] ^ x[c], 12);
		x[a] += x[b]; x[d] = rotateLeft(x[d] ^ x[a], 8);
		x[c] += x[d]; x[b] = rotateLeft(x[b] ^ x[c], 7);
	}

	static void createBlock(const uint32_t* state, uint8_t* output)
	{
		uint32_t x[16];
		memcpy(x, state, sizeof(x));

		for (int i = 0; i < 10; i++)
		{
			quarterRound(x, 0, 4, 8, 12);
			quarterRound(x, 1, 5, 9, 13);
			quarterRound(x, 2, 6, 10, 14);
			quarterRound(x, 3, 7, 11, 15);

			quarterRound(x, 0, 5, 10, 15);
			quarterRound(x, 1, 6, 11, 12);
			quarterRound(x, 2, 7, 8, 13);
			quarterRound(x, 3, 4, 9, 14);
		}

		for (int i = 0; i < 16; i++)
		{
			const uint32_t v = x[i] + state[i];

			output[4 * i] = (uint8_t)(v & 0xFF);
			output[4 * i + 1] = (uint8_t)((v >> 8) & 0xFF);
			output[4 * i + 2] = (uint8_t)((v >> 16) & 0xFF);
			output[4 * i + 3] = (uint8_t)((v >> 24) & 0xFF);
		}
	}
};

} // namespace hise
