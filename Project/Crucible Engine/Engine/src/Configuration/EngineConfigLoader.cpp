//EngineConfigLoader.cpp
// loads engine configuration form jason

#include <limits>
#include <stdexcept>
#include <optional>
#include <string>
#include <vector>

#include "EngineConfigLoader.h"
#include "Json/JsonReader.h"
#include "AssetPipe/Assetmanager.h"
#include "Json/JsonWrapper.h"
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

		void ReadScriptInstances(const JsonValue& scripting, std::vector<ScriptInstanceConfig>& output)
		{
			const auto member = scripting.FindMember("instances");
			if (member == scripting.MemberEnd()) return;
			if (!member->value.IsArray()) throw std::runtime_error("EngineConfigLoader: scripting.instances must be an array.");
			std::vector<ScriptInstanceConfig> instances;
			for (const auto& value : member->value.GetArray()) {
				if (!value.IsObject()) throw std::runtime_error("EngineConfigLoader: each scripting instance must be an object.");
				ScriptInstanceConfig instance;
				ReadString(value, "assembly", instance.assembly);
				ReadString(value, "namespace", instance.nameSpace);
				ReadString(value, "class", instance.className);
				ReadBool(value, "enabled", instance.enabled);
				if (instance.assembly.empty() || instance.className.empty())
					throw std::runtime_error("EngineConfigLoader: scripting instances require assembly and class (namespace may be empty).");
				instances.push_back(std::move(instance));
			}
			output = std::move(instances); // per-app arrays replace the shared list, including []
		}

		// "aspectRatio": [16, 9]  ->  width / height. [0, 0] means free aspect.
		void ReadAspect(const JsonValue& object, const char* key, std::uint32_t& width, std::uint32_t& height)
		{
			auto member = object.FindMember(key);

			if (member == object.MemberEnd()) return;

			const JsonValue& value = member->value;
			if (!value.IsArray() || value.Size() != 2 || !value[0].IsUint() || !value[1].IsUint())
			{
				std::string msg = std::string("EngineConfigLoader: '") + key + "' must be [width, height], e.g. [16, 9].";
				CRU_WARN("{}", msg);
				throw std::runtime_error(msg);
			}

			width  = value[0].GetUint();
			height = value[1].GetUint();
		}

		// Reads every known section of `root` into `config`. Keys that are missing keep whatever
		// `config` already holds, which is what lets the "editor" / "game" sections override
		// only the values they mention.
		void ApplySections(const JsonValue& root, EngineConfig& config)
		{
			//graphics
			if (const JsonValue* graphics = FindSection(root, "graphics"))
			{
				ReadString(*graphics, "windowTitle", config.graphics.windowTitle);
				ReadUInt(*graphics, "windowWidth", config.graphics.windowWidth);
				ReadUInt(*graphics, "windowHeight", config.graphics.windowHeight);
				ReadBool(*graphics, "fullScreen", config.graphics.fullScreen);
				ReadUInt(*graphics, "targetFPS", config.graphics.targetFPS);
			}

			//networking
			if (const JsonValue* network = FindSection(root, "network"))
			{
				ReadUInt(*network, "port", config.network.port);
				ReadUInt(*network, "maxPlayers", config.network.maxPlayers);
			}

			//audio
			if (const JsonValue* audio = FindSection(root, "audio"))
			{
				ReadFloat(*audio, "masterVolume", config.audio.masterVolume);
				ReadFloat(*audio, "musicVolume", config.audio.musicVolume);
				ReadFloat(*audio, "sfxVolume", config.audio.sfxVolume);
			}

			//project
			if (const JsonValue* project = FindSection(root, "project"))
			{
				ReadString(*project, "startupScene", config.project.startupScene);
				ReadAspect(*project, "aspectRatio", config.project.aspectWidth, config.project.aspectHeight);
			}
			if (const JsonValue* scripting = FindSection(root, "scripting")) {
				ReadBool(*scripting, "enabled", config.scripting.enabled);
				ReadString(*scripting, "runtimeDirectory", config.scripting.runtimeDirectory);
				ReadBool(*scripting, "verifyManagedExecution", config.scripting.verifyManagedExecution);
				ReadString(*scripting, "startupAssembly", config.scripting.startupAssembly);
				ReadString(*scripting, "startupNamespace", config.scripting.startupNamespace);
				ReadString(*scripting, "startupClass", config.scripting.startupClass);
				ReadString(*scripting, "startupMethod", config.scripting.startupMethod);
				ReadBool(*scripting, "startupStatic", config.scripting.startupStatic);
				ReadScriptInstances(*scripting, config.scripting.instances);
			}
		}

		rapidjson::Document ReadRoot(const std::filesystem::path& path)
		{
			rapidjson::Document document = JsonReader::Read(path);

			if (!document.IsObject())
			{
				std::string msg = "EngineConfigLoader: Root JSON value must be an object.";
				CRU_WARN("{}", msg);
				throw std::runtime_error(msg);
			}
			return document;
		}

		const char* AppSectionName(AppKind app)
		{
			switch (app)
			{
			case AppKind::Editor: return "editor";
			case AppKind::Game:   return "game";
			}
			return "";
		}

		// Reads `key` into `out` only if it is present (so "absent" stays distinguishable).
		template<typename T, typename Reader>
		void ReadOptional(const JsonValue& object, const char* key, std::optional<T>& out, Reader reader)
		{
			if (!object.HasMember(key)) return;
			T value{};
			reader(object, key, value);
			out = value;
		}

		// Set the key when `value` has one, remove it otherwise.
		template<typename T>
		void SetOrRemove(JsonWrapper& doc, const std::string& path, const std::optional<T>& value)
		{
			if (value) doc.Set(path, *value);
			else       doc.Remove(path);
		}

		// Inside the Assets folder.
		constexpr const char* CONFIG_RELATIVE_PATH = "Config/EngineConfig.json";
	}

	EngineConfig EngineConfigLoader::Load(const std::filesystem::path& path)
	{
		//default initilize with values in engineconfig.h
		EngineConfig config{};

		rapidjson::Document document = ReadRoot(path);
		ApplySections(document, config);
		return config;
	}

	std::filesystem::path EngineConfigLoader::FindConfigFile()
	{
		const std::filesystem::path assetRoot = AssetManager::FindAssetRoot({});
		if (assetRoot.empty()) return {};
		return assetRoot / CONFIG_RELATIVE_PATH;
	}

	bool EngineConfigLoader::Save(const EngineConfig& config, const std::filesystem::path& path)
	{
		if (path.empty())
		{
			CRU_WARN("EngineConfigLoader: no config path to save to");
			return false;
		}

		// Start from the file as it is, so override sections and unknown keys are kept. If it is
		// there but broken, refuse rather than replace someone's hand edits with a fresh file.
		JsonWrapper doc;
		std::error_code ec;
		if (std::filesystem::exists(path, ec) && !doc.ReadFromFile(path))
		{
			CRU_WARN("EngineConfigLoader: '{}' could not be parsed - not overwriting it", path.string());
			return false;
		}

		// Same keys, same layout as ApplySections reads.
		doc.Set("graphics/windowTitle",  config.graphics.windowTitle);
		doc.Set("graphics/windowWidth",  config.graphics.windowWidth);
		doc.Set("graphics/windowHeight", config.graphics.windowHeight);
		doc.Set("graphics/fullScreen",   config.graphics.fullScreen);
		doc.Set("graphics/targetFPS",    config.graphics.targetFPS);

		doc.Set("network/port",       config.network.port);
		doc.Set("network/maxPlayers", config.network.maxPlayers);

		doc.Set("audio/masterVolume", config.audio.masterVolume);
		doc.Set("audio/musicVolume",  config.audio.musicVolume);
		doc.Set("audio/sfxVolume",    config.audio.sfxVolume);

		doc.Set("project/startupScene", config.project.startupScene);
		doc.SetList("project/aspectRatio", std::vector<std::uint32_t>{ config.project.aspectWidth, config.project.aspectHeight });
		doc.Set("scripting/enabled", config.scripting.enabled);
		doc.Set("scripting/runtimeDirectory", config.scripting.runtimeDirectory);
		doc.Set("scripting/verifyManagedExecution", config.scripting.verifyManagedExecution);
		doc.Set("scripting/startupAssembly", config.scripting.startupAssembly);
		doc.Set("scripting/startupNamespace", config.scripting.startupNamespace);
		doc.Set("scripting/startupClass", config.scripting.startupClass);
		doc.Set("scripting/startupMethod", config.scripting.startupMethod);
		doc.Set("scripting/startupStatic", config.scripting.startupStatic);
		doc.SetList("scripting/instances", std::vector<std::string>{});
		for (std::size_t i = 0; i < config.scripting.instances.size(); ++i) {
			const auto pathPrefix = "scripting/instances/" + std::to_string(i) + "/";
			const auto& instance = config.scripting.instances[i];
			doc.Set(pathPrefix + "assembly", instance.assembly);
			doc.Set(pathPrefix + "namespace", instance.nameSpace);
			doc.Set(pathPrefix + "class", instance.className);
			doc.Set(pathPrefix + "enabled", instance.enabled);
		}

		if (!doc.SaveToFile(path))
		{
			CRU_WARN("EngineConfigLoader: could not write '{}'", path.string());
			return false;
		}
		return true;
	}

	AppOverrides EngineConfigLoader::LoadOverrides(AppKind app, const std::filesystem::path& path)
	{
		AppOverrides overrides;

		rapidjson::Document document = ReadRoot(path);
		const JsonValue* section = FindSection(document, AppSectionName(app));
		if (section == nullptr) return overrides;

		if (const JsonValue* graphics = FindSection(*section, "graphics"))
		{
			ReadOptional(*graphics, "windowTitle",  overrides.windowTitle,  ReadString);
			ReadOptional(*graphics, "windowWidth",  overrides.windowWidth,  ReadUInt);
			ReadOptional(*graphics, "windowHeight", overrides.windowHeight, ReadUInt);
		}
		if (const JsonValue* project = FindSection(*section, "project"))
		{
			ReadOptional(*project, "startupScene", overrides.startupScene, ReadString);
		}
		return overrides;
	}

	bool EngineConfigLoader::SaveOverrides(AppKind app, const AppOverrides& overrides, const std::filesystem::path& path)
	{
		if (path.empty())
		{
			CRU_WARN("EngineConfigLoader: no config path to save to");
			return false;
		}

		JsonWrapper doc;
		std::error_code ec;
		if (std::filesystem::exists(path, ec) && !doc.ReadFromFile(path))
		{
			CRU_WARN("EngineConfigLoader: '{}' could not be parsed - not overwriting it", path.string());
			return false;
		}

		const std::string section = AppSectionName(app);
		SetOrRemove(doc, section + "/graphics/windowTitle",  overrides.windowTitle);
		SetOrRemove(doc, section + "/graphics/windowWidth",  overrides.windowWidth);
		SetOrRemove(doc, section + "/graphics/windowHeight", overrides.windowHeight);
		SetOrRemove(doc, section + "/project/startupScene",  overrides.startupScene);

		if (!doc.SaveToFile(path))
		{
			CRU_WARN("EngineConfigLoader: could not write '{}'", path.string());
			return false;
		}
		return true;
	}

	EngineConfig EngineConfigLoader::LoadForApp(AppKind app)
	{
		const std::filesystem::path path = FindConfigFile();
		if (path.empty())
		{
			CRU_WARN("EngineConfigLoader: no Assets folder found - using default config");
			return EngineConfig{};
		}

		try
		{
			// Shared settings first, then this app's overrides on top.
			EngineConfig config{};
			rapidjson::Document document = ReadRoot(path);
			ApplySections(document, config);

			if (const JsonValue* overrides = FindSection(document, AppSectionName(app)))
				ApplySections(*overrides, config);

			CRU_INFO("EngineConfigLoader: loaded '{}' ({})", path.string(), AppSectionName(app));
			return config;
		}
		catch (const std::exception& e)
		{
			CRU_WARN("EngineConfigLoader: could not load '{}' - using default config. {}", path.string(), e.what());
			return EngineConfig{};
		}
	}
}
