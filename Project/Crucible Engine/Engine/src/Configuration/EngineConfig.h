// engine config
// contains data structs for engine settings for graphics networking audio etc
// see configLoader to see how the defaults get populated
//
#pragma once


#include <cstdint>
#include <string>

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

	//Struct of config
	struct EngineConfig
	{
		GrahpicsConfig graphics;
		NetworkConfig  network;
		AudioConfig    audio;
	};


}
