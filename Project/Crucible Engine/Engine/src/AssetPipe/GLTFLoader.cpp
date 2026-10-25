/*************************************************************************
 * GLTFLoader.cpp — extends the original RapidJSON importer by Chewn Thing Kwan.
 * Complete CPU model import plus a compatibility first-primitive projection.
 * Pose evaluation, GPU skinning and gameplay policy are later milestones.
 **************************************************************************/
#include "GLTFLoader.h"
#include "GLTFDocument.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Crucible {
    namespace {
        using Value = rapidjson::Value;
        using GLTFDetail::Accessor;
        using GLTFDetail::Component;
        using GLTFDetail::Shape;

        struct ImportFailure : std::runtime_error {
            using std::runtime_error::runtime_error;
        };
        [[noreturn]] void Error(const std::string &path, const std::string &message) {
            throw ImportFailure(path + ": " + message);
        }
        void Check(bool condition, const std::string &path, const std::string &message) {
            if (!condition)
                Error(path, message);
        }
        std::string At(const std::string &name, size_t index) {
            return name + "[" + std::to_string(index) + "]";
        }
        std::string Text(const Value &value) {
            return {value.GetString(), value.GetStringLength()};
        }
        const Value *Field(const Value &object, const char *name) {
            auto member = object.FindMember(name);
            return member == object.MemberEnd() ? nullptr : &member->value;
        }
        const Value &Array(const Value &object, const char *name, const std::string &path,
                           bool required = false) {
            static const Value empty(rapidjson::kArrayType);
            const auto *value = Field(object, name);
            if (!value) {
                Check(!required, path, "Missing array");
                return empty;
            }
            Check(value->IsArray(), path, "Expected array");
            return *value;
        }
        std::string Name(const Value &object, const std::string &path) {
            const auto *name = Field(object, "name");
            if (!name)
                return {};
            Check(name->IsString(), path + ".name", "Expected string");
            return Text(*name);
        }
        uint32_t Index(const Value &value, size_t count, const std::string &path) {
            Check(value.IsUint(), path, "Expected unsigned integer index");
            const uint32_t index = value.GetUint();
            Check(index < count, path, "Index out of range");
            return index;
        }
        uint32_t Reference(const Value &object, const char *name, size_t count, const std::string &path,
                           bool required = false) {
            const auto *value = Field(object, name);
            if (!value) {
                Check(!required, path + "." + name, "Missing reference");
                return InvalidModelIndex;
            }
            return Index(*value, count, path + "." + name);
        }
        float Number(const Value &value, const std::string &path) {
            Check(value.IsNumber(), path, "Expected number");
            const double number = value.GetDouble();
            Check(std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max(), path,
                  "Expected finite float");
            return static_cast<float>(number);
        }
        float Factor(const Value &object, const char *name, float fallback, const std::string &path,
                     float minimum = 0.0f, float maximum = 1.0f) {
            const auto *field = Field(object, name);
            if (!field)
                return fallback;
            const float value = Number(*field, path + "." + name);
            Check(value >= minimum && value <= maximum, path + "." + name, "Value outside supported range");
            return value;
        }
        template <size_t N> std::array<float, N> Numbers(const Value &value, const std::string &path) {
            Check(value.IsArray() && value.Size() == N, path, "Incorrect numeric array length");
            std::array<float, N> out{};
            for (size_t i = 0; i < N; ++i)
                out[i] = Number(value[static_cast<rapidjson::SizeType>(i)], At(path, i));
            return out;
        }
        void Quaternion(const glm::vec4 &value, const std::string &path) {
            const float length = glm::dot(value, value);
            Check(std::isfinite(length) && std::abs(length - 1.0f) <= 0.002f, path,
                  "Quaternion must have unit length");
        }

        class ModelReader {
          public:
            ModelReader(const GLTFDetail::Document &document, ModelResource &model)
                : m_document(document), m_json(document.Json()), m_model(model) {}

            void Read() {
                for (const char *name : {"nodes", "meshes", "materials", "images", "textures", "samplers",
                                         "skins", "animations", "scenes", "bufferViews", "accessors"}) {
                    const auto &entries = Array(m_json, name, name);
                    for (rapidjson::SizeType i = 0; i < entries.Size(); ++i)
                        Check(entries[i].IsObject(), At(name, i), "Expected object");
                }
                const auto &extensions = Array(m_json, "extensionsUsed", "extensionsUsed");
                for (const auto &extension : extensions.GetArray()) {
                    Check(extension.IsString(), "extensionsUsed", "Expected extension names");
                    m_model.warnings.push_back("Optional extension not implemented; core fallback used: " +
                                               Text(extension));
                }
                // Resolve all declared dense accessors/views, including unused entries.
                // Unsupported format features are never silently hidden in the asset.
                const auto &views = Array(m_json, "bufferViews", "bufferViews");
                for (size_t i = 0; i < views.Size(); ++i) {
                    std::span<const unsigned char> bytes;
                    std::string error;
                    Check(m_document.BufferView(i, bytes, error), At("bufferViews", i), error);
                }
                const auto &accessors = Array(m_json, "accessors", "accessors");
                for (size_t i = 0; i < accessors.Size(); ++i) {
                    (void)ReadAccessor(static_cast<uint32_t>(i), At("accessors", i));
                }
                ReadImagesAndTextures();
                ReadMaterials();
                ReadMeshes();
                ReadNodes();
                ReadSkins();
                ReadAnimations();
                ValidateBindings();
            }

          private:
            Accessor ReadAccessor(uint32_t index, const std::string &path) const {
                Accessor accessor;
                std::string error;
                Check(m_document.GetAccessor(index, accessor, error), path, error);
                return accessor;
            }
            Accessor Attribute(const Value &attributes, const char *name, size_t count,
                               const std::string &path) const {
                const uint32_t index =
                    Reference(attributes, name, Array(m_json, "accessors", "accessors").Size(), path, true);
                auto accessor = ReadAccessor(index, path + "." + name);
                Check(!count || accessor.Count() == count, path + "." + name,
                      "Attribute count differs from POSITION");
                Check(accessor.Offset() % 4 == 0 && accessor.RelativeOffset() % 4 == 0 &&
                          accessor.Stride() % 4 == 0,
                      path + "." + name, "Vertex attributes must be four-byte aligned");
                return accessor;
            }
            std::vector<float> Floats(const Accessor &accessor, const std::string &path) const {
                std::vector<float> values;
                std::string error;
                Check(accessor.Floats(values, error), path, error);
                return values;
            }
            std::vector<uint32_t> Integers(const Accessor &accessor, const std::string &path) const {
                std::vector<uint32_t> values;
                std::string error;
                Check(accessor.Unsigned(values, error), path, error);
                return values;
            }
            static bool FloatOrNormalizedUnsigned(const Accessor &accessor) {
                return accessor.Format() == Component::Float ||
                       (accessor.Normalized() &&
                        (accessor.Format() == Component::UByte || accessor.Format() == Component::UShort));
            }

            void ReadImagesAndTextures() {
                const auto &images = Array(m_json, "images", "images");
                for (size_t i = 0; i < images.Size(); ++i) {
                    const auto path = At("images", i);
                    const auto &image = images[static_cast<rapidjson::SizeType>(i)];
                    Check(!image.HasMember("uri"), path,
                          "External/data-URI images are unsupported; embed PNG/JPEG in a bufferView");
                    const auto *mime = Field(image, "mimeType");
                    if (!mime || !mime->IsString())
                        Error(path, "Embedded images require mimeType");
                    ModelImage out;
                    out.name = Name(image, path);
                    out.mimeType = Text(*mime);
                    Check(out.mimeType == "image/png" || out.mimeType == "image/jpeg", path,
                          "Unsupported image MIME type: " + out.mimeType);
                    uint32_t view = Reference(image, "bufferView",
                                              Array(m_json, "bufferViews", "bufferViews").Size(), path, true);
                    const auto &viewJSON = m_json["bufferViews"][view];
                    Check(!viewJSON.HasMember("byteStride"), path,
                          "Image bufferView cannot declare byteStride");
                    std::span<const unsigned char> bytes;
                    std::string error;
                    Check(m_document.BufferView(view, bytes, error), path, error);
                    out.bytes.assign(bytes.begin(), bytes.end());
                    m_model.images.push_back(std::move(out));
                }
                const auto &samplers = Array(m_json, "samplers", "samplers");
                for (size_t i = 0; i < samplers.Size(); ++i) {
                    const auto path = At("samplers", i);
                    const auto &sampler = samplers[static_cast<rapidjson::SizeType>(i)];
                    ModelSampler out;
                    auto read = [&](const char *name, int fallback, std::initializer_list<int> allowed) {
                        const auto *value = Field(sampler, name);
                        if (!value)
                            return fallback;
                        Check(value->IsInt(), path + "." + name, "Expected sampler enum");
                        int integer = value->GetInt();
                        Check(std::find(allowed.begin(), allowed.end(), integer) != allowed.end(),
                              path + "." + name, "Unsupported sampler enum");
                        return integer;
                    };
                    auto filter = [](int value) {
                        switch (value) {
                        case 9728: return ModelFilter::Nearest;
                        case 9729: return ModelFilter::Linear;
                        case 9984: return ModelFilter::NearestMipNearest;
                        case 9985: return ModelFilter::LinearMipNearest;
                        case 9986: return ModelFilter::NearestMipLinear;
                        case 9987: return ModelFilter::LinearMipLinear;
                        default: return ModelFilter::Default;
                        }
                    };
                    auto wrap = [](int value) {
                        return value == 33071 ? ModelWrap::ClampToEdge :
                            value == 33648 ? ModelWrap::MirroredRepeat : ModelWrap::Repeat;
                    };
                    out.minFilter = filter(read("minFilter", -1, {9728, 9729, 9984, 9985, 9986, 9987}));
                    out.magFilter = filter(read("magFilter", -1, {9728, 9729}));
                    out.wrapS = wrap(read("wrapS", 10497, {33071, 33648, 10497}));
                    out.wrapT = wrap(read("wrapT", 10497, {33071, 33648, 10497}));
                    m_model.samplers.push_back(out);
                }
                const auto &textures = Array(m_json, "textures", "textures");
                for (size_t i = 0; i < textures.Size(); ++i) {
                    const auto path = At("textures", i);
                    const auto &texture = textures[static_cast<rapidjson::SizeType>(i)];
                    ModelTexture out;
                    out.name = Name(texture, path);
                    out.image = Reference(texture, "source", m_model.images.size(), path, true);
                    out.sampler = Reference(texture, "sampler", m_model.samplers.size(), path);
                    m_model.textures.push_back(std::move(out));
                }
            }

            ModelTextureInfo TextureInfo(const Value &object, const char *field,
                                         const std::string &path) const {
                ModelTextureInfo out;
                const auto *slot = Field(object, field);
                if (!slot)
                    return out;
                const auto where = path + "." + field;
                Check(slot->IsObject(), where, "Expected texture info object");
                out.texture = Reference(*slot, "index", m_model.textures.size(), where, true);
                if (const auto *texCoord = Field(*slot, "texCoord")) {
                    Check(texCoord->IsUint(), where, "texCoord must be an unsigned integer");
                    out.texCoord = texCoord->GetUint();
                    Check(out.texCoord == 0, where, "Only TEXCOORD_0 is supported");
                }
                return out;
            }

            void ReadMaterials() {
                const auto &materials = Array(m_json, "materials", "materials");
                for (size_t i = 0; i < materials.Size(); ++i) {
                    const auto path = At("materials", i);
                    const auto &material = materials[static_cast<rapidjson::SizeType>(i)];
                    ModelMaterial out;
                    out.name = Name(material, path);
                    if (const auto *pbr = Field(material, "pbrMetallicRoughness")) {
                        Check(pbr->IsObject(), path, "pbrMetallicRoughness must be an object");
                        if (const auto *color = Field(*pbr, "baseColorFactor")) {
                            const auto values = Numbers<4>(*color, path + ".baseColorFactor");
                            for (float value : values)
                                Check(value >= 0 && value <= 1, path, "baseColorFactor must be within [0,1]");
                            out.baseColorFactor = {values[0], values[1], values[2], values[3]};
                        }
                        out.metallicFactor = Factor(*pbr, "metallicFactor", 1, path);
                        out.roughnessFactor = Factor(*pbr, "roughnessFactor", 1, path);
                        out.baseColor = TextureInfo(*pbr, "baseColorTexture", path);
                        out.metallicRoughness = TextureInfo(*pbr, "metallicRoughnessTexture", path);
                    }
                    out.normal = TextureInfo(material, "normalTexture", path);
                    out.occlusion = TextureInfo(material, "occlusionTexture", path);
                    out.emissive = TextureInfo(material, "emissiveTexture", path);
                    if (const auto *slot = Field(material, "normalTexture"))
                        out.normalScale =
                            Factor(*slot, "scale", 1, path + ".normalTexture",
                                   -std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
                    if (const auto *slot = Field(material, "occlusionTexture"))
                        out.occlusionStrength = Factor(*slot, "strength", 1, path + ".occlusionTexture");
                    if (const auto *emissive = Field(material, "emissiveFactor")) {
                        const auto values = Numbers<3>(*emissive, path + ".emissiveFactor");
                        for (float value : values)
                            Check(value >= 0 && value <= 1, path, "emissiveFactor must be within [0,1]");
                        out.emissiveFactor = {values[0], values[1], values[2]};
                    }
                    if (const auto *mode = Field(material, "alphaMode")) {
                        Check(mode->IsString(), path, "alphaMode must be a string");
                        const auto name = Text(*mode);
                        if (name == "MASK")
                            out.alphaMode = ModelAlphaMode::Mask;
                        else if (name == "BLEND")
                            out.alphaMode = ModelAlphaMode::Blend;
                        else
                            Check(name == "OPAQUE", path, "Unknown alphaMode");
                    }
                    out.alphaCutoff =
                        Factor(material, "alphaCutoff", 0.5f, path, 0, std::numeric_limits<float>::max());
                    if (const auto *sided = Field(material, "doubleSided")) {
                        Check(sided->IsBool(), path, "doubleSided must be boolean");
                        out.doubleSided = sided->GetBool();
                    }
                    m_model.materials.push_back(std::move(out));
                }
            }

            ModelPrimitive ReadPrimitive(const Value &primitive, const std::string &path) {
                Check(primitive.IsObject(), path, "Expected primitive object");
                Check(!primitive.HasMember("targets"), path, "Morph targets are unsupported");
                if (const auto *mode = Field(primitive, "mode"))
                    Check(mode->IsUint() && mode->GetUint() == 4, path,
                          "Only TRIANGLES primitives are supported");
                const auto *attributes = Field(primitive, "attributes");
                if (!attributes || !attributes->IsObject())
                    Error(path, "Missing attributes object");
                for (auto member = attributes->MemberBegin(); member != attributes->MemberEnd(); ++member) {
                    const auto name = Text(member->name);
                    Check(name == "POSITION" || name == "NORMAL" || name == "TEXCOORD_0" ||
                              name == "COLOR_0" || name == "TANGENT" || name == "JOINTS_0" ||
                              name == "WEIGHTS_0",
                          path, "Unsupported vertex attribute: " + name);
                }
                const auto attributePath = path + ".attributes";
                const auto positions = Attribute(*attributes, "POSITION", 0, attributePath);
                Check(positions.Type() == Shape::Vec3 && positions.Format() == Component::Float,
                      attributePath, "POSITION must be FLOAT VEC3");
                const auto count = positions.Count();
                const auto p = Floats(positions, attributePath + ".POSITION");
                ModelPrimitive out;
                out.material = Reference(primitive, "material", m_model.materials.size(), path);
                out.vertices.resize(count);
                for (size_t i = 0; i < count; ++i)
                    out.vertices[i].position = {p[i * 3], p[i * 3 + 1], p[i * 3 + 2]};
                for (const char *name : {"NORMAL", "TEXCOORD_0", "COLOR_0", "TANGENT", "WEIGHTS_0"}) {
                    if (!attributes->HasMember(name))
                        continue;
                    const auto accessor = Attribute(*attributes, name, count, attributePath);
                    const std::string_view semantic(name);
                    const auto where = attributePath + "." + name;
                    const auto components = accessor.Components();
                    const bool vector = semantic == "NORMAL"       ? accessor.Type() == Shape::Vec3
                                        : semantic == "TEXCOORD_0" ? accessor.Type() == Shape::Vec2
                                        : semantic == "COLOR_0"    ? (accessor.Type() == Shape::Vec3 ||
                                                                      accessor.Type() == Shape::Vec4)
                                                                   : accessor.Type() == Shape::Vec4;
                    Check(vector, where, "Incorrect attribute shape");
                    if (semantic == "NORMAL" || semantic == "TANGENT")
                        Check(accessor.Format() == Component::Float, where, "Expected FLOAT attribute");
                    else
                        Check(FloatOrNormalizedUnsigned(accessor), where,
                              "Expected FLOAT or normalized UNSIGNED_BYTE/SHORT");
                    const auto values = Floats(accessor, where);
                    for (size_t i = 0; i < count; ++i) {
                        auto &vertex = out.vertices[i];
                        const auto offset = i * components;
                        if (semantic == "NORMAL") {
                            vertex.normal = {values[offset], values[offset + 1], values[offset + 2]};
                            Check(std::abs(glm::dot(vertex.normal, vertex.normal) - 1.0f) <= 0.002f, where,
                                  "Normals must have unit length");
                        } else if (semantic == "TEXCOORD_0")
                            vertex.uv = {values[offset], values[offset + 1]};
                        else if (semantic == "COLOR_0") {
                            vertex.color = {values[offset], values[offset + 1], values[offset + 2],
                                            components == 4 ? values[offset + 3] : 1.0f};
                            for (int c = 0; c < 4; ++c)
                                Check(vertex.color[c] >= 0 && vertex.color[c] <= 1, where,
                                      "Colors must be within [0,1]");
                        } else if (semantic == "TANGENT") {
                            vertex.tangent = {values[offset], values[offset + 1], values[offset + 2],
                                              values[offset + 3]};
                            Check(std::abs(glm::dot(glm::vec3(vertex.tangent), glm::vec3(vertex.tangent)) -
                                           1.0f) <= 0.002f &&
                                      std::abs(vertex.tangent.w) == 1.0f,
                                  where, "Tangent must have unit xyz and w = +/-1");
                        } else {
                            vertex.weights = {values[offset], values[offset + 1], values[offset + 2],
                                              values[offset + 3]};
                            float sum = 0;
                            for (int c = 0; c < 4; ++c) {
                                Check(vertex.weights[c] >= 0 && vertex.weights[c] <= 1, where,
                                      "Weights must be within [0,1]");
                                sum += vertex.weights[c];
                            }
                            Check(sum > 0 && std::abs(sum - 1.0f) <= 0.01f, where,
                                  "Weights must sum to one (allowing quantization)");
                            vertex.weights /= sum;
                        }
                    }
                }
                out.hasNormals = attributes->HasMember("NORMAL");
                out.hasUVs = attributes->HasMember("TEXCOORD_0");
                out.hasColors = attributes->HasMember("COLOR_0");
                out.hasTangents = attributes->HasMember("TANGENT");
                const bool joints = attributes->HasMember("JOINTS_0"),
                           weights = attributes->HasMember("WEIGHTS_0");
                Check(joints == weights, path, "JOINTS_0 and WEIGHTS_0 must be supplied together");
                out.hasSkinning = joints;
                if (joints) {
                    const auto accessor = Attribute(*attributes, "JOINTS_0", count, attributePath);
                    Check(
                        accessor.Type() == Shape::Vec4 && !accessor.Normalized() &&
                            (accessor.Format() == Component::UByte || accessor.Format() == Component::UShort),
                        path, "JOINTS_0 must be non-normalized UNSIGNED_BYTE/SHORT VEC4");
                    const auto values = Integers(accessor, attributePath + ".JOINTS_0");
                    for (size_t i = 0; i < count; ++i)
                        out.vertices[i].joints = {values[i * 4], values[i * 4 + 1], values[i * 4 + 2],
                                                  values[i * 4 + 3]};
                }
                if (!out.hasTangents)
                    for (auto &vertex : out.vertices) {
                        const auto seed =
                            std::abs(vertex.normal.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                        vertex.tangent = glm::vec4(glm::normalize(glm::cross(seed, vertex.normal)), 1.0f);
                    }
                if (const auto *indices = Field(primitive, "indices")) {
                    const auto index =
                        Index(*indices, Array(m_json, "accessors", "accessors").Size(), path + ".indices");
                    const auto accessor = ReadAccessor(index, path + ".indices");
                    Check(accessor.Type() == Shape::Scalar && !accessor.ExplicitStride(), path,
                          "Indices must be tightly packed unsigned SCALAR");
                    out.indices = Integers(accessor, path + ".indices");
                    const uint32_t restart = accessor.Format() == Component::UByte    ? 255u
                                             : accessor.Format() == Component::UShort ? 65535u
                                                                                      : 0xffffffffu;
                    for (uint32_t value : out.indices)
                        Check(value != restart, path, "Primitive-restart index values are forbidden in glTF");
                } else {
                    Check(count <= std::numeric_limits<uint32_t>::max(), path,
                          "Too many vertices for generated indices");
                    out.indices.resize(count);
                    std::iota(out.indices.begin(), out.indices.end(), 0u);
                }
                Check(out.indices.size() % 3 == 0, path,
                      "Triangle index/vertex count must be a multiple of three");
                for (uint32_t index : out.indices)
                    Check(index < count, path, "Index exceeds vertex count");
                return out;
            }

            void ReadMeshes() {
                const auto &meshes = Array(m_json, "meshes", "meshes");
                for (size_t i = 0; i < meshes.Size(); ++i) {
                    const auto path = At("meshes", i);
                    const auto &mesh = meshes[static_cast<rapidjson::SizeType>(i)];
                    Check(!mesh.HasMember("weights"), path, "Morph weights are unsupported");
                    ModelMesh out;
                    out.name = Name(mesh, path);
                    const auto &primitives = Array(mesh, "primitives", path + ".primitives", true);
                    Check(!primitives.Empty(), path, "Mesh must have at least one primitive");
                    for (size_t j = 0; j < primitives.Size(); ++j)
                        out.primitives.push_back(ReadPrimitive(
                            primitives[static_cast<rapidjson::SizeType>(j)], At(path + ".primitives", j)));
                    m_model.meshes.push_back(std::move(out));
                }
            }

            void ReadNodes() {
                const auto &nodes = Array(m_json, "nodes", "nodes");
                m_model.nodes.resize(nodes.Size());
                const auto skinCount = Array(m_json, "skins", "skins").Size();
                for (uint32_t i = 0; i < nodes.Size(); ++i) {
                    const auto path = At("nodes", i);
                    const auto &node = nodes[i];
                    auto &out = m_model.nodes[i];
                    out.name = Name(node, path);
                    out.mesh = Reference(node, "mesh", m_model.meshes.size(), path);
                    out.skin = Reference(node, "skin", skinCount, path);
                    Check(out.skin == InvalidModelIndex || out.mesh != InvalidModelIndex, path,
                          "A skinned node must reference a mesh");
                    Check(!node.HasMember("weights"), path, "Morph weights are unsupported");
                    Check(!node.HasMember("camera"), path,
                          "Camera objects are outside model import's supported subset");
                    auto &local = out.local;
                    if (const auto *matrix = Field(node, "matrix")) {
                        Check(!node.HasMember("translation") && !node.HasMember("rotation") &&
                                  !node.HasMember("scale"),
                              path, "matrix and TRS cannot coexist");
                        const auto values = Numbers<16>(*matrix, path + ".matrix");
                        for (int c = 0; c < 4; ++c)
                            for (int r = 0; r < 4; ++r)
                                local.matrix[c][r] = values[c * 4 + r];
                        Check(local.matrix[0][3] == 0 && local.matrix[1][3] == 0 && local.matrix[2][3] == 0 &&
                                  local.matrix[3][3] == 1,
                              path, "Node matrix must be affine");
                        local.hasMatrix = true;
                    } else {
                        if (const auto *value = Field(node, "translation")) {
                            const auto v = Numbers<3>(*value, path + ".translation");
                            local.translation = {v[0], v[1], v[2]};
                        }
                        if (const auto *value = Field(node, "scale")) {
                            const auto v = Numbers<3>(*value, path + ".scale");
                            local.scale = {v[0], v[1], v[2]};
                        }
                        if (const auto *value = Field(node, "rotation")) {
                            const auto v = Numbers<4>(*value, path + ".rotation");
                            Quaternion({v[0], v[1], v[2], v[3]}, path + ".rotation");
                            local.rotation = glm::normalize(glm::quat(v[3], v[0], v[1], v[2]));
                        }
                    }
                    const auto &children = Array(node, "children", path + ".children");
                    for (const auto &value : children.GetArray()) {
                        uint32_t child = Index(value, nodes.Size(), path + ".children");
                        Check(child != i && m_model.nodes[child].parent == InvalidModelIndex, path,
                              "Self-parent, duplicate child or multiple parents");
                        m_model.nodes[child].parent = i;
                        out.children.push_back(child);
                    }
                }
                // Iterative parent-first traversal also detects cycles without recursive stack growth.
                m_model.nodeOrder.reserve(nodes.Size());
                for (uint32_t i = 0; i < nodes.Size(); ++i)
                    if (m_model.nodes[i].parent == InvalidModelIndex)
                        m_model.nodeOrder.push_back(i);
                for (size_t i = 0; i < m_model.nodeOrder.size(); ++i) {
                    const auto &children = m_model.nodes[m_model.nodeOrder[i]].children;
                    m_model.nodeOrder.insert(m_model.nodeOrder.end(), children.begin(), children.end());
                }
                Check(m_model.nodeOrder.size() == nodes.Size(), "nodes", "Hierarchy contains a cycle");
                const auto &scenes = Array(m_json, "scenes", "scenes");
                for (size_t i = 0; i < scenes.Size(); ++i) {
                    const auto path = At("scenes", i);
                    const auto &scene = scenes[static_cast<rapidjson::SizeType>(i)];
                    ModelScene out;
                    out.name = Name(scene, path);
                    std::set<uint32_t> unique;
                    for (const auto &root : Array(scene, "nodes", path + ".nodes").GetArray()) {
                        const auto index = Index(root, nodes.Size(), path + ".nodes");
                        Check(m_model.nodes[index].parent == InvalidModelIndex && unique.insert(index).second,
                              path, "Scene nodes must be distinct hierarchy roots");
                        out.roots.push_back(index);
                    }
                    m_model.scenes.push_back(std::move(out));
                }
                m_model.defaultScene = Reference(m_json, "scene", scenes.Size(), "root");
                if (!m_model.scenes.empty())
                    m_model.sceneRoots =
                        m_model.scenes[m_model.defaultScene == InvalidModelIndex ? 0 : m_model.defaultScene]
                            .roots;
                else
                    for (uint32_t i = 0; i < nodes.Size(); ++i)
                        if (m_model.nodes[i].parent == InvalidModelIndex)
                            m_model.sceneRoots.push_back(i);
            }

            void ReadSkins() {
                const auto &skins = Array(m_json, "skins", "skins");
                for (size_t i = 0; i < skins.Size(); ++i) {
                    const auto path = At("skins", i);
                    const auto &skin = skins[static_cast<rapidjson::SizeType>(i)];
                    ModelSkin out;
                    out.name = Name(skin, path);
                    out.skeletonRoot = Reference(skin, "skeleton", m_model.nodes.size(), path);
                    const auto &joints = Array(skin, "joints", path + ".joints", true);
                    Check(!joints.Empty(), path, "Skin requires joints");
                    std::set<uint32_t> unique;
                    uint32_t hierarchyRoot = InvalidModelIndex;
                    for (const auto &joint : joints.GetArray()) {
                        const auto node = Index(joint, m_model.nodes.size(), path + ".joints");
                        Check(unique.insert(node).second, path, "Duplicate skin joint");
                        out.jointNodes.push_back(node);
                        uint32_t root = node;
                        bool skeletonFound = out.skeletonRoot == InvalidModelIndex;
                        for (uint32_t ancestor = node; ancestor != InvalidModelIndex;
                             ancestor = m_model.nodes[ancestor].parent) {
                            root = ancestor;
                            skeletonFound |= ancestor == out.skeletonRoot;
                        }
                        Check(skeletonFound, path, "skeleton must be an ancestor of every joint");
                        Check(hierarchyRoot == InvalidModelIndex || hierarchyRoot == root, path,
                              "Skin joints must share a hierarchy root");
                        hierarchyRoot = root;
                    }
                    out.inverseBindMatrices.assign(out.jointNodes.size(), glm::mat4(1.0f));
                    if (const auto *inverse = Field(skin, "inverseBindMatrices")) {
                        const auto accessor =
                            ReadAccessor(Index(*inverse, Array(m_json, "accessors", "accessors").Size(),
                                               path + ".inverseBindMatrices"),
                                         path);
                        Check(accessor.Type() == Shape::Mat4 && accessor.Format() == Component::Float &&
                                  accessor.Count() >= out.jointNodes.size() && !accessor.ExplicitStride(),
                              path, "Inverse binds require tightly packed FLOAT MAT4, one per joint");
                        const auto values = Floats(accessor, path + ".inverseBindMatrices");
                        for (size_t j = 0; j < out.jointNodes.size(); ++j)
                            for (int c = 0; c < 4; ++c)
                                for (int r = 0; r < 4; ++r)
                                    out.inverseBindMatrices[j][c][r] = values[j * 16 + c * 4 + r];
                    }
                    m_model.skins.push_back(std::move(out));
                }
            }

            void ReadAnimations() {
                const auto &animations = Array(m_json, "animations", "animations");
                const auto accessorCount = Array(m_json, "accessors", "accessors").Size();
                for (size_t i = 0; i < animations.Size(); ++i) {
                    const auto path = At("animations", i);
                    const auto &animation = animations[static_cast<rapidjson::SizeType>(i)];
                    ModelAnimationClip out;
                    out.name = Name(animation, path);
                    out.startTime = std::numeric_limits<float>::max();
                    const auto &samplers = Array(animation, "samplers", path + ".samplers", true);
                    const auto &channels = Array(animation, "channels", path + ".channels", true);
                    Check(!samplers.Empty() && !channels.Empty(), path,
                          "Animation requires samplers and channels");
                    for (size_t j = 0; j < samplers.Size(); ++j) {
                        const auto where = At(path + ".samplers", j);
                        const auto &sampler = samplers[static_cast<rapidjson::SizeType>(j)];
                        Check(sampler.IsObject(), where, "Expected sampler object");
                        ModelAnimationSampler values;
                        if (const auto *interpolation = Field(sampler, "interpolation")) {
                            Check(interpolation->IsString(), where, "interpolation must be a string");
                            const auto mode = Text(*interpolation);
                            if (mode == "STEP")
                                values.interpolation = AnimationInterpolation::Step;
                            else
                                Check(mode == "LINEAR", where, "Unsupported interpolation: " + mode);
                        }
                        const auto input = ReadAccessor(
                            Reference(sampler, "input", accessorCount, where, true), where + ".input");
                        const auto output = ReadAccessor(
                            Reference(sampler, "output", accessorCount, where, true), where + ".output");
                        Check(input.Type() == Shape::Scalar && input.Format() == Component::Float &&
                                  !input.ExplicitStride(),
                              where, "Animation input must be tightly packed FLOAT SCALAR");
                        Check(output.Format() == Component::Float &&
                                  (output.Type() == Shape::Vec3 || output.Type() == Shape::Vec4) &&
                                  !output.ExplicitStride(),
                              where, "TRS animation output must be tightly packed FLOAT VEC3/VEC4");
                        Check(input.Count() == output.Count(), where, "Animation input/output counts differ");
                        values.times = Floats(input, where + ".input");
                        for (size_t k = 0; k < values.times.size(); ++k)
                            Check(values.times[k] >= 0 && (k == 0 || values.times[k] > values.times[k - 1]),
                                  where, "Key times must be nonnegative and strictly increasing");
                        const auto flat = Floats(output, where + ".output");
                        values.components = static_cast<uint32_t>(output.Components());
                        values.values.resize(output.Count(), glm::vec4(0));
                        for (size_t k = 0; k < output.Count(); ++k)
                            for (uint32_t c = 0; c < values.components; ++c)
                                values.values[k][c] = flat[k * values.components + c];
                        out.samplers.push_back(std::move(values));
                    }
                    std::set<std::pair<uint32_t, AnimationTarget>> destinations;
                    for (size_t j = 0; j < channels.Size(); ++j) {
                        const auto where = At(path + ".channels", j);
                        const auto &channel = channels[static_cast<rapidjson::SizeType>(j)];
                        Check(channel.IsObject(), where, "Expected channel object");
                        ModelAnimationChannel value;
                        value.sampler = Reference(channel, "sampler", out.samplers.size(), where, true);
                        const auto *target = Field(channel, "target");
                        if (!target || !target->IsObject())
                            Error(where, "Missing target object");
                        value.node =
                            Reference(*target, "node", m_model.nodes.size(), where + ".target", true);
                        const auto *property = Field(*target, "path");
                        if (!property || !property->IsString())
                            Error(where, "Missing target path");
                        const auto propertyName = Text(*property);
                        if (propertyName == "translation")
                            value.target = AnimationTarget::Translation;
                        else if (propertyName == "rotation")
                            value.target = AnimationTarget::Rotation;
                        else if (propertyName == "scale")
                            value.target = AnimationTarget::Scale;
                        else
                            Error(where, "Unsupported animation target: " + propertyName);
                        Check(!m_model.nodes[value.node].local.hasMatrix, where,
                              "TRS animation cannot target a matrix node");
                        Check(destinations.emplace(value.node, value.target).second, where,
                              "Duplicate node/property animation channel");
                        const auto &sampler = out.samplers[value.sampler];
                        out.startTime = std::min(out.startTime, sampler.times.front());
                        out.endTime = std::max(out.endTime, sampler.times.back());
                        Check(sampler.components == (value.target == AnimationTarget::Rotation ? 4u : 3u),
                              where, "Animation output shape differs from target property");
                        if (value.target == AnimationTarget::Rotation)
                            for (const auto &q : sampler.values)
                                Quaternion(q, where);
                        out.channels.push_back(value);
                    }
                    out.duration = out.endTime - out.startTime;
                    m_model.animations.push_back(std::move(out));
                }
            }

            void ValidateBindings() const {
                for (size_t i = 0; i < m_model.nodes.size(); ++i) {
                    const auto &node = m_model.nodes[i];
                    if (node.skin == InvalidModelIndex)
                        continue;
                    const auto &skin = m_model.skins[node.skin];
                    for (const auto &primitive : m_model.meshes[node.mesh].primitives) {
                        Check(primitive.hasSkinning, At("nodes", i),
                              "Skinned mesh requires JOINTS_0 and WEIGHTS_0 on every primitive");
                        for (const auto &vertex : primitive.vertices)
                            for (int c = 0; c < 4; ++c)
                                Check(vertex.joints[c] < skin.jointNodes.size(), At("nodes", i),
                                      "JOINTS_0 index exceeds this node's skin joint count");
                    }
                }
            }

            const GLTFDetail::Document &m_document;
            const rapidjson::Document &m_json;
            ModelResource &m_model;
        };

        bool ReadFile(const std::filesystem::path &path, std::vector<unsigned char> &bytes,
                      std::string &error) {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) {
                error = "Could not open GLB: " + path.string();
                return false;
            }
            const auto size = file.tellg();
            if (size < 0 || static_cast<uint64_t>(size) > std::numeric_limits<uint32_t>::max()) {
                error = "Invalid GLB file size";
                return false;
            }
            bytes.resize(static_cast<size_t>(size));
            file.seekg(0, std::ios::beg);
            if (!file || (size > 0 && !file.read(reinterpret_cast<char *>(bytes.data()),
                                                 static_cast<std::streamsize>(size)))) {
                error = "Could not read GLB: " + path.string();
                bytes.clear();
                return false;
            }
            return true;
        }
    } // namespace

    ModelImportResult GLTFLoader::LoadModelGLB(std::span<const unsigned char> bytes) {
        ModelImportResult result;
        try {
            GLTFDetail::Document document;
            if (!document.Parse(bytes, result.error))
                return result;
            ModelResource model;
            ModelReader(document, model).Read();
            result.model = std::move(model);
            result.ok = true;
        } catch (const ImportFailure &failure) {
            result.error = failure.what();
        } catch (const std::bad_alloc &) {
            result.error = "Insufficient memory importing GLB";
        } catch (const std::length_error &) {
            result.error = "GLB data exceeds container capacity";
        }
        return result;
    }

    ModelImportResult GLTFLoader::LoadModelGLBFile(const std::filesystem::path &path) {
        ModelImportResult result;
        std::vector<unsigned char> bytes;
        if (!ReadFile(path, bytes, result.error))
            return result;
        return LoadModelGLB(bytes);
    }

    GLTFAsset GLTFLoader::FirstPrimitive(const ModelResource &model) {
        GLTFAsset out;
        if (model.meshes.empty() || model.meshes[0].primitives.empty()) {
            out.error = "Model has no first mesh primitive";
            return out;
        }
        const auto &primitive = model.meshes[0].primitives[0];
        out.meshName = model.meshes[0].name;
        out.mesh.indices = primitive.indices;
        out.mesh.vertices.reserve(primitive.vertices.size());
        for (const auto &source : primitive.vertices) {
            Vertex vertex{};
            vertex.pos = source.position;
            vertex.normal = source.normal;
            vertex.uv = source.uv;
            vertex.color = glm::vec3(source.color);
            vertex.tangent = source.tangent;
            out.mesh.vertices.push_back(vertex);
        }
        if (primitive.material != InvalidModelIndex) {
            if (primitive.material >= model.materials.size()) {
                out.error = "First primitive has invalid material reference";
                return out;
            }
            const auto &source = model.materials[primitive.material];
            GLTFMaterial material;
            material.baseColorFactor = source.baseColorFactor;
            material.emissiveFactor = source.emissiveFactor;
            material.metallicFactor = source.metallicFactor;
            material.roughnessFactor = source.roughnessFactor;
            material.alphaMode = source.alphaMode == ModelAlphaMode::Mask    ? GLTFAlphaMode::Mask
                                 : source.alphaMode == ModelAlphaMode::Blend ? GLTFAlphaMode::Blend
                                                                             : GLTFAlphaMode::Opaque;
            material.alphaCutoff = source.alphaCutoff;
            material.doubleSided = source.doubleSided;
            auto image = [&](ModelTextureInfo info, bool &has, size_t &index) {
                if (info.texture == InvalidModelIndex || info.texture >= model.textures.size())
                    return;
                const auto imageIndex = model.textures[info.texture].image;
                if (imageIndex >= model.images.size())
                    return;
                index = out.images.size();
                out.images.push_back({model.images[imageIndex].bytes});
                has = true;
            };
            // Keep the original texture-resolution order for legacy callers.
            image(source.baseColor, material.hasBaseColor, material.baseColorImage);
            image(source.metallicRoughness, material.hasMetallicRoughness, material.metallicRoughnessImage);
            image(source.normal, material.hasNormal, material.normalImage);
            image(source.occlusion, material.hasOcclusion, material.occlusionImage);
            image(source.emissive, material.hasEmissive, material.emissiveImage);
            out.material = material;
        }
        out.ok = true;
        return out;
    }

    GLTFAsset GLTFLoader::LoadGLB(const std::vector<unsigned char> &bytes) {
        auto model = LoadModelGLB(bytes);
        if (!model.ok) {
            GLTFAsset asset;
            asset.error = std::move(model.error);
            return asset;
        }
        return FirstPrimitive(model.model);
    }

    GLTFAsset GLTFLoader::LoadGLBFile(const std::filesystem::path &path) {
        auto model = LoadModelGLBFile(path);
        if (!model.ok) {
            GLTFAsset asset;
            asset.error = std::move(model.error);
            return asset;
        }
        return FirstPrimitive(model.model);
    }
} // namespace Crucible
