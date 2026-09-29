//jsonReader.cpp


#include "JsonReader.h"

#include <fstream>
#include <stdexcept>
#include <string>

#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>
#include <Diagnostics/Logger.h>

namespace Crucible
{
	rapidjson::Document JsonReader::Read(const std::filesystem::path& path)
	{
		std::ifstream file(path);

		if (!file.is_open())
		{
			CRU_ERROR("JsonReader: failure to open file from{}", path.string());
			throw std::runtime_error("JsonReader: failure to open file from"+path.string());
		}

		rapidjson::IStreamWrapper stream(file);

		rapidjson::Document document;

		//read through file
		document.ParseStream(stream);

		//catch for error if the json file has error(incorrect conventions)
		if (document.HasParseError())
		{
			std::string errorMessage ="JsonReader: JSON parse error in "+ path.string()+ " at offset "+ std::to_string(document.GetErrorOffset())
			+ ": "+ rapidjson::GetParseError_En(document.GetParseError());

			CRU_ERROR("Json Reader:{}", errorMessage);

			throw std::runtime_error(errorMessage);
		}

		return document;
	}
}