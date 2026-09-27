#pragma once

#include <filesystem>
#include <rapidjson/document.h>

//json file reader for engine config
namespace Crucible
{
	class JsonReader
	{
	public:
		static rapidjson::Document Read(const std::filesystem::path& path);
   };
}