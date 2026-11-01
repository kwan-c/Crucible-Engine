/*!
* @file			Billet.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		Billet packing and unpacking (see Billet.h).
*
* A packed chunk is one bit stream, read lowest bit first:
*   1. Two Huffman code-length tables, 4 bits per symbol (0 = symbol unused):
*      the literal/length alphabet (LitLenSymbols), then the distance alphabet (DistSymbols).
*   2. Tokens until the end-of-chunk symbol:
*        0..255     one literal byte
*        256        end of chunk
*        257+code   a match; its length and then its distance are "value codes" (ValueCode):
*                   a Huffman-coded bucket followed by a few raw extra bits.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "Compression/Billet.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>

namespace Crucible::Billet {

	namespace {

		constexpr uint32_t Magic        = 0x544C4C42u;   // "BLLT" read as a little-endian u32
		constexpr uint32_t Version      = 1;
		constexpr size_t   HeaderSize   = 28;
		constexpr size_t   ChunkHeader  = 9;
		constexpr uint32_t ChunkSize    = 1u << 20;      // also the furthest a match can reach back

		constexpr uint32_t MinMatch     = 4;
		constexpr uint32_t MaxMatch     = MinMatch + 65531;   // length - MinMatch stays below 2^16
		constexpr uint32_t MaxChain     = 48;            // match candidates tried per position (speed vs size)
		constexpr uint32_t HashBits     = 16;

		constexpr uint32_t EndOfChunk   = 256;
		constexpr uint32_t LengthCodes  = 32;            // value codes for 0 .. 2^16-1
		constexpr uint32_t LitLenSymbols = 257 + LengthCodes;
		constexpr uint32_t DistSymbols  = 40;            // value codes for 0 .. 2^20-1
		constexpr int      MaxCodeBits  = 15;

		uint64_t Hash(const unsigned char* bytes, size_t size) {
			uint64_t hash = 1469598103934665603ull;   // FNV-1a, same convention as AssetRegistry
			for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
			return hash;
		}

		void Put32(std::vector<unsigned char>& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(v >> (8 * i))); }
		void Put64(std::vector<unsigned char>& out, uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<unsigned char>(v >> (8 * i))); }
		uint32_t Get32(const unsigned char* p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t{ p[i] } << (8 * i); return v; }
		uint64_t Get64(const unsigned char* p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= uint64_t{ p[i] } << (8 * i); return v; }

		// ---- Value codes: a number as a small "bucket" code plus raw extra bits ------------------
		// 0..3 are their own codes. Above that, each power of two is split into two halves:
		// code 2n + (second-highest bit), with n-1 extra bits for the rest.
		struct ValueCode { uint32_t code, extraBits, extra; };

		ValueCode EncodeValue(uint32_t v) {
			if (v < 4) return { v, 0, 0 };
			const uint32_t n  = static_cast<uint32_t>(std::bit_width(v)) - 1;
			const uint32_t eb = n - 1;
			return { 2 * n + ((v >> eb) & 1u), eb, v & ((1u << eb) - 1) };
		}

		uint32_t ValueBase(uint32_t code, uint32_t& extraBits) {
			if (code < 4) { extraBits = 0; return code; }
			const uint32_t n = code / 2;
			extraBits = n - 1;
			return (1u << n) | ((code & 1u) << (n - 1));
		}

		// ---- Bits ---------------------------------------------------------------------------------

		struct BitWriter {
			std::vector<unsigned char>& out;
			uint64_t buffer = 0;
			int      count  = 0;
			void Put(uint32_t bits, int n) {
				buffer |= uint64_t{ bits } << count;
				count += n;
				while (count >= 8) { out.push_back(static_cast<unsigned char>(buffer)); buffer >>= 8; count -= 8; }
			}
			void Flush() { if (count > 0) { out.push_back(static_cast<unsigned char>(buffer)); buffer = 0; count = 0; } }
		};

		// Reads past the end as zero bits, but remembers it: Overrun() then fails the chunk.
		struct BitReader {
			const unsigned char* p;
			const unsigned char* end;
			uint64_t buffer   = 0;
			int      count    = 0;
			size_t   phantom  = 0;   // zero bytes made up past the end
			void Refill() {
				while (count <= 56) {
					uint64_t byte = 0;
					if (p < end) byte = *p++; else ++phantom;
					buffer |= byte << count;
					count += 8;
				}
			}
			uint32_t Peek(int n) { if (count < n) Refill(); return static_cast<uint32_t>(buffer & ((uint64_t{ 1 } << n) - 1)); }
			void     Skip(int n) { buffer >>= n; count -= n; }
			uint32_t Get(int n)  { if (n == 0) return 0; const uint32_t v = Peek(n); Skip(n); return v; }
			// Every bit used came from the stream (the made-up ones were never consumed).
			bool Overrun() const { return phantom * 8 > static_cast<size_t>(count); }
		};

		// ---- Huffman codes ------------------------------------------------------------------------

		// Code lengths (0 = unused) for these symbol counts, none longer than MaxCodeBits.
		std::vector<uint8_t> BuildLengths(std::vector<uint32_t> freq) {
			const size_t n = freq.size();
			std::vector<uint8_t> lengths(n, 0);
			std::vector<uint32_t> used;
			for (uint32_t s = 0; s < n; ++s) if (freq[s]) used.push_back(s);
			if (used.empty()) return lengths;
			if (used.size() == 1) { lengths[used[0]] = 1; return lengths; }

			for (;;) {
				// Classic Huffman tree: keep joining the two lightest nodes.
				struct Node { uint64_t weight; int32_t parent; };
				std::vector<Node> nodes;
				using Item = std::pair<uint64_t, uint32_t>;   // weight, node index
				std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
				for (uint32_t s : used) { queue.push({ freq[s], static_cast<uint32_t>(nodes.size()) }); nodes.push_back({ freq[s], -1 }); }
				while (queue.size() > 1) {
					const Item a = queue.top(); queue.pop();
					const Item b = queue.top(); queue.pop();
					const uint32_t parent = static_cast<uint32_t>(nodes.size());
					nodes.push_back({ a.first + b.first, -1 });
					nodes[a.second].parent = static_cast<int32_t>(parent);
					nodes[b.second].parent = static_cast<int32_t>(parent);
					queue.push({ a.first + b.first, parent });
				}
				int deepest = 0;
				for (size_t i = 0; i < used.size(); ++i) {
					int depth = 0;
					for (int32_t at = nodes[i].parent; at >= 0; at = nodes[static_cast<size_t>(at)].parent) ++depth;
					lengths[used[i]] = static_cast<uint8_t>(std::min(depth, 255));
					deepest = std::max(deepest, depth);
				}
				if (deepest <= MaxCodeBits) return lengths;
				// Too deep: flatten the counts (rare symbols get relatively commoner) and try again.
				for (uint32_t s : used) freq[s] = (freq[s] + 1) / 2;
			}
		}

		// Canonical codes from lengths, bit-reversed so they can be written lowest bit first.
		std::vector<uint16_t> BuildCodes(const std::vector<uint8_t>& lengths) {
			uint32_t countPerLength[MaxCodeBits + 1] = {};
			for (uint8_t l : lengths) if (l) ++countPerLength[l];
			uint32_t next[MaxCodeBits + 2] = {};
			for (int bits = 1, code = 0; bits <= MaxCodeBits; ++bits) {
				code = (code + static_cast<int>(countPerLength[bits - 1])) << 1;
				next[bits] = static_cast<uint32_t>(code);
			}
			std::vector<uint16_t> codes(lengths.size(), 0);
			for (size_t s = 0; s < lengths.size(); ++s) {
				const int l = lengths[s];
				if (!l) continue;
				uint32_t code = next[l]++, reversed = 0;
				for (int i = 0; i < l; ++i) { reversed = (reversed << 1) | (code & 1u); code >>= 1; }
				codes[s] = static_cast<uint16_t>(reversed);
			}
			return codes;
		}

		// Decoding table: index = the next MaxCodeBits bits, entry = (symbol << 4) | length (0 = invalid).
		void BuildTable(const std::vector<uint8_t>& lengths, std::vector<uint16_t>& table) {
			table.assign(size_t{ 1 } << MaxCodeBits, 0);
			const std::vector<uint16_t> codes = BuildCodes(lengths);
			for (size_t s = 0; s < lengths.size(); ++s) {
				const uint32_t l = lengths[s];
				if (!l) continue;
				for (uint32_t i = codes[s]; i < table.size(); i += 1u << l)
					table[i] = static_cast<uint16_t>((s << 4) | l);
			}
		}

		bool DecodeSymbol(BitReader& bits, const std::vector<uint16_t>& table, uint32_t& symbol) {
			const uint16_t entry = table[bits.Peek(MaxCodeBits)];
			if ((entry & 15u) == 0) return false;
			bits.Skip(entry & 15u);
			symbol = entry >> 4;
			return true;
		}

		// ---- One chunk ----------------------------------------------------------------------------

		struct Token { uint32_t length; uint32_t distance; };   // distance 0: literal byte `length`

		uint32_t Load32(const unsigned char* p) { return uint32_t{ p[0] } | uint32_t{ p[1] } << 8 | uint32_t{ p[2] } << 16 | uint32_t{ p[3] } << 24; }
		uint32_t HashAt(const unsigned char* p) { return (Load32(p) * 2654435761u) >> (32 - HashBits); }

		std::vector<Token> FindTokens(const unsigned char* data, uint32_t size) {
			std::vector<int32_t> head(size_t{ 1 } << HashBits, -1);
			std::vector<int32_t> previous(size, -1);
			auto insert = [&](uint32_t pos) {
				if (pos + MinMatch > size) return;
				const uint32_t h = HashAt(data + pos);
				previous[pos] = head[h];
				head[h] = static_cast<int32_t>(pos);
			};
			// Longest earlier match for `pos` (before `pos` itself is inserted).
			auto longest = [&](uint32_t pos, uint32_t& bestDistance) {
				uint32_t best = 0;
				if (pos + MinMatch > size) return best;
				const uint32_t limit = std::min(MaxMatch, size - pos);
				int32_t candidate = head[HashAt(data + pos)];
				for (uint32_t tries = 0; candidate >= 0 && tries < MaxChain; ++tries, candidate = previous[static_cast<size_t>(candidate)]) {
					const unsigned char* a = data + candidate;
					const unsigned char* b = data + pos;
					if (a[best] != b[best]) continue;   // cannot beat the best so far
					uint32_t length = 0;
					while (length < limit && a[length] == b[length]) ++length;
					if (length > best) { best = length; bestDistance = pos - static_cast<uint32_t>(candidate); if (best == limit) break; }
				}
				return best >= MinMatch ? best : 0;
			};

			std::vector<Token> tokens;
			tokens.reserve(size / 2);
			uint32_t pos = 0;
			while (pos < size) {
				uint32_t distance = 0;
				uint32_t length = longest(pos, distance);
				// Lazy matching: if the next position has a longer match, take a literal first.
				if (length && length < 64 && pos + 1 < size) {
					insert(pos);
					uint32_t nextDistance = 0;
					const uint32_t next = longest(pos + 1, nextDistance);
					if (next > length) {
						tokens.push_back({ data[pos], 0 });
						++pos;
						length = next; distance = nextDistance;
					} else {
						for (uint32_t i = 1; i < length; ++i) insert(pos + i);
						tokens.push_back({ length, distance });
						pos += length;
						continue;
					}
				}
				if (length) {
					for (uint32_t i = 0; i < length; ++i) insert(pos + i);
					tokens.push_back({ length, distance });
					pos += length;
				} else {
					insert(pos);
					tokens.push_back({ data[pos], 0 });
					++pos;
				}
			}
			return tokens;
		}

		std::vector<unsigned char> PackChunk(const unsigned char* data, uint32_t size) {
			const std::vector<Token> tokens = FindTokens(data, size);

			std::vector<uint32_t> litLenFreq(LitLenSymbols, 0), distFreq(DistSymbols, 0);
			for (const Token& t : tokens) {
				if (t.distance == 0) { ++litLenFreq[t.length]; continue; }
				++litLenFreq[257 + EncodeValue(t.length - MinMatch).code];
				++distFreq[EncodeValue(t.distance - 1).code];
			}
			++litLenFreq[EndOfChunk];

			const std::vector<uint8_t>  litLenLengths = BuildLengths(litLenFreq), distLengths = BuildLengths(distFreq);
			const std::vector<uint16_t> litLenCodes   = BuildCodes(litLenLengths), distCodes   = BuildCodes(distLengths);

			std::vector<unsigned char> out;
			out.reserve(size / 2);
			BitWriter bits{ out };
			for (uint8_t l : litLenLengths) bits.Put(l, 4);
			for (uint8_t l : distLengths)   bits.Put(l, 4);
			for (const Token& t : tokens) {
				if (t.distance == 0) { bits.Put(litLenCodes[t.length], litLenLengths[t.length]); continue; }
				const ValueCode len = EncodeValue(t.length - MinMatch), dist = EncodeValue(t.distance - 1);
				bits.Put(litLenCodes[257 + len.code], litLenLengths[257 + len.code]);
				bits.Put(len.extra, static_cast<int>(len.extraBits));
				bits.Put(distCodes[dist.code], distLengths[dist.code]);
				bits.Put(dist.extra, static_cast<int>(dist.extraBits));
			}
			bits.Put(litLenCodes[EndOfChunk], litLenLengths[EndOfChunk]);
			bits.Flush();
			return out;
		}

		bool UnpackChunk(const unsigned char* in, size_t inSize, unsigned char* out, uint32_t outSize, std::string& error) {
			BitReader bits{ in, in + inSize };
			std::vector<uint8_t> litLenLengths(LitLenSymbols), distLengths(DistSymbols);
			for (uint8_t& l : litLenLengths) l = static_cast<uint8_t>(bits.Get(4));
			for (uint8_t& l : distLengths)   l = static_cast<uint8_t>(bits.Get(4));
			std::vector<uint16_t> litLenTable, distTable;
			BuildTable(litLenLengths, litLenTable);
			BuildTable(distLengths, distTable);

			uint32_t pos = 0;
			for (;;) {
				uint32_t symbol = 0;
				if (!DecodeSymbol(bits, litLenTable, symbol)) { error = "invalid code"; return false; }
				if (symbol < 256) {
					if (pos >= outSize) { error = "more data than recorded"; return false; }
					out[pos++] = static_cast<unsigned char>(symbol);
					continue;
				}
				if (symbol == EndOfChunk) break;

				// ValueBase sets extraBits: always call it in a statement of its own, BEFORE reading
				// the extra bits - inside one expression C++ may read extraBits first.
				uint32_t extraBits = 0;
				const uint32_t lengthBase = ValueBase(symbol - 257, extraBits) + MinMatch;
				const uint32_t matchLength = lengthBase + bits.Get(static_cast<int>(extraBits));
				uint32_t distSymbol = 0;
				if (!DecodeSymbol(bits, distTable, distSymbol)) { error = "invalid distance code"; return false; }
				const uint32_t distanceBase = ValueBase(distSymbol, extraBits) + 1;
				const uint32_t distance = distanceBase + bits.Get(static_cast<int>(extraBits));
				if (distance > pos || matchLength > outSize - pos) { error = "copy out of range"; return false; }
				for (uint32_t i = 0; i < matchLength; ++i, ++pos) out[pos] = out[pos - distance];   // may overlap: byte by byte
			}
			if (bits.Overrun()) { error = "truncated"; return false; }
			if (pos != outSize)  { error = "less data than recorded"; return false; }
			return true;
		}

	}

	std::vector<unsigned char> Compress(std::span<const unsigned char> raw) {
		std::vector<unsigned char> out;
		Put32(out, Magic);
		Put32(out, Version);
		Put64(out, raw.size());
		Put64(out, Hash(raw.data(), raw.size()));
		const uint32_t chunks = static_cast<uint32_t>((raw.size() + ChunkSize - 1) / ChunkSize);
		Put32(out, chunks);

		for (uint32_t c = 0; c < chunks; ++c) {
			const size_t   begin = size_t{ c } * ChunkSize;
			const uint32_t size  = static_cast<uint32_t>(std::min<size_t>(ChunkSize, raw.size() - begin));
			const std::vector<unsigned char> packed = PackChunk(raw.data() + begin, size);
			const bool stored = packed.size() >= size;
			Put32(out, size);
			Put32(out, stored ? size : static_cast<uint32_t>(packed.size()));
			out.push_back(stored ? 0 : 1);
			if (stored) out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(begin), raw.begin() + static_cast<std::ptrdiff_t>(begin + size));
			else        out.insert(out.end(), packed.begin(), packed.end());
		}
		return out;
	}

	bool Decompress(std::span<const unsigned char> packed, std::vector<unsigned char>& out,
	                std::string& error, std::size_t maxSize) {
		const auto fail = [&](const std::string& why) { error = "Billet: " + why; return false; };
		if (packed.size() < HeaderSize || Get32(packed.data()) != Magic) return fail("not a Billet stream");
		if (Get32(packed.data() + 4) != Version) return fail("unknown version " + std::to_string(Get32(packed.data() + 4)));
		const uint64_t rawSize = Get64(packed.data() + 8);
		const uint64_t rawHash = Get64(packed.data() + 16);
		const uint32_t chunks  = Get32(packed.data() + 24);
		if (rawSize > maxSize) return fail("unpacks to more than allowed");
		if (chunks != (rawSize + ChunkSize - 1) / ChunkSize) return fail("bad chunk count");

		std::vector<unsigned char> result(static_cast<size_t>(rawSize));
		size_t at = HeaderSize, written = 0;
		for (uint32_t c = 0; c < chunks; ++c) {
			if (packed.size() - at < ChunkHeader) return fail("truncated chunk header");
			const uint32_t size   = Get32(packed.data() + at);
			const uint32_t stored = Get32(packed.data() + at + 4);
			const uint8_t  mode   = packed[at + 8];
			at += ChunkHeader;
			const uint32_t expected = static_cast<uint32_t>(std::min<uint64_t>(ChunkSize, rawSize - written));
			if (size != expected || stored > packed.size() - at) return fail("bad chunk size");
			if (mode == 0) {
				if (stored != size) return fail("bad stored chunk");
				std::copy_n(packed.data() + at, size, result.data() + written);
			} else if (mode == 1) {
				std::string why;
				if (!UnpackChunk(packed.data() + at, stored, result.data() + written, size, why)) return fail(why);
			} else {
				return fail("unknown chunk mode");
			}
			at += stored;
			written += size;
		}
		if (at != packed.size()) return fail("unexpected bytes after the last chunk");
		if (Hash(result.data(), result.size()) != rawHash) return fail("data is damaged (hash mismatch)");
		out = std::move(result);
		return true;
	}

}
