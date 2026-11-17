// The error material: what a material is drawn with when its shader cannot be - a shader graph
// that was deleted, has a problem that stops it or does not compile, a shader whose .spv is
// missing (not cooked), or one the GPU driver refused. Magenta and near-black squares laid out
// by the mesh's UVs, glowing a little so they show in the dark too: nobody mistakes
// it for a real material, and nothing crashes.
//
// Built by Engine/CompileShaders.cmake like every shader; loaded once at start next to the
// built-in one (MaterialShaders::Init). It has no parameters and reads no textures, so it draws
// any material whatever that material's shader expected. If ErrorShader.spv itself is missing,
// the engine falls back to the built-in shader for these materials (and says so in the log).
// See Surface.slang for how a surface function and SurfaceTemplate.slang fit together.

#include "Surface.slang"

SurfaceData EvaluateSurface(SurfaceInput s) {
    // Which square of the checkerboard this point is in: eight squares across the mesh's UV range,
    // so the pattern stays on the mesh when it moves or bends (a moving player, a skinned golem).
    // A mesh without UVs (all zero) is one magenta square: still plainly the error material.
    const float2 cell    = floor(s.uv * 8.0);
    const float  checker = abs(fmod(cell.x + cell.y, 2.0));   // 0 or 1

    const float3 magenta = float3(1.0, 0.0, 1.0);
    const float3 dark    = float3(0.02, 0.0, 0.02);

    SurfaceData o;
    o.albedo    = lerp(magenta, dark, checker);
    o.normalTS  = float3(0.0, 0.0, 1.0);
    o.metallic  = 0.0;
    o.roughness = 1.0;
    // A slow pulse on the magenta squares: lit or unlit, it is plainly not the intended surface.
    o.emissive  = magenta * (1.0 - checker) * (0.35 + 0.25 * sin(s.time * 3.0));
    o.occlusion = 1.0;
    o.alpha     = 1.0;
    return o;
}

#include "SurfaceTemplate.slang"
