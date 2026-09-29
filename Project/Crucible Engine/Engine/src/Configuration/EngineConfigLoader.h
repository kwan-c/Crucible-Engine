#pragma once

#include <filesystem>

#include "EngineConfig.h"
#include "EngineExport.h"

namespace Crucible
{
	class ENGINE_API EngineConfigLoader
	{
	public:

		static EngineConfig Load(const std::filesystem::path& path);
	};
}