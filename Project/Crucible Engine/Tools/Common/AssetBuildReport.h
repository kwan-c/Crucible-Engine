/*!
* @file     AssetBuildReport.h
* @brief    The list of assets the build could not compile, shared by the asset compilers
*           (which write it) and the editor (which reads it on startup).
*
* The model and texture compilers run on every build. Run with --report <file>, a bad asset no
* longer fails the build: it is skipped, written to this file, and the compiler exits 0, so the
* Engine and Editor still build and open. The editor then lists the problems in the Console and
* will not enter Play mode until they are fixed. Cook and Package Game stay strict.
*
* Format: plain text, one problem per line, "<asset virtual path>\t<message>".
* Lines starting with '#' are comments. An empty file (or no file) means no problems.
*/
#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace Crucible::AssetBuildReport
{
	struct Problem
	{
		std::string asset;     // virtual path of the source asset, e.g. "Models/Rock.glb"
		std::string message;   // why it failed, on one line
	};

	// The reports live in the build folder, one file per compiler.
	inline std::filesystem::path Folder(std::filesystem::path const& buildDir) { return buildDir / "AssetBuildReports"; }

	// Tabs and line breaks would break the one-line format.
	inline std::string OneLine(std::string text)
	{
		for (char& c : text)
			if (c == '\t' || c == '\r' || c == '\n') c = ' ';
		return text;
	}

	// Replaces the file: a build that compiles cleanly leaves it with no problems.
	inline bool Write(std::filesystem::path const& file, std::vector<Problem> const& problems)
	{
		std::error_code ec;
		std::filesystem::create_directories(file.parent_path(), ec);
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out << "# Crucible asset build report: assets that failed to compile on the last build\n";
		for (Problem const& p : problems) out << OneLine(p.asset) << '\t' << OneLine(p.message) << '\n';
		return static_cast<bool>(out);
	}

	inline std::vector<Problem> Read(std::filesystem::path const& file)
	{
		std::vector<Problem> problems;
		std::ifstream in(file, std::ios::binary);
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			std::size_t const tab = line.find('\t');
			if (tab == std::string::npos) problems.push_back({ line, "failed to compile" });
			else problems.push_back({ line.substr(0, tab), line.substr(tab + 1) });
		}
		return problems;
	}

	// Every report in the build folder (models and textures).
	inline std::vector<Problem> ReadAll(std::filesystem::path const& buildDir)
	{
		std::vector<Problem> all;
		std::error_code ec;
		for (auto const& entry : std::filesystem::directory_iterator(Folder(buildDir), ec)) {
			if (!entry.is_regular_file(ec) || entry.path().extension() != ".txt") continue;
			for (Problem& p : Read(entry.path())) all.push_back(std::move(p));
		}
		return all;
	}
}
