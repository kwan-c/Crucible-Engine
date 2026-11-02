/*************************************************************************
* \file:         CanonicalHash.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                64-bit FNV-1a over a canonical byte string: the hash
*                two builds compare at join time to prove they encode
*                and decode the same way.
*
* \details
*                "Canonical" means the same logical content always
*                produces the same bytes, on every machine and compiler:
*                integers are fed little-endian byte by byte (never as
*                raw memory), and strings are length-prefixed so
*                ("ab", "c") and ("a", "bc") hash differently.
*
*                The session (later phases) feeds it kProtocolVersion,
*                the tick constants, every serializer and every network
*                prefab, in id order. A client whose hash differs is
*                refused with ProtocolMismatch instead of mis-decoding.
*
*                FNV-1a is not cryptographic. It detects accidental
*                mismatches between honest builds, which is its only
*                job here.
*
* \history
*    -v1.0       FNV-1a 64 with U8/U32/U64/Str (Networking Guide 18.3,
*                issue 7); U8 is public so the algorithm can be checked
*                against the published FNV test vectors
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <string_view>

namespace Crucible
{
	/*********************************************************************
	 \brief
		Accumulates a 64-bit FNV-1a hash of values fed in a fixed order.
	 \details
		Order matters: the caller feeds fields in one documented order
		(for example by ascending id), never in container iteration
		order that could differ between builds.
	 *********************************************************************/
	class CanonicalHash
	{
	public:
		void U8(std::uint8_t b) noexcept { m_hash = (m_hash ^ b) * kPrime; }
		void U32(std::uint32_t v) noexcept { for (int i = 0; i < 4; ++i) U8(static_cast<std::uint8_t>(v >> (8 * i))); }
		void U64(std::uint64_t v) noexcept { for (int i = 0; i < 8; ++i) U8(static_cast<std::uint8_t>(v >> (8 * i))); }

		/// Length (u32) first, then the bytes.
		void Str(std::string_view s) noexcept
		{
			U32(static_cast<std::uint32_t>(s.size()));
			for (const char c : s) U8(static_cast<std::uint8_t>(c));
		}

		std::uint64_t Value() const noexcept { return m_hash; }

		static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;   // FNV-1a 64
		static constexpr std::uint64_t kPrime       = 1099511628211ull;

	private:
		std::uint64_t m_hash = kOffsetBasis;
	};
}
