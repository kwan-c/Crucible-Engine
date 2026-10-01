/*************************************************************************
* \file:         GLTFLoader.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                See GLTFLoader.h for the format/scope this parser covers.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "GLTFLoader.h"

#include <cmath>
#include <cstring>
#include <fstream>

#include <rapidjson/document.h>

namespace Crucible {
namespace {

    // ---- glTF enum values this parser understands (see the glTF 2.0 spec, "Accessor") --------

    constexpr int kComponentByte          = 5120;
    constexpr int kComponentUnsignedByte  = 5121;
    constexpr int kComponentShort         = 5122;
    constexpr int kComponentUnsignedShort = 5123;
    constexpr int kComponentUnsignedInt   = 5125;
    constexpr int kComponentFloat         = 5126;

    constexpr uint32_t kGLBMagic     = 0x46546C67u;   // "glTF", little-endian bytes
    constexpr uint32_t kJSONChunkTag = 0x4E4F534Au;   // "JSON"
    constexpr uint32_t kBINChunkTag  = 0x004E4942u;   // "BIN\0"

    std::size_t ComponentsPerElement(std::string const& type) {
        if (type == "SCALAR") return 1;
        if (type == "VEC2")   return 2;
        if (type == "VEC3")   return 3;
        if (type == "VEC4")   return 4;
        return 0;   // MAT2/3/4: not needed by anything this loader reads (positions/normals/
                    // uvs/tangents/indices are all SCALAR or VECn) - treated as unsupported.
    }

    std::size_t ComponentByteSize(int componentType) {
        switch (componentType) {
            case kComponentByte:
            case kComponentUnsignedByte:  return 1;
            case kComponentShort:
            case kComponentUnsignedShort: return 2;
            case kComponentUnsignedInt:
            case kComponentFloat:         return 4;
            default:                      return 0;
        }
    }

    // Reads one accessor's data as floats, normalizing/widening integer component types the way
    // glTF's "normalized" flag requires. componentsPerElement/count describe the result's shape:
    // out.size() == count * componentsPerElement, element i's components at
    // out[i*componentsPerElement .. i*componentsPerElement+componentsPerElement).
    bool ReadAccessorFloats(rapidjson::Document const& doc, int accessorIndex,
                            std::vector<unsigned char> const& binChunk,
                            std::vector<float>& out, std::size_t& componentsPerElement, std::size_t& count,
                            std::string& error)
    {
        rapidjson::Value const& accessors = doc["accessors"];
        if (accessorIndex < 0 || static_cast<rapidjson::SizeType>(accessorIndex) >= accessors.Size()) {
            error = "accessor index out of range"; return false;
        }
        rapidjson::Value const& acc = accessors[static_cast<rapidjson::SizeType>(accessorIndex)];

        if (!acc.HasMember("bufferView")) { error = "sparse/bufferView-less accessors are not supported"; return false; }

        int const componentType = acc["componentType"].GetInt();
        std::string const type  = acc["type"].GetString();
        count = acc["count"].GetUint64();
        std::size_t const byteOffsetAcc = acc.HasMember("byteOffset") ? acc["byteOffset"].GetUint64() : 0;
        bool const normalized = acc.HasMember("normalized") && acc["normalized"].GetBool();

        componentsPerElement = ComponentsPerElement(type);
        std::size_t const componentByteSize = ComponentByteSize(componentType);
        if (componentsPerElement == 0 || componentByteSize == 0) { error = "unsupported accessor type/componentType"; return false; }

        int const bufferViewIndex = acc["bufferView"].GetInt();
        rapidjson::Value const& bv = doc["bufferViews"][static_cast<rapidjson::SizeType>(bufferViewIndex)];
        std::size_t const byteOffsetView = bv.HasMember("byteOffset") ? bv["byteOffset"].GetUint64() : 0;
        std::size_t const elementByteSize = componentsPerElement * componentByteSize;
        std::size_t const stride = bv.HasMember("byteStride") ? bv["byteStride"].GetUint64() : elementByteSize;

        std::size_t const base = byteOffsetView + byteOffsetAcc;
        out.resize(count * componentsPerElement);

        for (std::size_t i = 0; i < count; ++i) {
            std::size_t const elementOffset = base + i * stride;
            for (std::size_t c = 0; c < componentsPerElement; ++c) {
                std::size_t const byteOffset = elementOffset + c * componentByteSize;
                if (byteOffset + componentByteSize > binChunk.size()) { error = "accessor reads past end of buffer"; return false; }

                float value = 0.0f;
                switch (componentType) {
                    case kComponentFloat: {
                        float f; std::memcpy(&f, &binChunk[byteOffset], 4); value = f;
                        break;
                    }
                    case kComponentUnsignedByte: {
                        unsigned char v = binChunk[byteOffset];
                        value = normalized ? static_cast<float>(v) / 255.0f : static_cast<float>(v);
                        break;
                    }
                    case kComponentUnsignedShort: {
                        uint16_t v; std::memcpy(&v, &binChunk[byteOffset], 2);
                        value = normalized ? static_cast<float>(v) / 65535.0f : static_cast<float>(v);
                        break;
                    }
                    default:
                        error = "unsupported componentType for a float attribute";
                        return false;
                }
                out[i * componentsPerElement + c] = value;
            }
        }
        return true;
    }

    // Reads an indices accessor (always unsigned, always tightly packed per the glTF spec - an
    // ELEMENT_ARRAY_BUFFER bufferView never has a byteStride), widened to uint32_t.
    bool ReadAccessorIndices(rapidjson::Document const& doc, int accessorIndex,
                             std::vector<unsigned char> const& binChunk,
                             std::vector<uint32_t>& out, std::string& error)
    {
        rapidjson::Value const& accessors = doc["accessors"];
        if (accessorIndex < 0 || static_cast<rapidjson::SizeType>(accessorIndex) >= accessors.Size()) {
            error = "accessor index out of range"; return false;
        }
        rapidjson::Value const& acc = accessors[static_cast<rapidjson::SizeType>(accessorIndex)];
        if (!acc.HasMember("bufferView")) { error = "sparse index accessors are not supported"; return false; }

        int const componentType = acc["componentType"].GetInt();
        std::size_t const count = acc["count"].GetUint64();
        std::size_t const byteOffsetAcc = acc.HasMember("byteOffset") ? acc["byteOffset"].GetUint64() : 0;

        int const bufferViewIndex = acc["bufferView"].GetInt();
        rapidjson::Value const& bv = doc["bufferViews"][static_cast<rapidjson::SizeType>(bufferViewIndex)];
        std::size_t const byteOffsetView = bv.HasMember("byteOffset") ? bv["byteOffset"].GetUint64() : 0;

        std::size_t const componentByteSize = ComponentByteSize(componentType);
        if (componentByteSize == 0) { error = "unsupported index componentType"; return false; }

        std::size_t const base = byteOffsetView + byteOffsetAcc;
        out.resize(count);

        for (std::size_t i = 0; i < count; ++i) {
            std::size_t const byteOffset = base + i * componentByteSize;
            if (byteOffset + componentByteSize > binChunk.size()) { error = "index accessor reads past end of buffer"; return false; }

            switch (componentType) {
                case kComponentUnsignedByte:  out[i] = binChunk[byteOffset]; break;
                case kComponentUnsignedShort: { uint16_t v; std::memcpy(&v, &binChunk[byteOffset], 2); out[i] = v; break; }
                case kComponentUnsignedInt:   { uint32_t v; std::memcpy(&v, &binChunk[byteOffset], 4); out[i] = v; break; }
                default: error = "index componentType must be unsigned byte/short/int"; return false;
            }
        }
        return true;
    }

    // texture[textureIndex].source -> images[sourceIndex]'s bufferView, copied out as an opaque
    // still-encoded blob. Appends to `images` and returns the new entry's index. External (uri)
    // images are not supported (see GLTFLoader.h's scope-limit note) - on that path this still
    // appends an empty GLTFImage so index bookkeeping stays consistent, and reports the problem
    // through `error` for the caller to decide whether to treat it as fatal.
    std::size_t ResolveEmbeddedImage(rapidjson::Document const& doc, int textureIndex,
                                      std::vector<unsigned char> const& binChunk,
                                      std::vector<GLTFImage>& images, std::string& error)
    {
        rapidjson::Value const& textures = doc["textures"];
        rapidjson::Value const& tex = textures[static_cast<rapidjson::SizeType>(textureIndex)];
        int const sourceIndex = tex["source"].GetInt();
        rapidjson::Value const& img = doc["images"][static_cast<rapidjson::SizeType>(sourceIndex)];

        images.emplace_back();
        std::size_t const newIndex = images.size() - 1;

        if (!img.HasMember("bufferView")) {
            error = "external (uri) images are not supported - GLB-embedded images only";
            return newIndex;
        }

        int const bvIndex = img["bufferView"].GetInt();
        rapidjson::Value const& bv = doc["bufferViews"][static_cast<rapidjson::SizeType>(bvIndex)];
        std::size_t const offset = bv.HasMember("byteOffset") ? bv["byteOffset"].GetUint64() : 0;
        std::size_t const length = bv["byteLength"].GetUint64();

        if (offset + length > binChunk.size()) { error = "image bufferView reads past end of buffer"; return newIndex; }

        images[newIndex].bytes.assign(binChunk.begin() + static_cast<long>(offset),
                                       binChunk.begin() + static_cast<long>(offset + length));
        return newIndex;
    }

    // Parses primitives[0]'s "material" (if present) into a GLTFMaterial. Never fails the whole
    // asset: a malformed/unsupported material just means asset.material stays unset (geometry
    // is still usable on its own - Renderer::GetDefaultMaterial() covers "no material").
    std::optional<GLTFMaterial> ReadMaterial(rapidjson::Document const& doc, int materialIndex,
                                              std::vector<unsigned char> const& binChunk,
                                              std::vector<GLTFImage>& images)
    {
        rapidjson::Value const& materials = doc["materials"];
        if (materialIndex < 0 || static_cast<rapidjson::SizeType>(materialIndex) >= materials.Size()) { return std::nullopt; }
        rapidjson::Value const& mat = materials[static_cast<rapidjson::SizeType>(materialIndex)];

        GLTFMaterial gm;
        std::string unused;   // per-texture resolve errors are non-fatal here; see the file
                               // header's scope-limit note (external images just get skipped).

        if (mat.HasMember("pbrMetallicRoughness")) {
            rapidjson::Value const& pbr = mat["pbrMetallicRoughness"];

            if (pbr.HasMember("baseColorFactor")) {
                rapidjson::Value const& f = pbr["baseColorFactor"];
                gm.baseColorFactor = glm::vec4(f[0].GetFloat(), f[1].GetFloat(), f[2].GetFloat(), f[3].GetFloat());
            }
            if (pbr.HasMember("metallicFactor"))  { gm.metallicFactor  = pbr["metallicFactor"].GetFloat(); }
            if (pbr.HasMember("roughnessFactor")) { gm.roughnessFactor = pbr["roughnessFactor"].GetFloat(); }

            if (pbr.HasMember("baseColorTexture")) {
                int const texIndex = pbr["baseColorTexture"]["index"].GetInt();
                gm.baseColorImage = ResolveEmbeddedImage(doc, texIndex, binChunk, images, unused);
                gm.hasBaseColor = unused.empty();
            }
            if (pbr.HasMember("metallicRoughnessTexture")) {
                int const texIndex = pbr["metallicRoughnessTexture"]["index"].GetInt();
                gm.metallicRoughnessImage = ResolveEmbeddedImage(doc, texIndex, binChunk, images, unused);
                gm.hasMetallicRoughness = unused.empty();
            }
        }

        if (mat.HasMember("normalTexture")) {
            int const texIndex = mat["normalTexture"]["index"].GetInt();
            gm.normalImage = ResolveEmbeddedImage(doc, texIndex, binChunk, images, unused);
            gm.hasNormal = unused.empty();
        }
        if (mat.HasMember("occlusionTexture")) {
            int const texIndex = mat["occlusionTexture"]["index"].GetInt();
            gm.occlusionImage = ResolveEmbeddedImage(doc, texIndex, binChunk, images, unused);
            gm.hasOcclusion = unused.empty();
        }
        if (mat.HasMember("emissiveTexture")) {
            int const texIndex = mat["emissiveTexture"]["index"].GetInt();
            gm.emissiveImage = ResolveEmbeddedImage(doc, texIndex, binChunk, images, unused);
            gm.hasEmissive = unused.empty();
        }
        if (mat.HasMember("emissiveFactor")) {
            rapidjson::Value const& f = mat["emissiveFactor"];
            gm.emissiveFactor = glm::vec3(f[0].GetFloat(), f[1].GetFloat(), f[2].GetFloat());
        }

        return gm;
    }

}   // anonymous namespace

GLTFAsset GLTFLoader::LoadGLB(const std::vector<unsigned char>& fileBytes) {
    GLTFAsset asset;

    // ---- GLB container: 12-byte header + a sequence of length-prefixed chunks ----------------
    if (fileBytes.size() < 12) { asset.error = "file too small to be a GLB"; return asset; }

    uint32_t magic = 0;
    std::memcpy(&magic, &fileBytes[0], 4);
    if (magic != kGLBMagic) { asset.error = "not a GLB file (bad magic)"; return asset; }
    // version (offset 4) and total length (offset 8) are read implicitly by the chunk loop
    // below running off the actual buffer size - neither is otherwise needed here.

    std::string jsonText;
    std::vector<unsigned char> binChunk;

    std::size_t cursor = 12;
    while (cursor + 8 <= fileBytes.size()) {
        uint32_t chunkLength = 0, chunkType = 0;
        std::memcpy(&chunkLength, &fileBytes[cursor], 4);
        std::memcpy(&chunkType, &fileBytes[cursor + 4], 4);
        cursor += 8;

        if (cursor + chunkLength > fileBytes.size()) { asset.error = "GLB chunk runs past end of file"; return asset; }

        if (chunkType == kJSONChunkTag) {
            jsonText.assign(reinterpret_cast<const char*>(&fileBytes[cursor]), chunkLength);
        } else if (chunkType == kBINChunkTag) {
            binChunk.assign(fileBytes.begin() + static_cast<long>(cursor),
                             fileBytes.begin() + static_cast<long>(cursor + chunkLength));
        }
        // Any other chunk type is, per the GLB spec, ignored by readers that don't recognise it.

        cursor += chunkLength;
    }

    if (jsonText.empty()) { asset.error = "GLB has no JSON chunk"; return asset; }

    // ---- JSON chunk ----------------------------------------------------------------------------
    rapidjson::Document doc;
    doc.Parse(jsonText.c_str(), jsonText.size());
    if (doc.HasParseError()) { asset.error = "malformed JSON chunk"; return asset; }

    if (!doc.HasMember("meshes") || doc["meshes"].Empty()) { asset.error = "glTF has no meshes"; return asset; }
    rapidjson::Value const& mesh0 = doc["meshes"][0];
    if (mesh0.HasMember("name") && mesh0["name"].IsString()) { asset.meshName = mesh0["name"].GetString(); }
    if (!mesh0.HasMember("primitives") || mesh0["primitives"].Empty()) { asset.error = "first mesh has no primitives"; return asset; }
    rapidjson::Value const& prim0 = mesh0["primitives"][0];

    // glTF "mode" defaults to 4 (TRIANGLES) when absent; a primitive using strips/fans/points/
    // lines (1,2,3,5,6) would silently render as garbage if read as a plain triangle list, so
    // reject anything else outright rather than guessing.
    if (prim0.HasMember("mode") && prim0["mode"].GetInt() != 4) {
        asset.error = "only triangle-list primitives (mode 4, or omitted) are supported";
        return asset;
    }

    if (!prim0.HasMember("attributes") || !prim0["attributes"].HasMember("POSITION")) {
        asset.error = "primitive has no POSITION attribute"; return asset;
    }
    rapidjson::Value const& attrs = prim0["attributes"];

    std::string err;
    std::vector<float> positions, normals, uvs, tangents;
    std::size_t comp = 0, vertexCount = 0;

    if (!ReadAccessorFloats(doc, attrs["POSITION"].GetInt(), binChunk, positions, comp, vertexCount, err) || comp != 3) {
        asset.error = "POSITION: " + err; return asset;
    }

    bool const hasNormal = attrs.HasMember("NORMAL");
    if (hasNormal) {
        std::size_t c2 = 0, n2 = 0;
        if (!ReadAccessorFloats(doc, attrs["NORMAL"].GetInt(), binChunk, normals, c2, n2, err) || c2 != 3) {
            asset.error = "NORMAL: " + err; return asset;
        }
    }

    bool const hasUV = attrs.HasMember("TEXCOORD_0");
    if (hasUV) {
        std::size_t c2 = 0, n2 = 0;
        if (!ReadAccessorFloats(doc, attrs["TEXCOORD_0"].GetInt(), binChunk, uvs, c2, n2, err) || c2 != 2) {
            asset.error = "TEXCOORD_0: " + err; return asset;
        }
    }

    bool const hasTangent = attrs.HasMember("TANGENT");
    if (hasTangent) {
        std::size_t c2 = 0, n2 = 0;
        if (!ReadAccessorFloats(doc, attrs["TANGENT"].GetInt(), binChunk, tangents, c2, n2, err) || c2 != 4) {
            asset.error = "TANGENT: " + err; return asset;
        }
    }

    asset.mesh.vertices.resize(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        Vertex v{};
        v.pos = glm::vec3(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);

        // Missing NORMAL/TEXCOORD_0: fall back to neutral placeholders rather than failing the
        // whole asset. +Y isn't geometrically correct for an arbitrary mesh, but every Khronos
        // sample asset this loader targets provides NORMAL anyway.
        v.normal = hasNormal ? glm::vec3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2])
                              : glm::vec3(0.0f, 1.0f, 0.0f);
        v.uv     = hasUV     ? glm::vec2(uvs[i * 2 + 0], uvs[i * 2 + 1])
                              : glm::vec2(0.0f, 0.0f);
        v.color  = glm::vec3(1.0f, 1.0f, 1.0f);   // COLOR_0 is not imported (see file header)

        // Missing TANGENT (common for an asset with no normal map - BoxTextured, for one, has
        // none): FIXES A REAL BUG here that shipped in an earlier version of this loader. That
        // version used a single fixed placeholder tangent (1,0,0,1) for every vertex regardless
        // of its normal - exactly Primitives.h's Detail::kFlatCapTangent convention, but that
        // convention is only safe there because each flat cap's normal is known in advance to be
        // perpendicular to it. Applied blindly to an arbitrary imported normal, a fixed (1,0,0)
        // tangent is exactly PARALLEL to a normal of (+-1,0,0) - two of a cube's six faces - and
        // the shader's Gram-Schmidt re-orthogonalization (T - N*dot(N,T)) collapses to a
        // near-zero vector there; normalizing that produces NaN, and NaN propagates through the
        // whole BRDF even where the math should have zeroed it out (IEEE754: NaN * 0 = NaN, not
        // 0), which is what showed up as glitching/garbage on two of BoxTextured's faces.
        // Fixed by deriving a tangent from each vertex's own normal instead: pick any seed axis
        // not parallel to it (+Y unless the normal is itself near +-Y, then +X), and take
        // cross(seed, normal) - guaranteed non-degenerate for every possible normal direction.
        // The resulting tangent's exact direction is otherwise arbitrary and inconsistent across
        // a smooth surface, which only matters for actual normal mapping - moot here, since a
        // material with no TANGENT also has no normal map to be wrong about (Renderer substitutes
        // its flat-normal placeholder texture, whose tangent-space normal is (0,0,1): the TBN
        // matrix's T/B columns are multiplied by that normal map sample's x/y = 0 either way).
        if (hasTangent) {
            v.tangent = glm::vec4(tangents[i * 4 + 0], tangents[i * 4 + 1], tangents[i * 4 + 2], tangents[i * 4 + 3]);
        } else {
            glm::vec3 const seed = (std::abs(v.normal.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            v.tangent = glm::vec4(glm::normalize(glm::cross(seed, v.normal)), 1.0f);
        }

        asset.mesh.vertices[i] = v;
    }

    if (!prim0.HasMember("indices")) { asset.error = "non-indexed primitives are not supported"; return asset; }
    if (!ReadAccessorIndices(doc, prim0["indices"].GetInt(), binChunk, asset.mesh.indices, err)) {
        asset.error = "indices: " + err; return asset;
    }

    if (prim0.HasMember("material")) {
        asset.material = ReadMaterial(doc, prim0["material"].GetInt(), binChunk, asset.images);
    }

    asset.ok = true;
    return asset;
}

GLTFAsset GLTFLoader::LoadGLBFile(const std::filesystem::path& path) {
    GLTFAsset asset;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { asset.error = "could not open file: " + path.string(); return asset; }

    std::streamsize const size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        asset.error = "could not read file: " + path.string();
        return asset;
    }

    return LoadGLB(bytes);
}

}   // NAMESPACE CRUCIBLE
