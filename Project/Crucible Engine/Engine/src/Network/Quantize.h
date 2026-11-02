/*************************************************************************
* \file:         Quantize.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Quantization: turns floats into small integers with a
*                known precision before they go on the wire, and back.
*
* \details
*                Quantizing does two jobs:
*                  - Size. A float axis is 4 bytes; a movement axis fits
*                    in 1, an angle in 2, a whole rotation in 4.
*                  - Validation. An integer cannot be NaN or infinite,
*                    so whatever the receiver decodes is a usable value.
*
*                Every encoder first replaces NaN or infinity with a safe
*                value (Finite). std::clamp, std::fmod and std::lround do
*                NOT do that: clamp(NaN) is NaN and lround(NaN) is
*                unspecified. A NaN can come from our own simulation
*                (a physics blow-up, normalizing a zero vector), so the
*                sender must never put one on the wire.
*
*                Precision (one step):
*                  SignedUnitToI8    1/127        (movement axes)
*                  AngleToU16        360/65536 deg (0.0055 deg)
*                  PositionToFixed   1/256 m      (3.9 mm), +-8388 km
*                  PackQuat          about 0.0014 per component
*                Decoding rounds, so the error is at most half a step
*                (PackQuat: see NetQuantizeTests for the measured angle).
*
* \history
*    -v1.0       Finite, I8 axes, U16 angles, fixed-point positions, yaw
*                helpers, smallest-three quaternions (Networking Guide 8.3)
*
**************************************************************************/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Math/Math.h"

namespace Crucible::Quantize
{
	/// v if it is a real number, otherwise fallback (NaN and +-infinity both become fallback).
	inline float Finite(float v, float fallback = 0.f) noexcept { return std::isfinite(v) ? v : fallback; }

	/// -1..1 to a signed byte (movement axes): -127..127. Out-of-range input is clamped.
	inline std::int8_t SignedUnitToI8(float v) noexcept
	{
		return static_cast<std::int8_t>(std::lround(std::clamp(Finite(v), -1.f, 1.f) * 127.f));
	}

	/// Inverse of SignedUnitToI8. -128 (never sent, but a byte can hold it) reads as -1.
	inline float I8ToSignedUnit(std::int8_t q) noexcept { return std::max(-1.f, static_cast<float>(q) / 127.f); }

	/// Any angle in degrees to 16 bits, wrapped into [0, 360). Negative input wraps correctly.
	inline std::uint16_t AngleToU16(float degrees) noexcept
	{
		float t = std::fmod(Finite(degrees), 360.f);
		if (t < 0.f) t += 360.f;
		// 359.999 rounds to 65536, which & 0xFFFF wraps to 0 = 0 degrees: the same angle.
		return static_cast<std::uint16_t>(static_cast<std::uint32_t>(std::lround(t * (65536.f / 360.f))) & 0xFFFFu);
	}

	/// Inverse of AngleToU16: [0, 360).
	inline float U16ToAngle(std::uint16_t q) noexcept { return static_cast<float>(q) * (360.f / 65536.f); }

	/// Fixed point, 1/256 m steps. int32 covers +-8,388 km; anything further is clamped.
	inline constexpr float kPositionScale = 256.f;

	inline std::int32_t PositionToFixed(float metres) noexcept
	{
		constexpr float kLimit = 2147483520.f / kPositionScale;   // the largest float below INT32_MAX, / 256
		return static_cast<std::int32_t>(std::lround(std::clamp(Finite(metres), -kLimit, kLimit) * kPositionScale));
	}

	inline float FixedToPosition(std::int32_t q) noexcept { return static_cast<float>(q) / kPositionScale; }

	/// Yaw of an upright rotation in degrees: the angle of its +Z axis around world +Y,
	/// in (-180, 180]. (Not glm::yaw: that uses asin and only returns -90..90.)
	inline float YawDegrees(const Math::quat& q) noexcept
	{
		const Math::vec3 forward = q * Math::vec3(0.f, 0.f, 1.f);
		return glm::degrees(std::atan2(forward.x, forward.z));
	}

	/// A rotation of `degrees` around world +Y.
	inline Math::quat YawRotation(float degrees) noexcept
	{
		return glm::angleAxis(glm::radians(Finite(degrees)), Math::vec3(0.f, 1.f, 0.f));
	}

	// "Smallest three": a unit quaternion's components satisfy x^2 + y^2 + z^2 + w^2 = 1, so the
	// largest one can be recomputed from the other three. Store its index in 2 bits and the
	// other three in 10 bits each (2 + 30 = 32). The other three are at most 1/sqrt(2) in size.
	inline constexpr float kQuatRange = 0.70710678f;

	/// Any quaternion to 32 bits. Normalizes first; NaN, infinite or zero-length input
	/// becomes the identity. q and -q (the same rotation) pack identically.
	inline std::uint32_t PackQuat(Math::quat q) noexcept
	{
		const float lengthSquared = glm::dot(q, q);
		if (!std::isfinite(lengthSquared) || lengthSquared < 1e-12f) q = Math::quat(1.f, 0.f, 0.f, 0.f);   // identity
		else q = q * (1.f / std::sqrt(lengthSquared));

		const float c[4] = { q.x, q.y, q.z, q.w };
		int largest = 0;
		for (int i = 1; i < 4; ++i)
			if (std::fabs(c[i]) > std::fabs(c[largest])) largest = i;
		const float sign = c[largest] < 0.f ? -1.f : 1.f;   // make the dropped component positive

		std::uint32_t packed = static_cast<std::uint32_t>(largest) << 30;
		int shift = 20;
		for (int i = 0; i < 4; ++i)
		{
			if (i == largest) continue;
			const float v = std::clamp(c[i] * sign / kQuatRange, -1.f, 1.f);
			packed |= static_cast<std::uint32_t>(std::lround((v * 0.5f + 0.5f) * 1023.f)) << shift;
			shift -= 10;
		}
		return packed;
	}

	/// Inverse of PackQuat. Every 32-bit value decodes to a finite unit quaternion: no
	/// field can be exactly 0, so the vector being normalized is never zero.
	inline Math::quat UnpackQuat(std::uint32_t packed) noexcept
	{
		const int largest = static_cast<int>(packed >> 30);
		float c[4]{};
		float sum = 0.f;
		int shift = 20;
		for (int i = 0; i < 4; ++i)
		{
			if (i == largest) continue;
			c[i] = (static_cast<float>((packed >> shift) & 1023u) / 1023.f * 2.f - 1.f) * kQuatRange;
			sum += c[i] * c[i];
			shift -= 10;
		}
		c[largest] = std::sqrt(std::max(0.f, 1.f - sum));
		return glm::normalize(Math::quat(c[3], c[0], c[1], c[2]));   // glm::quat takes (w, x, y, z)
	}
}
