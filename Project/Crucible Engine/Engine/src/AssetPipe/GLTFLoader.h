#pragma once
/*************************************************************************
 \file       GLTFLoader.h
 \brief
    Hand-rolled .glb (binary glTF) parser: reads a GLB's JSON + binary
    chunks with the already-vendored rapidjson (Extern/rapidjson) and
    produces a Crucible::MeshData plus an optional material description,
    with no new third-party dependency (no cgltf/tinygltf).

 \details
    DELIBERATE SCOPE LIMIT: only the first mesh's first primitive is
    imported, and only that primitive's material (if it has one). A full
    glTF importer would walk the whole node graph, every mesh's every
    primitive, skinning, animation, cameras and lights - none of that
    exists in Crucible's ECS/renderer yet (one MeshHandle + one
    MaterialHandle per MeshRenderer - see ECS/Components/Components.h), so
    parsing more than this pass uses would be untested, unused code. This
    is enough to load a real single-mesh model (Khronos's small sample
    assets, most simple props) and prove the format end-to-end.

    Also out of scope: sparse accessors, non-indexed primitives, external
    (.bin/.uri) buffers and images (GLB-embedded only - external-file
    glTF should go through AssetManager's own file-reading path instead),
    morph targets, and any texture other than a GLB-embedded image blob
    (still-encoded bytes; decoding is the caller's job - see GLTFImage).

    This header depends on RenderTypes.h for MeshData/Vertex (same
    dependency Primitives.h already has - see that file), but deliberately
    NOT on RenderSystem.h/Renderer: turning the parsed data into GPU
    resources (Renderer::CreateMesh, CreateTextureFromMemory, CreateMaterial)
    is left to the caller, so this parser stays testable standalone (no
    live Vulkan device needed), mirroring how Primitives.h was verified.
 *************************************************************************/

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Graphics/RenderTypes.h"   // MeshData / Vertex

namespace Crucible {

    // One image embedded in the GLB's binary chunk, still encoded (PNG/JPEG/...) exactly as
    // stored - decode with Renderer::CreateTextureFromMemory (stb_image under the hood).
    struct GLTFImage {
        std::vector<unsigned char> bytes;
    };

    // The imported primitive's material: texture references (indices into GLTFAsset::images)
    // and scalar factors. Mirrors Crucible::MaterialData one-for-one but stays independent of
    // it (TextureHandle only exists once a texture has actually been uploaded to the GPU, which
    // hasn't happened yet at parse time).
    struct GLTFMaterial {
        bool        hasBaseColor = false;         std::size_t baseColorImage = 0;
        bool        hasNormal = false;            std::size_t normalImage = 0;
        bool        hasMetallicRoughness = false; std::size_t metallicRoughnessImage = 0;
        bool        hasEmissive = false;           std::size_t emissiveImage = 0;
        bool        hasOcclusion = false;          std::size_t occlusionImage = 0;

        glm::vec4   baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec3   emissiveFactor{ 0.0f, 0.0f, 0.0f };
        float       metallicFactor  = 1.0f;
        float       roughnessFactor = 1.0f;
    };

    // Result of parsing one .glb file (see the scope-limit note above).
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

    class GLTFLoader {
    public:
        // Parses a .glb file already loaded into memory (the whole file, 12-byte header
        // included). Returns a result with ok == false and a human-readable error on any
        // problem - malformed input is a content problem, not a reason to throw or crash.
        static GLTFAsset LoadGLB(const std::vector<unsigned char>& fileBytes);

        // Convenience: reads the file from disk, then LoadGLB.
        static GLTFAsset LoadGLBFile(const std::filesystem::path& path);
    };

}   // NAMESPACE CRUCIBLE
