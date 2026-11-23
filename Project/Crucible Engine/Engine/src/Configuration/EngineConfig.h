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
		// Window mode at startup. windowWidth x windowHeight stays the windowed size: the size
		// the window returns to when it leaves fullscreen or is un-maximized. A player's own last
		// choice (F11 / Alt+Enter, the maximize button) is remembered in UserSettings.json next to
		// the .exe and wins over these - see EngineConfigLoader::SaveWindowState.
		bool        fullScreen   = false;
		bool        maximized    = false;
		// The resolution fullscreen switches the monitor to ("exclusive"); 0 = the monitor's own
		// ("borderless", the default). Only ever set by the player (C# Screen.SetResolution),
		// so it is read from UserSettings.json alone, never from EngineConfig.json.
		uint32_t    fullscreenWidth       = 0;
		uint32_t    fullscreenHeight      = 0;
		uint32_t    fullscreenRefreshRate = 0;   // Hz; 0 = the highest at that size
		uint32_t    targetFPS = 60;
	};

	//network config - the team's shared multiplayer defaults ("network" in EngineConfig.json).
	// The one source of shared networking defaults: Core turns them into NetworkSettings at
	// startup (Network/NetworkSettings.h, FromEngineConfig). The editor's Network panel then
	// changes them for its own instance only, in memory - nothing writes a Host/Client choice
	// back here, so two editors can be Host and Client at the same time.
	struct NetworkConfig
	{
		std::string mode = "offline";             // "offline" | "host" | "client": what Play does at startup
		std::string listenAddress;                // host: local address to listen on; empty = all interfaces
		uint16_t    hostPort = 27020;             // host: UDP port to listen on
		std::string connectAddress = "127.0.0.1"; // client: the host's IP address
		uint16_t    connectPort = 27020;          // client: the host's port
		uint32_t    maxClients = 3;               // host: remote players accepted (1..3; the host is the 4th)
		// Finding lobbies: "auto" (the connection server if one is set, else / when it is down LAN
		// broadcast), "server" or "lan"; the connection server's IPv4 address ("" = none) and port;
		// the LAN broadcast port.
		std::string lobbyDiscovery = "auto";
		std::string lobbyServerAddress;
		uint16_t    lobbyServerPort = 27018;
		uint16_t    discoveryPort = 27019;
	};

	// No audio section: volumes are the PLAYER's choice, so they live in UserSettings.json
	// next to the .exe (Configuration/UserSettings.h), not in this shared team file.

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

	// Editor-only settings, stored in the "editor" section of EngineConfig.json next to the
	// editor's overrides ("editor": { "releaseCursorKey": "Shift+F1", ... }). The Game reads
	// them too but never uses them.
	struct EditorSettings
	{
		// Frees the mouse while the game has it captured in Play mode (a KeyChord, see
		// Input/KeyChord.h). Not Escape: that key belongs to the game (its pause menu).
		std::string releaseCursorKey = "Shift+F1";
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
		ProjectConfig  project;

		// Which app this config was loaded for (set by LoadForApp, never read from the file).
		// Core uses it to save the window state into the right section of UserSettings.json.
		AppKind        app = AppKind::Editor;
		ScriptingConfig scripting;
		EditorSettings  editor;
	};

	// The values one app's section ("editor" / "game") in EngineConfig.json changes, for the
	// editor's Project Settings window. Empty = not overridden, the shared value is used.
	// (LoadForApp applies any key in those sections; these are the ones the editor UI edits.)
	struct AppOverrides
	{
		std::optional<std::string>   windowTitle;
		std::optional<std::uint32_t> windowWidth;
		std::optional<std::uint32_t> windowHeight;
		std::optional<bool>          fullScreen;
		std::optional<bool>          maximized;
		std::optional<std::string>   startupScene;

		// `shared` with these overrides on top - what that app will start with.
		EngineConfig ApplyTo(EngineConfig shared) const
		{
			if (windowTitle)  shared.graphics.windowTitle  = *windowTitle;
			if (windowWidth)  shared.graphics.windowWidth  = *windowWidth;
			if (windowHeight) shared.graphics.windowHeight = *windowHeight;
			if (fullScreen)   shared.graphics.fullScreen   = *fullScreen;
			if (maximized)    shared.graphics.maximized    = *maximized;
			if (startupScene) shared.project.startupScene  = *startupScene;
			return shared;
		}
	};


}
