/*!
* @file			main.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		CrucibleCook: runs Cook (Cook.h) from the command line, for the installer
*				build. Exits 1 if Resources is incomplete, so a broken package fails the build.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "Cook.h"

#include <iostream>

int main(int argc, char** argv) {
	if (argc != 3) {
		std::cerr << "Usage: CrucibleCook <Assets> <Resources>\n";
		return 2;
	}

	Crucible::Cook::Report const report = Crucible::Cook::Run(argv[1], argv[2]);

	for (std::string const& line : report.warnings) std::cout << "[Cook] warning: " << line << '\n';
	for (std::string const& line : report.missing)  std::cerr << "[Cook] missing: " << line << '\n';
	for (std::string const& line : report.errors)   std::cerr << "[Cook] error: " << line << '\n';
	std::cout << "[Cook] copied=" << report.copied << " unchanged=" << report.unchanged
	          << " removed=" << report.removed << " missing=" << report.missing.size()
	          << " errors=" << report.errors.size() << '\n';

	return report.Ok() ? 0 : 1;
}
