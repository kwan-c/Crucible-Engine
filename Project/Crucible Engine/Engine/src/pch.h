#pragma once
// Precompiled header: ONLY headers that almost never change. No Crucible headers here.

// ---- Standard library ----
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// ---- Third-party ----
#define GLFW_INCLUDE_VULKAN              // matches Systems/WindowManager.h
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

#define GLM_ENABLE_EXPERIMENTAL          // matches Math/Math.h
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/compatibility.hpp>

#include <rapidjson/document.h>
#include <rttr/registration.h>
#include <rttr/type.h>