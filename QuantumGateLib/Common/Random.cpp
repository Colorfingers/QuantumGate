// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "stdafx.h"
#include "Random.h"

#include <algorithm>
#include <limits>

#ifndef NO_PCG_RANDOM
#pragma message("Using PCG as random number generator")
#else
#pragma message("Using Mersenne Twister 19937 as random number generator (slower!)")
#endif

namespace QuantumGate::Implementation
{
	Buffer Random::GetPseudoRandomBytes(const Size count)
	{
		if (count == 0) return Buffer();

		GetRngEngine().CheckSeed64(count);

		// How many 64bit integers do we need for the number of requested bytes?
		const auto num = static_cast<unsigned int>(std::ceil(static_cast<float>(count) /
															 static_cast<float>(sizeof(UInt64))));

		assert(count <= (num * sizeof(UInt64)));

		Buffer bytes(num * sizeof(UInt64));
		auto uint64_ptr = reinterpret_cast<UInt64*>(bytes.GetBytes());

		// Generate random 64bit integers
		std::generate(uint64_ptr, uint64_ptr + num,
					  [&]() -> UInt64
		{
			return (GetRngEngine().Rng64)();
		});
		
		bytes.Resize(count);

		return std::move(bytes);
	}
}