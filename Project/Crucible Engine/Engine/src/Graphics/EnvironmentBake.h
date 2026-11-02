/*************************************************************************
  \file       EnvironmentBake.h
  \brief
    The CPU maths that turns a sky into what the renderer needs: a
    panorama into six cube faces, and those faces into image-based
    lighting (a blurred cube for reflections and nine spherical-harmonic
    numbers for diffuse light).
  \details
    No Vulkan and no image loading, so the same code runs in two places:
      - EnvironmentMaps (the renderer), for skies made at runtime;
      - CrucibleTextureCompiler, which bakes *_skybox images into .sky
        files offline, so the game only uploads them (Resources/CompiledSky.h).
*************************************************************************/
#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "EngineExport.h"

namespace Crucible::EnvironmentBake {

    /// World-space direction through the centre of texel (x, y) of cube face `face`, using the
    /// Vulkan/OpenGL cube-map face selection table (face order +X, -X, +Y, -Y, +Z, -Z; row 0 =
    /// t near 0). Inverse of what the sampler does, so faces generated with this line up with
    /// SamplerCube lookups by construction.
    ENGINE_API glm::vec3 CubeFaceTexelDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t size);

    /// The reverse of CubeFaceTexelDirection: which face, and which texel of it, a direction
    /// points at (same face selection table).
    ENGINE_API void DirectionToCubeTexel(const glm::vec3& d, uint32_t size, uint32_t& face, uint32_t& x, uint32_t& y);

    /// An equirectangular panorama (linear RGBA floats, width x height, top row = straight up)
    /// as six cube faces of faceSize^2 linear RGB texels each, face after face.
    ENGINE_API std::vector<glm::vec3> EquirectToCube(const float* rgba, int width, int height, uint32_t faceSize);

    /// A sky prepared as a light source.
    struct Lighting {
        /// Face size of each level of the reflection cube: a power of two at most 128, down to 16.
        std::vector<uint32_t> sizes;
        /// Per level, six faces of sizes[level]^2 linear RGB texels. Level 0 is the sky shrunk
        /// (a mirror); each further level is what a rougher surface would reflect.
        std::vector<std::vector<glm::vec3>> levels;
        /// Diffuse light as nine spherical-harmonic coefficients, with the cosine falloff and the
        /// basis constants folded in (see SkyIrradiance in DefaultShader.slang).
        std::array<glm::vec3, 9> sh{};
    };

    /// Bakes the lighting of six linear RGB faces of faceSize^2 texels (face after face).
    ENGINE_API Lighting BakeLighting(const std::vector<glm::vec3>& faces, uint32_t faceSize);

}
