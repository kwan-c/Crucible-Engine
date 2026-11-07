/*!
* @file			ModelBuild.h
*
* @brief		.glb -> .cmodel, shared by CrucibleModelCompiler (every model, at build time) and
*				the editor's Asset Browser (one model, as soon as it appears in Assets), so both
*				write byte-identical files and the build sees the editor's output as up to date.
*
* Uses GLTFLoader and TextureBuild, which belong to tools and the editor only: compile
* ModelBuild.cpp into those, never into Engine.dll or the game.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "Resources/ModelResource.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Crucible::ModelBuild {

	struct Output {
		std::vector<unsigned char> bytes;   ///< the .cmodel file
		ModelResource              model;   ///< what those bytes decode to (for summaries)
	};

	/// Reads a whole file. Throws std::runtime_error if it is missing or over the 1 GiB limit.
	std::vector<unsigned char> ReadFile(const std::filesystem::path& path);

	/// Byte-exact fingerprint of a source .glb, mixed with the texture compiler's version.
	uint64_t SourceHash(std::span<const unsigned char> sourceBytes);

	/// Whether `target` is a valid .cmodel of this guid, compiled from exactly this source.
	bool IsUpToDate(const std::filesystem::path& target, uint64_t guid, uint64_t sourceHash);

	/// Imports a .glb, compresses its images, encodes it and proves the result round-trips.
	/// Throws std::runtime_error with a readable message if the model cannot be compiled.
	Output Compile(std::span<const unsigned char> sourceBytes, uint64_t guid, uint64_t sourceHash);

	/// Writes the .cmodel and its .meta (carrying the source's guid), each replaced atomically.
	void Write(const std::filesystem::path& target, const std::vector<unsigned char>& bytes,
	           uint64_t guid, const std::string& virtualPath);

	/// "nodes=11 meshes=4 ...": one line for logs.
	std::string Summary(const ModelResource& model);
}
