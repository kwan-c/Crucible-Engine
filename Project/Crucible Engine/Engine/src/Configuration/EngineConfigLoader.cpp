//EngineConfigLoader.h
// loads engine configuration form jason

#include <limits>
#include <stdexcept>
#include <string>

#include "EngineConfigLoader.h"
#include "Json/JsonReader.h"
#include <Diagnostics/Logger.h>


namespace Crucible
{
	namespace
	{
		using JsonValue = rapidjson::Value;

		//helper to find root , graphics for example
		const JsonValue* FindSection(const JsonValue& root, const char* name)
		{
			auto member = root.FindMember(name);

			if (member == root.MemberEnd()) return nullptr;

			if (!member->value.IsObject())
			{
				std::string msg = std::string("EngineConfigLoader: '") + name + "' must be a JSON object.";
				CRU_WARN("{}", msg);
				throw std::runtime_error(msg);
			}
			
			return &member->value;
		}

		//helper fun to read in and store out put
		void ReadUInt(const JsonValue& object,const char* key,std::uint32_t& output)
		{
			auto member = object.FindMember(key);

			if (member == object.MemberEnd())return;//if can find default value;


			if (!member->value.IsUint())
			{
				std::string msg = std::string("EngineConfigLoader: '") + key + "' must be an unsigned integer.";
				CRU_WARN("{}", msg);
				throw std::runtime_error(msg);
			}


			output = member->value.GetUint();
		}

		void ReadUInt16(const JsonValue& object,const char* key,std::uint16_t& output)
		{
			auto member = object.FindMember(key);

			if (member == object.MemberEnd())return;



			if (!member->value.IsUint())
			{
				std::string msg = std::string("EngineConfigLoader: '") + key + "' must be an unsigned integer.";
				CRU_WARN("{}", msg);
				throw std::runtime_error(msg);
			}


			const unsigned int value =member->value.GetUint();


			if (value >std::numeric_limits<std::uint16_t>::max())
			{
				std::string msg = std::string("EngineConfigLoader: '") + key + "' is outside uint16_t range.";
				CRU_WARN("{}", msg);
				throw std::runtime_error(msg);
			}


			output = static_cast<std::uint16_t>(value);
		}

		void ReadFloat(const JsonValue& object,const char* key,float& output)
		{
			auto member = object.FindMember(key);

			if (member == object.MemberEnd())return;



			if (!member->value.IsNumber())
			{
				std::string msg = std::string("EngineConfigLoader: '") + key + "' must be a number.";
				CRU_WARN("{}", msg);
				throw std::runtime_error(msg);
			}

			output = member->value.GetFloat();
		}

		void ReadBool(const JsonValue& object,const char* key,bool& output)
		{
			auto member = object.FindMember(key);

			if (member == object.MemberEnd())return;

			if (!member->value.IsBool())
			{
				std::string msg = std::string("EngineConfigLoader: '") + key + "' must be true or false.";
				CRU_WARN("{}", msg);
				throw std::runtime_error(msg);
			}

			output = member->value.GetBool();
		}

		void ReadString(const JsonValue& object,const char* key,std::string& output)
		{
			auto member = object.FindMember(key);

			if (member == object.MemberEnd())return;



			if (!member->value.IsString())
			{
				std::string msg = std::string("EngineConfigLoader: '") + key + "' must be a string.";
				CRU_WARN("{}", msg);
				throw std::runtime_error(msg);
			}

			output = member->value.GetString();
		}
	}

	EngineConfig EngineConfigLoader::Load(const std::filesystem::path& path)
	{
		//default initilize with values in engineconfig.h
		EngineConfig config{};

		rapidjson::Document document = JsonReader::Read(path);

		if (!document.IsObject())
		{
			std::string msg = "EngineConfigLoader: Root JSON value must be an object.";
			CRU_WARN("{}", msg);
			throw std::runtime_error(msg);
		}

		//graphics
		if (const JsonValue* graphics =FindSection(document, "graphics"))
		{
			ReadString(*graphics, "windowTitle", config.graphics.windowTitle);
			ReadUInt(*graphics,"windowWidth",config.graphics.windowWidth);
			ReadUInt(*graphics,"windowHeight",config.graphics.windowHeight);
			ReadBool(*graphics,"fullScreen",config.graphics.fullScreen);
			ReadUInt(*graphics,"targetFPS",config.graphics.targetFPS);
		}

		//networking
		if (const JsonValue* network =FindSection(document, "network"))
		{
			ReadUInt(*network,"port",config.network.port);
			ReadUInt(*network,"maxPlayers",config.network.maxPlayers);
		}


		//audio
		if (const JsonValue* audio =FindSection(document, "audio"))
		{
			ReadFloat(*audio,"masterVolume",config.audio.masterVolume);
			ReadFloat(*audio,"musicVolume",config.audio.musicVolume);
			ReadFloat(*audio,"sfxVolume",config.audio.sfxVolume);
		}
		return config;
	}


}