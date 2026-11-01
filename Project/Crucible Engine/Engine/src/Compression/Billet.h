/*!
* @file			Billet.h
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		Billet: the engine's own lossless packer, for files that are stored on disk
*				smaller and unpacked when loaded (no external library, so the game may use it).
*
* In metalworking a billet is a bar of metal cast for storage and shaped later. Here the
* compilers pack data into a billet (Compress, offline), and the engine unpacks it at load
* (Decompress) into exactly the same bytes - nothing is lost.
*
* How it packs (the same two ideas as zip/deflate):
*   1. LZ77: a run of bytes that already appeared earlier is replaced by
*      "copy <length> bytes from <distance> back".
*   2. Huffman codes: common symbols (bytes, lengths, distances) get short bit codes,
*      rare ones long codes, so the result takes fewer bits in total.
* The data is cut into 1 MiB chunks, each packed on its own; a chunk that does not get
* smaller is stored as it is.
*
* Layout, little-endian:
*   u32 magic "BLLT"   u32 version   u64 unpacked size   u64 FNV-1a hash of the unpacked bytes
*   u32 chunk count, then per chunk: u32 unpacked size, u32 stored size, u8 mode (0 stored, 1 packed), bytes
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "EngineExport.h"

namespace Crucible::Billet {

	/// Packs `raw`. Always succeeds; incompressible data costs a few bytes of headers.
	ENGINE_API std::vector<unsigned char> Compress(std::span<const unsigned char> raw);

	/// Unpacks a Billet stream into `out`. False with `error` set if the stream is damaged,
	/// would unpack to more than `maxSize` bytes, or does not match its recorded hash.
	ENGINE_API bool Decompress(std::span<const unsigned char> packed, std::vector<unsigned char>& out,
	                std::string& error, std::size_t maxSize);

}
