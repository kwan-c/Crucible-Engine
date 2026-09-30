#pragma once

// Precompiled header for Crucible engine

// Include standard library headers
#include <algorithm>
#include <array>
#include <cstdint>
#include <concepts>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <optional>
#include <variant>
#include <ranges>
#include <format>
#include <span>
#include <type_traits>
#include <functional>
#include <cassert>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>
#include <chrono>

// Include third-party library headers
#include <rapidjson/document.h>
#include <GLFW/glfw3.h>

// Include Crucible engine headers
#include "AssetPipe/Assetmanager.h"
#include "AssetPipe/AssetRegistry.h"
#include "Audio/AudioBuses.h"
#include "Configuration/EngineConfig.h"
#include "Configuration/EngineConfigLoader.h"
#include "Diagnostics/Logger.h"
#include "Diagnostics/LogLevel.h"
#include "Diagnostics/LogSink.h"
#include "Diagnostics/LogTime.h"
#include "Diagnostics/ScopedTimer.h"
#include "ECS/Archetype/Archetype.h"
#include "ECS/Archetype/ArchetypeRegistry.h"
#include "ECS/Components/Components.h"

// Define any necessary macros or constants

// Forward declarations of classes and functions
