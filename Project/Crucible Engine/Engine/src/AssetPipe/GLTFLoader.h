#pragma once
/*************************************************************************
* \file:         GLTFLoader.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.1
* \brief
*                Hand-rolled .glb (binary glTF) parser: reads a GLB's JSON
*                + binary chunks with the already-vendored RapidJSON.
*                LoadModelGLB produces a complete CPU ModelResource.
*                LoadGLB remains the first-primitive compatibility API.
*                No new third-party importer dependency.
*
* \details
*                Imports all model nodes/scenes, triangle primitives,
*                core PBR materials/textures, skins, and LINEAR/STEP TRS
*                animation. ModelResource owns the data independently of
*                the input bytes or RapidJSON document. No playback or
*                GPU skinning is performed by this importer.
*
*                Unsupported features fail explicitly: sparse/bufferless
*                accessors, external/data URIs, morph targets, additional
*                UV/influence sets, cameras, required extensions and
*                CUBICSPLINE. Optional extensions use the core fallback
*                with a warning. Images remain encoded PNG/JPEG blobs.
*                Non-indexed triangle primitives receive generated indices.
*
*                This header depends on RenderTypes.h for MeshData/Vertex
*                (same dependency Primitives.h already has - see that
*                file), but deliberately NOT on RenderSystem.h/Renderer:
*                turning the parsed data into GPU resources
*                (Renderer::CreateMesh, CreateTextureCompressed,
*                CreateMaterial) is left to the caller, so this parser
*                stays testable standalone (no live Vulkan device needed),
*                mirroring how Primitives.h was verified.
*
* \history
*    -v1.0       initial implementation
*    -v1.1       generic complete-model import; validated shared accessors;
*                first-primitive API retained for existing static callers
*
**************************************************************************/

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Graphics/RenderTypes.h"   // MeshData / Vertex
#include "Resources/ModelResource.h"

namespace Crucible {

    /*************************************************************************
     \brief
        One image embedded in the GLB's binary chunk, still encoded
        (PNG/JPEG/...) exactly as stored.

     \details
        CrucibleModelCompiler compresses them for the GPU (stb_image under the
        hood).
     *************************************************************************/
    struct GLTFImage {
        std::vector<unsigned char> bytes;
    };

    /*************************************************************************
     \brief
        The imported primitive's material: texture references (indices
        into GLTFAsset::images) and scalar factors.

     \details
        Mirrors Crucible::MaterialData one-for-one but stays independent of
        it (TextureHandle only exists once a texture has actually been
        uploaded to the GPU, which hasn't happened yet at parse time).
     *************************************************************************/
    /// glTF material.alphaMode: how the base colour's alpha is used.
    enum class GLTFAlphaMode : uint8_t {
        Opaque,   ///< "OPAQUE" (the default): alpha ignored
        Mask,     ///< "MASK": pixels below alphaCutoff are cut out (leaves, fences)
        Blend     ///< "BLEND": see-through (the engine has no blended materials yet - cut out at 0.5)
    };

    struct GLTFMaterial {
        bool        hasBaseColor = false;           std::size_t baseColorImage = 0;
        bool        hasNormal = false;              std::size_t normalImage = 0;
        bool        hasMetallicRoughness = false;   std::size_t metallicRoughnessImage = 0;
        bool        hasEmissive = false;            std::size_t emissiveImage = 0;
        bool        hasOcclusion = false;           std::size_t occlusionImage = 0;

        glm::vec4   baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec3   emissiveFactor{ 0.0f, 0.0f, 0.0f };
        float       metallicFactor  = 1.0f;
        float       roughnessFactor = 1.0f;

        GLTFAlphaMode alphaMode   = GLTFAlphaMode::Opaque;
        float         alphaCutoff = 0.5f;    // MASK only (the spec's default is 0.5)
        bool          doubleSided = false;   // Blender: Backface Culling off -> true
    };

    /*************************************************************************
     \brief
        Result of parsing one .glb file (see the file header's scope-limit
        note).
     *************************************************************************/
    struct GLTFAsset {
        bool        ok = false;
        std::string error;   // set when ok is false; empty otherwise

        MeshData                    mesh;
        std::string                 meshName;   // meshes[0]'s own "name", if the file set one
                                                 // (glTF's mesh.name is optional); empty otherwise
                                                 // - the caller decides the fallback (a filename,
                                                 // usually) rather than this header inventing one.
        std::vector<GLTFImage>      images;     // indexed by GLTFMaterial's *Image fields
        std::optional<GLTFMaterial> material;   // unset if the primitive referenced no material
    };

    /*************************************************************************
     \brief
        Static entry points for parsing a .glb file into a GLTFAsset.
     *************************************************************************/
    class GLTFLoader {
    public:
        // Complete, CPU-only import of one self-contained GLB. Counts, names and
        // array ordering are data, never model-specific policy. See ModelResource.h.
        static ModelImportResult LoadModelGLB(std::span<const unsigned char> fileBytes);
        static ModelImportResult LoadModelGLBFile(const std::filesystem::path& path);

        // Compatibility projection for the existing single-primitive renderer.
        // Does not evaluate node transforms, animation or skinning.
        static GLTFAsset FirstPrimitive(const ModelResource& model);

        /*********************************************************************
         \brief
            Parses a .glb file already loaded into memory (the whole file,
            12-byte header included).

         \param[in] fileBytes The complete .glb file contents.
         \return A GLTFAsset with ok == false and a human-readable error on
                 any problem - malformed input is a content problem, not a
                 reason to throw or crash.
         *********************************************************************/
        static GLTFAsset LoadGLB(const std::vector<unsigned char>& fileBytes);

        /*********************************************************************
         \brief  Convenience: reads the file from disk, then LoadGLB().
         \param[in] path Path to the .glb file on disk.
         \return See LoadGLB().
         *********************************************************************/
        static GLTFAsset LoadGLBFile(const std::filesystem::path& path);
    };

}   // NAMESPACE CRUCIBLE
