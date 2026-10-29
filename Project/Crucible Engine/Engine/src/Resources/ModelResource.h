#pragma once

// Immutable, engine-owned model definitions. No JSON, renderer handles, Vulkan,
// playback clocks or mutable instance poses belong here. All indices below are
// local to THIS resource, never references into another file.
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Crucible {

    inline constexpr uint32_t InvalidModelIndex = std::numeric_limits<uint32_t>::max();

    struct ModelLocalTransform {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        // Static matrix nodes retain their exact matrix; TRS is used otherwise.
        // A matrix node cannot be targeted by a supported TRS animation channel.
        glm::mat4 matrix{1.0f};
        bool hasMatrix = false;
    };

    struct ModelNode {
        std::string name;
        uint32_t parent = InvalidModelIndex;
        std::vector<uint32_t> children;
        ModelLocalTransform local;
        uint32_t mesh = InvalidModelIndex;
        uint32_t skin = InvalidModelIndex;
    };

    struct ModelScene {
        std::string name;
        std::vector<uint32_t> roots;
    };

    struct ModelVertex {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec2 uv{0.0f};
        glm::vec4 color{1.0f};
        glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
        // Indices into the bound skin's jointNodes, NOT model node indices.
        glm::uvec4 joints{0u};
        glm::vec4 weights{0.0f};
    };

    struct ModelPrimitive {
        std::vector<ModelVertex> vertices;
        std::vector<uint32_t> indices;         // triangle list; synthesized for non-indexed inputs
        uint32_t material = InvalidModelIndex; // missing means glTF's default material
        bool hasNormals = false;
        bool hasUVs = false;
        bool hasColors = false;
        bool hasTangents = false;
        bool hasSkinning = false;
    };

    struct ModelMesh {
        std::string name;
        std::vector<ModelPrimitive> primitives;
    };

    struct ModelImage {
        std::string name;
        std::string mimeType;
        std::vector<unsigned char> bytes; // embedded, still encoded PNG/JPEG
    };

    // Engine enums; source-format constants are translated by the offline importer.
    enum class ModelFilter : uint32_t {
        Default, Nearest, Linear, NearestMipNearest, LinearMipNearest,
        NearestMipLinear, LinearMipLinear
    };
    enum class ModelWrap : uint32_t { Repeat, ClampToEdge, MirroredRepeat };
    struct ModelSampler {
        ModelFilter minFilter = ModelFilter::Default;
        ModelFilter magFilter = ModelFilter::Default;
        ModelWrap wrapS = ModelWrap::Repeat;
        ModelWrap wrapT = ModelWrap::Repeat;
    };

    struct ModelTexture {
        std::string name;
        uint32_t image = InvalidModelIndex;
        uint32_t sampler = InvalidModelIndex;
    };

    struct ModelTextureInfo {
        uint32_t texture = InvalidModelIndex;
        uint32_t texCoord = 0;
    };

    enum class ModelAlphaMode : uint32_t {
        Opaque,
        Mask,
        Blend
    };

    struct ModelMaterial {
        std::string name;
        glm::vec4 baseColorFactor{1.0f};
        glm::vec3 emissiveFactor{0.0f};
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        ModelTextureInfo baseColor, metallicRoughness, normal, occlusion, emissive;
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        ModelAlphaMode alphaMode = ModelAlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
    };

    struct ModelSkin {
        std::string name;
        uint32_t skeletonRoot = InvalidModelIndex;
        std::vector<uint32_t> jointNodes;
        // Column-major, in jointNodes order. Missing glTF inverse binds become identity.
        std::vector<glm::mat4> inverseBindMatrices;
    };

    enum class AnimationInterpolation : uint32_t {
        Linear,
        Step
    };
    enum class AnimationTarget : uint32_t {
        Translation,
        Rotation,
        Scale
    };

    struct ModelAnimationSampler {
        AnimationInterpolation interpolation = AnimationInterpolation::Linear;
        std::vector<float> times;      // original glTF timestamps, strictly increasing
        std::vector<glm::vec4> values; // rotation = XYZW; VEC3 output uses xyz, w = 0
        uint32_t components = 0;
    };

    struct ModelAnimationChannel {
        uint32_t sampler = InvalidModelIndex;
        uint32_t node = InvalidModelIndex;
        AnimationTarget target = AnimationTarget::Translation;
    };

    struct ModelAnimationClip {
        std::string name; // optional and not assumed unique; index is authoritative
        float startTime = 0.0f;
        float endTime = 0.0f;
        float duration = 0.0f; // endTime - startTime; timestamps themselves are not shifted
        std::vector<ModelAnimationSampler> samplers;
        std::vector<ModelAnimationChannel> channels;
    };

    struct ModelResource {
        std::vector<ModelNode> nodes;
        std::vector<ModelMesh> meshes;
        std::vector<ModelMaterial> materials;
        std::vector<ModelImage> images;
        std::vector<ModelTexture> textures;
        std::vector<ModelSampler> samplers;
        std::vector<ModelSkin> skins;
        std::vector<ModelAnimationClip> animations;
        std::vector<ModelScene> scenes;
        uint32_t defaultScene = InvalidModelIndex;
        // Default scene's roots; otherwise first scene's roots; with no scenes,
        // all parentless nodes. All nodes/resources remain available regardless.
        std::vector<uint32_t> sceneRoots;
        std::vector<uint32_t> nodeOrder;   // every node, parent before children
        std::vector<std::string> warnings; // optional extensions deliberately ignored
    };

    struct ModelImportResult {
        bool ok = false;
        std::string error;
        ModelResource model; // empty on failure; no partially imported asset escapes
    };
} // namespace Crucible
