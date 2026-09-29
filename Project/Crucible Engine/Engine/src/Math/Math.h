#pragma once

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/compatibility.hpp>

namespace Crucible::Math
{
    using vec2 = glm::fvec2;
    using vec3 = glm::fvec3;
    using vec4 = glm::fvec4;

    using mat3 = glm::fmat3;
    using mat4 = glm::fmat4;

    using quat = glm::fquat;
}