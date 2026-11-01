#include "CompiledModel.h"
#include "CompiledTexture.h"   // CompiledTextureMimeType

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace Crucible {
namespace {
    constexpr std::array<unsigned char, 8> Magic{'C','R','M','O','D','E','L',0};
    constexpr size_t HeaderSize = 48;
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

    void Require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }
    uint64_t Hash(std::span<const unsigned char> bytes) {
        uint64_t hash = 1469598103934665603ull; // Same convention as AssetRegistry.
        for (auto value : bytes) { hash ^= value; hash *= 1099511628211ull; }
        return hash;
    }

    // One schema used by both archives, so additions cannot accidentally be
    // written in a different order from the reader. Changing it bumps the version.
    template<class A, class T> void Fields(A& a, T& v) {
        using V = std::remove_cv_t<T>;
        if constexpr (std::is_same_v<V, ModelLocalTransform>)
            a(v.translation,v.rotation,v.scale,v.matrix,v.hasMatrix);
        else if constexpr (std::is_same_v<V, ModelNode>)
            a(v.name,v.parent,v.children,v.local,v.mesh,v.skin);
        else if constexpr (std::is_same_v<V, ModelScene>) a(v.name,v.roots);
        else if constexpr (std::is_same_v<V, ModelVertex>)
            a(v.position,v.normal,v.uv,v.color,v.tangent,v.joints,v.weights);
        else if constexpr (std::is_same_v<V, ModelPrimitive>)
            a(v.vertices,v.indices,v.material,v.hasNormals,v.hasUVs,v.hasColors,v.hasTangents,v.hasSkinning);
        else if constexpr (std::is_same_v<V, ModelMesh>) a(v.name,v.primitives);
        else if constexpr (std::is_same_v<V, ModelImage>) a(v.name,v.mimeType,v.bytes);
        else if constexpr (std::is_same_v<V, ModelSampler>) a(v.minFilter,v.magFilter,v.wrapS,v.wrapT);
        else if constexpr (std::is_same_v<V, ModelTexture>) a(v.name,v.image,v.sampler);
        else if constexpr (std::is_same_v<V, ModelTextureInfo>) a(v.texture,v.texCoord);
        else if constexpr (std::is_same_v<V, ModelMaterial>)
            a(v.name,v.baseColorFactor,v.emissiveFactor,v.metallicFactor,v.roughnessFactor,
              v.baseColor,v.metallicRoughness,v.normal,v.occlusion,v.emissive,
              v.normalScale,v.occlusionStrength,v.alphaMode,v.alphaCutoff,v.doubleSided);
        else if constexpr (std::is_same_v<V, ModelSkin>) a(v.name,v.skeletonRoot,v.jointNodes,v.inverseBindMatrices);
        else if constexpr (std::is_same_v<V, ModelAnimationSampler>) a(v.interpolation,v.times,v.values,v.components);
        else if constexpr (std::is_same_v<V, ModelAnimationChannel>) a(v.sampler,v.node,v.target);
        else if constexpr (std::is_same_v<V, ModelAnimationClip>)
            a(v.name,v.startTime,v.endTime,v.duration,v.samplers,v.channels);
        else if constexpr (std::is_same_v<V, ModelResource>)
            a(v.nodes,v.meshes,v.materials,v.images,v.textures,v.samplers,v.skins,v.animations,
              v.scenes,v.defaultScene,v.sceneRoots,v.nodeOrder,v.warnings);
        else static_assert(!sizeof(T), "Missing compiled model field schema");
    }

    struct Writer {
        std::vector<unsigned char> bytes;
        template<class... T> void operator()(const T&... values) { (One(values), ...); }
        void Room(size_t size) {
            Require(size <= MaxCompiledModelBytes - bytes.size(), "Compiled model exceeds 1 GiB limit");
        }
        void One(uint32_t value) { Room(4); for (int i=0;i<4;++i) bytes.push_back(static_cast<unsigned char>(value >> (i*8))); }
        void One(uint64_t value) { Room(8); for (int i=0;i<8;++i) bytes.push_back(static_cast<unsigned char>(value >> (i*8))); }
        void One(float value) { Require(std::isfinite(value), "Non-finite model value"); One(std::bit_cast<uint32_t>(value)); }
        void One(bool value) { Room(1); bytes.push_back(value ? 1 : 0); }
        void One(const std::string& value) {
            Require(value.size() <= UINT32_MAX, "Model string too large");
            One(static_cast<uint32_t>(value.size())); Room(value.size());
            bytes.insert(bytes.end(),value.begin(),value.end());
        }
        template<class T> void One(const std::vector<T>& values) {
            Require(values.size() <= UINT32_MAX, "Model array too large");
            One(static_cast<uint32_t>(values.size()));
            if constexpr (std::is_same_v<T,unsigned char>) {
                Room(values.size()); bytes.insert(bytes.end(),values.begin(),values.end());
            } else for (const auto& value: values) One(value);
        }
        template<glm::length_t N, class T, glm::qualifier Q> void One(const glm::vec<N,T,Q>& value) {
            for (glm::length_t i=0;i<N;++i) One(value[i]);
        }
        void One(const glm::quat& q) { (*this)(q.x,q.y,q.z,q.w); }
        void One(const glm::mat4& m) { for (int i=0;i<4;++i) One(m[i]); }
        template<class T> void One(const T& value) {
            if constexpr (std::is_enum_v<T>) One(static_cast<uint32_t>(value));
            else Fields(*this,value);
        }
    };

    struct Reader {
        std::span<const unsigned char> bytes;
        size_t pos = 0, allocation = 0;
        template<class... T> void operator()(T&... values) { (One(values), ...); }
        void Need(size_t size) const { Require(size <= bytes.size()-pos, "Truncated compiled model"); }
        void Allocate(size_t count, size_t elementSize) {
            Require(count <= (MaxCompiledModelBytes-allocation)/elementSize, "Compiled model allocation limit exceeded");
            allocation += count*elementSize;
        }
        void One(uint32_t& value) { Need(4); value=0; for (int i=0;i<4;++i) value |= uint32_t(bytes[pos++]) << (i*8); }
        void One(uint64_t& value) { Need(8); value=0; for (int i=0;i<8;++i) value |= uint64_t(bytes[pos++]) << (i*8); }
        void One(float& value) {
            uint32_t bits; One(bits); value=std::bit_cast<float>(bits);
            Require(std::isfinite(value), "Non-finite compiled model value");
        }
        void One(bool& value) { Need(1); Require(bytes[pos]<=1,"Invalid compiled boolean"); value=bytes[pos++]!=0; }
        void One(std::string& value) {
            uint32_t count; One(count); Need(count); Allocate(count,1);
            value.assign(reinterpret_cast<const char*>(bytes.data()+pos),count); pos+=count;
        }
        template<class T> void One(std::vector<T>& values) {
            uint32_t count; One(count);
            // Every element encodes at least one byte; bound heap growth as well
            // as file access before constructing even the first nested object.
            Need(count); Allocate(count,sizeof(T)); values.resize(count);
            if constexpr (std::is_same_v<T,unsigned char>) {
                std::copy_n(bytes.begin()+pos,count,values.begin()); pos+=count;
            } else for (auto& value: values) One(value);
        }
        template<glm::length_t N, class T, glm::qualifier Q> void One(glm::vec<N,T,Q>& value) {
            for (glm::length_t i=0;i<N;++i) One(value[i]);
        }
        void One(glm::quat& q) { (*this)(q.x,q.y,q.z,q.w); }
        void One(glm::mat4& m) { for (int i=0;i<4;++i) One(m[i]); }
        template<class T> void One(T& value) {
            if constexpr (std::is_enum_v<T>) { uint32_t code; One(code); value=static_cast<T>(code); }
            else Fields(*this,value);
        }
    };

    bool Ref(uint32_t i,size_t size) { return i!=InvalidModelIndex && i<size; }
    bool Optional(uint32_t i,size_t size) { return i==InvalidModelIndex || i<size; }
    bool Unit(const glm::vec4& value) { return std::abs(glm::dot(value,value)-1.0f)<=0.002f; }

    void Validate(const ModelResource& m) {
        Require(Optional(m.defaultScene,m.scenes.size()),"Invalid default scene");
        Require(m.nodeOrder.size()==m.nodes.size(),"Incomplete node traversal");
        std::vector<bool> visited(m.nodes.size(),false), childSeen(m.nodes.size(),false);
        for (auto index:m.nodeOrder) {
            Require(Ref(index,m.nodes.size()) && !visited[index],"Invalid/duplicate node traversal index");
            const auto& n=m.nodes[index];
            Require(Optional(n.parent,m.nodes.size()),"Invalid node parent");
            Require(n.parent==InvalidModelIndex || visited[n.parent],"Cyclic or unordered hierarchy");
            Require(Optional(n.mesh,m.meshes.size()) && Optional(n.skin,m.skins.size()),"Invalid node mesh/skin");
            Require(n.skin==InvalidModelIndex || n.mesh!=InvalidModelIndex,"Skin without a mesh");
            Require(Unit({n.local.rotation.x,n.local.rotation.y,n.local.rotation.z,n.local.rotation.w}),"Invalid node quaternion");
            if (n.local.hasMatrix)
                Require(n.local.matrix[0][3]==0 && n.local.matrix[1][3]==0 && n.local.matrix[2][3]==0 && n.local.matrix[3][3]==1,"Non-affine node matrix");
            visited[index]=true;
            for (auto child:n.children) {
                Require(Ref(child,m.nodes.size()) && !childSeen[child] && m.nodes[child].parent==index,
                        "Invalid/duplicate child relationship");
                childSeen[child]=true;
            }
        }
        for (size_t i=0;i<m.nodes.size();++i)
            Require(childSeen[i]==(m.nodes[i].parent!=InvalidModelIndex),"Missing reciprocal parent/child relationship");
        auto roots=[&](const auto& list) {
            std::unordered_set<uint32_t> seen;
            for (auto i:list) Require(Ref(i,m.nodes.size()) && m.nodes[i].parent==InvalidModelIndex && seen.insert(i).second,"Invalid scene root");
        };
        for (const auto& scene:m.scenes) roots(scene.roots);
        roots(m.sceneRoots);
        if (!m.scenes.empty()) Require(m.sceneRoots==m.scenes[m.defaultScene==InvalidModelIndex?0:m.defaultScene].roots,"Selected scene roots disagree");
        else Require(m.sceneRoots.size()==static_cast<size_t>(std::count_if(m.nodes.begin(),m.nodes.end(),
                         [](const auto& n) { return n.parent==InvalidModelIndex; })),"Missing scene root");

        for (const auto& sampler:m.samplers)
            Require(sampler.minFilter<=ModelFilter::LinearMipLinear && sampler.magFilter<=ModelFilter::Linear &&
                    sampler.wrapS<=ModelWrap::MirroredRepeat && sampler.wrapT<=ModelWrap::MirroredRepeat,"Invalid texture sampler enum");
        for (const auto& image:m.images)
            Require(!image.bytes.empty() && (image.mimeType==CompiledTextureMimeType ||
                    image.mimeType=="image/png" || image.mimeType=="image/jpeg"),"Unsupported/empty compiled image");
        for (const auto& texture:m.textures)
            Require(Ref(texture.image,m.images.size()) && Optional(texture.sampler,m.samplers.size()),"Invalid texture reference");
        auto slot=[&](const ModelTextureInfo& s) {
            Require(Optional(s.texture,m.textures.size()) && s.texCoord==0,"Invalid texture slot");
        };
        for (const auto& material:m.materials) {
            slot(material.baseColor); slot(material.metallicRoughness); slot(material.normal); slot(material.occlusion); slot(material.emissive);
            Require(material.alphaMode<=ModelAlphaMode::Blend,"Invalid material alpha mode");
        }
        for (const auto& mesh:m.meshes) {
            Require(!mesh.primitives.empty(),"Empty mesh");
            for (const auto& p:mesh.primitives) {
                Require(!p.vertices.empty() && !p.indices.empty() && p.indices.size()%3==0,"Invalid triangle primitive");
                Require(Optional(p.material,m.materials.size()),"Invalid primitive material");
                for (auto i:p.indices) Require(i<p.vertices.size(),"Vertex index out of range");
                for (const auto& v:p.vertices) {
                    float sum=0;
                    for (int j=0;j<4;++j) { Require(v.weights[j]>=0,"Negative skin weight"); sum+=v.weights[j]; }
                    Require(p.hasSkinning ? std::abs(sum-1.0f)<0.002f : sum==0,"Invalid skin weights");
                }
            }
        }
        for (const auto& skin:m.skins) {
            Require(!skin.jointNodes.empty() && skin.jointNodes.size()==skin.inverseBindMatrices.size(),"Invalid skin/inverse binds");
            Require(Optional(skin.skeletonRoot,m.nodes.size()),"Invalid skeleton root");
            std::unordered_set<uint32_t> seen;
            for (auto joint:skin.jointNodes) Require(Ref(joint,m.nodes.size()) && seen.insert(joint).second,"Invalid/duplicate joint node");
        }
        for (const auto& node:m.nodes) if (node.mesh!=InvalidModelIndex) {
            for (const auto& p:m.meshes[node.mesh].primitives) {
                Require(node.skin==InvalidModelIndex || p.hasSkinning,"Skinned node references a primitive without weights");
                // The same weighted mesh may also be instanced without a skin.
                if (p.hasSkinning && node.skin!=InvalidModelIndex) for (const auto& v:p.vertices) for (int j=0;j<4;++j)
                    Require(v.joints[j]<m.skins[node.skin].jointNodes.size(),"Joint index outside bound skin");
            }
        }
        for (const auto& clip:m.animations) {
            Require(!clip.channels.empty() && !clip.samplers.empty(),"Empty animation clip");
            Require(clip.startTime>=0 && clip.endTime>=clip.startTime &&
                    std::abs(clip.duration-(clip.endTime-clip.startTime))<0.0001f,"Invalid clip time range");
            for (const auto& s:clip.samplers) {
                Require(s.interpolation<=AnimationInterpolation::Step,"Unsupported animation interpolation");
                Require(!s.times.empty() && s.times.size()==s.values.size() && (s.components==3 || s.components==4),"Invalid animation key arrays");
                for (size_t i=0;i<s.times.size();++i)
                    Require(s.times[i]>=0 && (i==0 || s.times[i]>s.times[i-1]),"Invalid animation timestamps");
            }
            std::unordered_set<uint64_t> targets;
            float start=std::numeric_limits<float>::max(),end=0;
            for (const auto& c:clip.channels) {
                Require(Ref(c.node,m.nodes.size()) && Ref(c.sampler,clip.samplers.size()) && c.target<=AnimationTarget::Scale,"Invalid animation channel");
                Require(!m.nodes[c.node].local.hasMatrix,"TRS animation targets a matrix node");
                Require(targets.insert((uint64_t(c.node)<<32)|uint32_t(c.target)).second,"Duplicate animation channel");
                const auto& s=clip.samplers[c.sampler];
                Require(s.components==(c.target==AnimationTarget::Rotation?4u:3u),"Animation channel type mismatch");
                if (c.target==AnimationTarget::Rotation) for (const auto& v:s.values) Require(Unit(v),"Invalid rotation key");
                start=std::min(start,s.times.front()); end=std::max(end,s.times.back());
            }
            Require(clip.startTime==start && clip.endTime==end,"Clip bounds disagree with channels");
        }
    }
}

std::vector<unsigned char> EncodeCompiledModel(const ModelResource& model,const CompiledModelInfo& info) {
    Require(info.version==CompiledModelVersion,"Unsupported compiled model writer version");
    Require(info.assetGuid!=0 && (info.assetGuid&1)!=0,"Invalid model asset GUID");
    Validate(model);
    Writer writer;
    writer.bytes.insert(writer.bytes.end(),Magic.begin(),Magic.end());
    writer(info.version,uint32_t(0),uint64_t(0),uint64_t(0),info.assetGuid,info.sourceHash);
    writer(model);
    Writer sizes;
    sizes(uint64_t(writer.bytes.size()-HeaderSize),Hash(std::span(writer.bytes).subspan(HeaderSize)));
    std::copy(sizes.bytes.begin(),sizes.bytes.end(),writer.bytes.begin()+16);
    return std::move(writer.bytes);
}

ModelImportResult DecodeCompiledModel(std::span<const unsigned char> bytes,uint64_t expectedGuid,CompiledModelInfo* info) {
    ModelImportResult result;
    try {
        Require(bytes.size()>=HeaderSize && bytes.size()<=MaxCompiledModelBytes,"Invalid compiled model size");
        Require(std::equal(Magic.begin(),Magic.end(),bytes.begin()),"Not a Crucible compiled model; compile source assets first");
        Reader reader{bytes}; reader.pos=8;
        CompiledModelInfo header;
        uint32_t flags; uint64_t payloadSize,checksum;
        reader(header.version,flags,payloadSize,checksum,header.assetGuid,header.sourceHash);
        Require(header.version==CompiledModelVersion,"Unsupported compiled model version; rebuild assets with the current compiler");
        Require(flags==0,"Unsupported compiled model flags");
        Require(header.assetGuid!=0 && (header.assetGuid&1)!=0 && (!expectedGuid || header.assetGuid==expectedGuid),"Compiled model GUID does not match its asset metadata");
        Require(payloadSize==bytes.size()-HeaderSize,"Compiled model payload length mismatch");
        Require(Hash(bytes.subspan(HeaderSize))==checksum,"Compiled model checksum mismatch");
        ModelResource model; reader(model);
        Require(reader.pos==bytes.size(),"Trailing compiled model data");
        Validate(model);
        result.model=std::move(model); result.ok=true;
        if (info) *info=header;
    } catch (const std::exception& error) { result.error=error.what(); }
    return result;
}
}
