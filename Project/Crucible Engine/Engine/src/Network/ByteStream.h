/*************************************************************************
* \file:         ByteStream.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                ByteWriter and ByteReader: explicit, bounds-checked,
*                little-endian encoding of integers, floats and varints
*                into a caller-owned buffer.
*
* \details
*                Why not memcpy a struct onto the wire: padding leaks
*                uninitialized bytes, layout and sizeof differ between
*                compilers, adding a field shifts every offset, and a
*                received bool or float can hold an invalid value.
*                Every field is written one by one, in a fixed byte
*                order, with a fixed size, and validated when read.
*
*                Both classes own no memory, never allocate and never
*                throw. Errors are STICKY: the first write that does
*                not fit, or the first read past the end, sets a flag;
*                every later call does nothing (reads return 0) and
*                Ok() stays false. Code can encode or decode a whole
*                message and check Ok() once at the end.
*
*                Bounds checks are written "n > size - pos", which
*                cannot overflow, never "pos + n > size".
*
* \history
*    -v1.0       ByteWriter, ByteReader, LEB128 varints (Networking
*                Guide 8.2); the reader also rejects non-minimal varints
*
**************************************************************************/
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace Crucible
{
	/*********************************************************************
	 \brief
		Writes little-endian values into a fixed buffer it does not own.
	 \details
		A write that does not fit writes nothing, clears Ok() and makes
		every later write a no-op, so Size() never exceeds the buffer.
	 *********************************************************************/
	class ByteWriter
	{
	public:
		explicit ByteWriter(std::span<std::byte> buffer) noexcept : m_buffer(buffer) {}

		void U8(std::uint8_t v) noexcept   { const std::byte b = static_cast<std::byte>(v); Put(&b, 1); }
		void U16(std::uint16_t v) noexcept { PutLE(v, 2); }
		void U32(std::uint32_t v) noexcept { PutLE(v, 4); }
		void U64(std::uint64_t v) noexcept { PutLE(v, 8); }
		void I8(std::int8_t v) noexcept    { U8(static_cast<std::uint8_t>(v)); }
		void I32(std::int32_t v) noexcept  { U32(static_cast<std::uint32_t>(v)); }

		/// The float's exact bits. The reader must check std::isfinite before using it.
		void F32(float v) noexcept { U32(std::bit_cast<std::uint32_t>(v)); }

		/// LEB128: 7 bits per byte, high bit = "more follows". Values below 128 take 1 byte,
		/// below 16384 two bytes, and 2^32 - 1 takes five.
		void VarU32(std::uint32_t v) noexcept
		{
			while (v >= 0x80u)
			{
				U8(static_cast<std::uint8_t>(v | 0x80u));
				v >>= 7;
			}
			U8(static_cast<std::uint8_t>(v));
		}

		/// Raw bytes, unchanged (an already-encoded record, a name).
		void Bytes(std::span<const std::byte> data) noexcept { Put(data.data(), data.size()); }

		/// Reserves one byte (a length known only later) and returns its offset for PatchU8.
		std::size_t ReserveU8() noexcept { const std::size_t at = m_size; U8(0); return at; }
		void PatchU8(std::size_t at, std::uint8_t v) noexcept
		{
			if (m_ok && at < m_size) m_buffer[at] = static_cast<std::byte>(v);
		}
		void PatchU16(std::size_t at, std::uint16_t v) noexcept
		{
			if (!m_ok || at >= m_size || m_size - at < 2) return;
			PatchU8(at, static_cast<std::uint8_t>(v));
			PatchU8(at + 1, static_cast<std::uint8_t>(v >> 8));
		}

		bool                       Ok() const noexcept        { return m_ok; }
		std::size_t                Size() const noexcept      { return m_size; }
		std::size_t                Remaining() const noexcept { return m_buffer.size() - m_size; }
		std::span<const std::byte> Written() const noexcept   { return { m_buffer.data(), m_size }; }

	private:
		void PutLE(std::uint64_t v, std::size_t bytes) noexcept
		{
			std::byte tmp[8];
			for (std::size_t i = 0; i < bytes; ++i) tmp[i] = static_cast<std::byte>((v >> (8 * i)) & 0xFFu);
			Put(tmp, bytes);
		}

		void Put(const std::byte* data, std::size_t n) noexcept
		{
			if (!m_ok || n > m_buffer.size() - m_size) { m_ok = false; return; }   // never writes past the end
			if (n != 0) std::memcpy(m_buffer.data() + m_size, data, n);
			m_size += n;
		}

		std::span<std::byte> m_buffer;
		std::size_t          m_size = 0;
		bool                 m_ok = true;
	};

	/*********************************************************************
	 \brief
		Reads little-endian values from a byte span it does not own.
	 \details
		A read past the end returns 0 and clears Ok() for good. A decoder
		reads every field, then checks Ok() and AtEnd() once: trailing
		bytes are as malformed as missing ones.
	 *********************************************************************/
	class ByteReader
	{
	public:
		explicit ByteReader(std::span<const std::byte> data) noexcept : m_data(data) {}

		std::uint8_t  U8() noexcept  { std::byte b{}; Take(&b, 1); return static_cast<std::uint8_t>(b); }
		std::uint16_t U16() noexcept { return static_cast<std::uint16_t>(TakeLE(2)); }
		std::uint32_t U32() noexcept { return static_cast<std::uint32_t>(TakeLE(4)); }
		std::uint64_t U64() noexcept { return TakeLE(8); }
		std::int8_t   I8() noexcept  { return static_cast<std::int8_t>(U8()); }
		std::int32_t  I32() noexcept { return static_cast<std::int32_t>(U32()); }

		/// The float's exact bits: may be NaN or infinite. The caller checks std::isfinite.
		float F32() noexcept { return std::bit_cast<float>(U32()); }

		/// LEB128 written by ByteWriter::VarU32. Malformed (Ok() false, returns 0) if it
		/// needs more than 32 bits, more than 5 bytes, or more bytes than the value needs
		/// (e.g. 0x80 0x00 for 0): one value has exactly one valid encoding.
		std::uint32_t VarU32() noexcept
		{
			std::uint32_t value = 0;
			for (int shift = 0; shift < 35; shift += 7)
			{
				const std::uint8_t b = U8();
				if (!m_ok) return 0;
				if (shift == 28 && (b & 0xF0u) != 0) { m_ok = false; return 0; }   // bits above 32, or a 6th byte
				value |= static_cast<std::uint32_t>(b & 0x7Fu) << shift;
				if ((b & 0x80u) == 0)
				{
					if (b == 0 && shift != 0) { m_ok = false; return 0; }          // non-minimal (overlong)
					return value;
				}
			}
			m_ok = false;
			return 0;
		}

		void Skip(std::size_t n) noexcept
		{
			if (!m_ok || n > Remaining()) { m_ok = false; return; }
			m_pos += n;
		}

		/// A bounded view of the next n bytes (one record's body), consumed from this reader.
		/// The view points into the original data, so it lives no longer than that data.
		std::span<const std::byte> Sub(std::size_t n) noexcept
		{
			if (!m_ok || n > Remaining()) { m_ok = false; return {}; }
			const std::span<const std::byte> view = m_data.subspan(m_pos, n);
			m_pos += n;
			return view;
		}

		bool        Ok() const noexcept        { return m_ok; }
		bool        AtEnd() const noexcept     { return m_pos == m_data.size(); }
		std::size_t Remaining() const noexcept { return m_data.size() - m_pos; }

	private:
		std::uint64_t TakeLE(std::size_t bytes) noexcept
		{
			std::byte tmp[8]{};
			Take(tmp, bytes);
			std::uint64_t v = 0;
			for (std::size_t i = 0; i < bytes; ++i) v |= static_cast<std::uint64_t>(tmp[i]) << (8 * i);
			return v;
		}

		void Take(std::byte* out, std::size_t n) noexcept
		{
			if (!m_ok || n > m_data.size() - m_pos) { m_ok = false; std::memset(out, 0, n); return; }
			if (n != 0) std::memcpy(out, m_data.data() + m_pos, n);
			m_pos += n;
		}

		std::span<const std::byte> m_data;
		std::size_t                m_pos = 0;
		bool                       m_ok = true;
	};
}
