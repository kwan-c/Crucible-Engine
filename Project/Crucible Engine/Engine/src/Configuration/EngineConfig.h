// engine config
// contains data structs for engine settings for graphics networking audio etc
// see configLoader to see how the defaults get populated
//
// The file lives at Assets/Config/EngineConfig.json and is loaded by both the Editor and the
// Game through EngineConfigLoader::LoadForApp(), so both start from the same settings.
//
#pragma once


#include <cstdint>
#include <optional>
#include <string>
#include "Scripting/ScriptingConfig.h"

namespace Crucible
{
	//graphics config
	struct GrahpicsConfig
	{
		std::string windowTitle  = "Crucible Engine";
		uint32_t    windowWidth  = 1200;
		uint32_t    windowHeight = 800;
		bool        fullScreen   = false;
		uint32_t    targetFPS = 60;
	};

	//network config
	struct NetworkConfig
	{
		uint32_t port = 9999;
		uint32_t maxPlayers = 4;
	};

	//audio config
	struct AudioConfig
	{
		float masterVolume = 1.0f;
		float musicVolume  = 1.0f;
		float sfxVolume    = 1.0f;
	};

	//project config - settings about the game itself, shared by the Editor and the Game
	struct ProjectConfig
	{
		// Scene opened at startup, relative to the Assets folder. Empty = start with no scene.
		std::string startupScene = "Scenes/Fog_Lighting_Effects.scene";

		// The shape the game is framed for. The Game locks its window to it; the Editor's Game
		// view letterboxes to it by default. 0:0 = free aspect.
		uint32_t aspectWidth  = 16;
		uint32_t aspectHeight = 9;

		// width / height, or 0 when the aspect is free.
		float AspectRatio() const
		{
			return (aspectWidth > 0 && aspectHeight > 0)
				? static_cast<float>(aspectWidth) / static_cast<float>(aspectHeight)
				: 0.0f;
		}
	};

	// Which executable is starting the engine. Picks the per-app override section
	// ("editor" / "game") in EngineConfig.json.
	enum class AppKind
	{
		Editor,
		Game
	};

	//Struct of config
	struct EngineConfig
	{
		GrahpicsConfig graphics;
		NetworkConfig  network;
		AudioConfig    audio;
		ProjectConfig  project;
		ScriptingConfig scripting;
	};

	// The values one app's section ("editor" / "game") in EngineConfig.json changes, for the
	// editor's Project Settings window. Empty = not overridden, the shared value is used.
	// (LoadForApp applies any key in those sections; these are the ones the editor UI edits.)
	struct AppOverrides
	{
		std::optional<std::string>   windowTitle;
		std::optional<std::uint32_t> windowWidth;
		std::optional<std::uint32_t> windowHeight;
		std::optional<std::string>   startupScene;

		// `shared` with these overrides on top - what that app will start with.
		EngineConfig ApplyTo(EngineConfig shared) const
		{
			if (windowTitle)  shared.graphics.windowTitle  = *windowTitle;
			if (windowWidth)  shared.graphics.windowWidth  = *windowWidth;
			if (windowHeight) shared.graphics.windowHeight = *windowHeight;
			if (startupScene) shared.project.startupScene  = *startupScene;
			return shared;
		}
	};


}
