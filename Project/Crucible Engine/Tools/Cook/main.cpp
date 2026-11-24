/*!
* @file			main.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		CrucibleCook: runs Cook (Cook.h) from the command line, for the installer
*				build. Exits 1 if Resources is incomplete, so a broken package fails the build.
*
*				CrucibleCook --shader-graphs <Assets> <Resources> only compiles the shader graphs
*				(CookShaderGraphs.h): the everyday build's ShaderGraphs target, run before Game.
*				Like the other everyday asset steps it lets a broken graph through (a warning,
*				exit 0) so the build still finishes; only a file it cannot write fails it.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "Cook.h"

#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
	bool const shaderGraphsOnly = argc == 4 && std::string_view(argv[1]) == "--shader-graphs";
	if (argc != 3 && !shaderGraphsOnly) {
		std::cerr << "Usage: CrucibleCook [--shader-graphs] <Assets> <Resources>\n";
		return 2;
	}
	char const* const assets    = argv[argc - 2];
	char const* const resources = argv[argc - 1];

	if (shaderGraphsOnly) {
		Crucible::Cook::Report const report = Crucible::Cook::RunShaderGraphs(assets, resources);
		for (std::string const& line : report.warnings) std::cout << "[ShaderGraphs] warning: " << line << '\n';
		for (std::string const& line : report.missing)  std::cout << "[ShaderGraphs] warning: missing " << line << '\n';
		for (std::string const& line : report.errors)   std::cerr << "[ShaderGraphs] error: " << line << '\n';
		std::cout << "[ShaderGraphs] compiled=" << report.compiled << " unchanged=" << report.unchanged
		          << " removed=" << report.removed << '\n';
		return report.errors.empty() ? 0 : 1;
	}

	Crucible::Cook::Report const report = Crucible::Cook::Run(assets, resources);

	for (std::string const& line : report.warnings) std::cout << "[Cook] warning: " << line << '\n';
	for (std::string const& line : report.missing)  std::cerr << "[Cook] missing: " << line << '\n';
	for (std::string const& line : report.errors)   std::cerr << "[Cook] error: " << line << '\n';
	std::cout << "[Cook] copied=" << report.copied << " compiled=" << report.compiled << " unchanged=" << report.unchanged
	          << " removed=" << report.removed << " missing=" << report.missing.size()
	          << " errors=" << report.errors.size() << '\n';

	return report.Ok() ? 0 : 1;
}
