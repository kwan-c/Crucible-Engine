#pragma once

#include <vector>
#include <string>

namespace Crucible::Graphics {

	class Utils {

	public:
		static std::vector<char> ReadFile(const std::string& filename);
	};

}
