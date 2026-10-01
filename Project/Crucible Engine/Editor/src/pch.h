#pragma once

// ---- Standard library ----
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

// ---- Third-party ----
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <imgui.h>
#include <rttr/type.h>