/*************************************************************************
  \file       MaterialShaderParams.h
  \brief
    A mesh shader's own parameters: the list of numbers, colours and
    textures a material using that shader can set, with their names,
    defaults and slider ranges.
  \details
    The built-in shader has none (its settings are the fixed factors and
    five textures of MaterialData). A shader of its own - a hand-written
    surface such as MagicOrb.slang, later every shader graph - comes with a
    list, kept beside its compiled code as <ShaderName>.params.json:

        {
          "version": 1,
          "params": [
            { "name": "wispColour", "type": "colour", "default": [1.0, 1.0, 1.0] },
            { "name": "swirlSpeed", "type": "float",  "default": 0.6, "min": -4.0, "max": 4.0 }
          ],
          "textures": [
            { "name": "pattern", "colour": false, "default": "white" }
          ]
        }

    A parameter's SLOT is its position in its list, first = 0. The shader
    reads parameter slot n with MaterialParam(n) and declares texture slot n
    with MATERIAL_TEXTURE(n, name) (Surface.slang); a material's values are
    stored by slot (MaterialData::params, shaderTextures) and saved by name
    (a .mat's "params" and "textures"), so a list can be reordered or
    extended without touching the materials.

    The list is what everything else is built from: the Inspector's
    controls (Editor: MaterialShaderUI), what a .mat's names mean
    (ResourceManager), how much of the parameter buffer a material gets and
    which default an empty texture slot shows (RenderResources).
*************************************************************************/
#pragma once

#include "EngineExport.h"
#include "Math/Math.h"
#include "TextureHandle.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Crucible {

    /// What a parameter is. Each takes one float4 in the parameter buffer, however few of its components it uses.
    enum class MaterialParamType : uint8_t {
        Float,    ///< .x
        Vec2,     ///< .xy
        Vec3,     ///< .xyz
        Vec4,     ///< .xyzw
        Colour    ///< .rgb, linear, as given (values above 1 are allowed: a glow)
    };

    /// What an empty texture slot shows.
    enum class MaterialTextureDefault : uint8_t {
        White,    ///< (1, 1, 1, 1): "no change" for anything multiplied in
        Black,    ///< (0, 0, 0, 1): "nothing" for anything added
        Normal    ///< a flat tangent-space normal
    };

    struct MaterialParamDesc {
        std::string       name;                      ///< unique in the list; a letter or '_', then letters, digits, '_'
        MaterialParamType type = MaterialParamType::Float;
        glm::vec4         defaultValue{ 0.0f };      ///< what a material that does not set it gets
        float             minValue = 0.0f;           ///< slider range in the Inspector, "min" and "max" in the
        float             maxValue = 1.0f;           ///< file (numbers only; not enforced)

        friend bool operator==(const MaterialParamDesc&, const MaterialParamDesc&) = default;
    };

    struct MaterialTextureDesc {
        std::string            name;                 ///< unique among the textures; same rule as a parameter's
        bool                   srgb = true;          ///< true: colour data (sRGB); false: plain numbers (masks, normals)
        MaterialTextureDefault fallback = MaterialTextureDefault::White;

        friend bool operator==(const MaterialTextureDesc&, const MaterialTextureDesc&) = default;
    };

    struct MaterialShaderParams {

        /// Texture slots a material has (set 1's bindings; RenderLayouts::kMaterialTextureSlots).
        static constexpr uint32_t kMaxTextures = 8;
        /// More parameters than any real shader has; a longer list is refused as a mistake.
        static constexpr uint32_t kMaxParams   = 256;

        std::vector<MaterialParamDesc>   params;     ///< slot = index
        std::vector<MaterialTextureDesc> textures;   ///< slot = index = set 1 binding

        bool Empty() const { return params.empty() && textures.empty(); }

        /// The slot of the parameter / texture called `name`, or -1.
        ENGINE_API int FindParam(std::string_view name) const;
        ENGINE_API int FindTexture(std::string_view name) const;

        /*********************************************************************
         \brief  Reads a list from the text of a .params.json file.
         \param[in] json    The file's text.
         \param[out] out    The list (untouched on failure).
         \param[out] error  What is wrong, in words a person can act on.
         \return false for text that is not such a file: bad JSON, an unknown
                 type, a name used twice, too many textures.
         *********************************************************************/
        ENGINE_API static bool Parse(std::string_view json, MaterialShaderParams& out, std::string& error);

        /// The list as the text of a .params.json file (what Parse reads).
        ENGINE_API std::string ToJson() const;

        friend bool operator==(const MaterialShaderParams&, const MaterialShaderParams&) = default;
    };

    /*************************************************************************
     \brief  Carries one material's values over when its shader's list
             changes from `before` to `now`.
     \details
        Values are kept by slot, and a slot is a position in the list, so
        they are matched BY NAME: a parameter still in the list with the same
        type keeps the material's value, wherever it has moved to; a new
        one, one whose type changed, and one the material had no value for
        get the new list's default; a removed one is dropped. Textures the
        same way (an unmatched slot is left empty).
     \param[in] before         The list the values belong to.
     \param[in] now            The list they are wanted for.
     \param[in,out] params     The material's parameter values, by slot.
                              On return, one per parameter of `now`.
     \param[in,out] textures   Its shader textures, by slot. On return, one
                              per texture of `now`.
     *************************************************************************/
    ENGINE_API void RemapMaterialShaderValues(const MaterialShaderParams& before, const MaterialShaderParams& now,
                                   std::vector<glm::vec4>& params, std::vector<TextureHandle>& textures);

    /// "float", "vec2", "vec3", "vec4", "colour": the "type" values of a .params.json file.
    ENGINE_API const char* MaterialParamTypeName(MaterialParamType type);
    /// How many of a parameter's four numbers its type uses (1-4; 3 for a colour).
    ENGINE_API uint32_t    MaterialParamComponents(MaterialParamType type);

}   // NAMESPACE CRUCIBLE
