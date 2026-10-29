#include "AssetPipe/AssetManager.h"
#include "AssetPipe/GLTFLoader.h"
#include "Resources/CompiledModel.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace fs = std::filesystem;
using namespace Crucible;

namespace {
    void Check(bool ok,const std::string& error) { if (!ok) throw std::runtime_error(error); }

    std::vector<unsigned char> Read(const fs::path& path) {
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        Check(bool(file),"Cannot open "+path.string());
        const auto size=file.tellg();
        Check(size>=0 && uint64_t(size)<=MaxCompiledModelBytes,"File exceeds 1 GiB limit: "+path.string());
        std::vector<unsigned char> bytes(static_cast<size_t>(size));
        file.seekg(0);
        Check(bool(file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))),"Cannot read "+path.string());
        return bytes;
    }

    void Replace(const fs::path& temporary,const fs::path& destination) {
#ifdef _WIN32
        Check(MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,
              "Cannot replace "+destination.string()+" (Windows error "+std::to_string(GetLastError())+")");
#else
        fs::rename(temporary,destination);
#endif
    }

    void Write(const fs::path& path,const std::vector<unsigned char>& bytes,
               const AssetEntry& source,const std::string& virtualPath) {
        fs::create_directories(path.parent_path());
        auto temporary=path.parent_path()/("."+path.filename().string()+".tmp");
        auto meta=AssetRegistry::MetaPathFor(path);
        auto temporaryMeta=path.parent_path()/("."+path.filename().string()+".meta.tmp");
        try {
            {
                std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
                Check(bool(file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))),"Cannot write "+temporary.string());
                file.close(); Check(!file.fail(),"Cannot flush "+temporary.string());
            }
            Check(Read(temporary)==bytes,"Compiled file read-back differs");
            AssetMeta metadata; metadata.guid=source.guid;
            metadata.contenthash=AssetRegistry::HashFileContents(temporary);
            Check(AssetRegistry::WriteMeta(temporaryMeta,metadata,virtualPath),"Cannot write compiled metadata");
            Replace(temporary,path);
            Replace(temporaryMeta,meta);
        } catch (...) {
            std::error_code ignored; fs::remove(temporary,ignored); fs::remove(temporaryMeta,ignored);
            throw;
        }
    }

    void Summary(const std::string& name,const ModelResource& m) {
        size_t primitives=0,vertices=0,indices=0;
        for (const auto& mesh:m.meshes) for (const auto& p:mesh.primitives) {
            ++primitives; vertices+=p.vertices.size(); indices+=p.indices.size();
        }
        std::cout<<name<<": nodes="<<m.nodes.size()<<" meshes="<<m.meshes.size()
                 <<" primitives="<<primitives<<" vertices="<<vertices<<" indices="<<indices
                 <<" materials="<<m.materials.size()<<" skins="<<m.skins.size()<<" clips="<<m.animations.size()<<'\n';
        for (size_t i=0;i<m.animations.size();++i)
            std::cout<<"  clip["<<i<<"] "<<m.animations[i].name<<" duration="<<m.animations[i].duration
                     <<" channels="<<m.animations[i].channels.size()<<'\n';
        for (const auto& warning:m.warnings) std::cout<<"  warning: "<<warning<<'\n';
    }

    void UniqueGuids(const AssetManager& assets) {
        std::unordered_set<uint64_t> ids;
        for (const auto& entry:assets.GetAll())
            Check(ids.insert(entry.guid.m_Value).second,"Duplicate asset GUID in "+assets.GetAssetRoot()+": "+entry.virtualPath);
    }

    int CompileTree(const fs::path& sourceRoot,const fs::path& outputRoot,bool verify) {
        Check(fs::is_directory(sourceRoot) && fs::is_directory(outputRoot),"Source and runtime asset roots must exist");
        const auto sourcePath=fs::weakly_canonical(sourceRoot),outputPath=fs::weakly_canonical(outputRoot);
        auto within=[](const fs::path& a,const fs::path& b) {
            auto relative=a.lexically_relative(b);
            return !relative.empty() && *relative.begin()!="..";
        };
        Check(!within(sourcePath,outputPath) && !within(outputPath,sourcePath),"Source and runtime roots must be separate, non-nested directories");
        AssetManager sources,outputs;
        Check(sources.Init(sourcePath.string()) && outputs.Init(outputPath.string()),"Cannot index asset roots");
        UniqueGuids(sources); UniqueGuids(outputs);
        size_t built=0,unchanged=0,failed=0;
        std::unordered_set<std::string> destinations;
        for (const auto* source:sources.GetByType(AssetType::Model)) {
            try {
                Check(source->extension==".glb","Unsupported source format "+source->extension+"; only embedded GLB is currently supported");
                fs::path relative=fs::path(source->virtualPath); relative.replace_extension(".cmodel");
                if (const auto* existing=outputs.FindByGuid(source->guid)) {
                    Check(existing->extension==".cmodel","GUID already belongs to a non-compiled runtime asset: "+existing->virtualPath);
                    relative=existing->virtualPath; // Stable runtime path survives a source rename.
                }
                const auto target=fs::weakly_canonical(outputPath/relative);
                Check(within(target,outputPath),"Output escapes runtime asset root");
                Check(destinations.insert(AssetManager::ToLower(target.string())).second,"Multiple sources map to "+target.string());
                if (fs::exists(target)) {
                    AssetMeta meta;
                    Check(AssetRegistry::ReadMeta(AssetRegistry::MetaPathFor(target),meta) && meta.guid==source->guid,
                          "Refusing to overwrite a compiled asset with a different GUID: "+target.string());
                }
                const auto sourceBytes=Read(source->absolutePath);
                // Source fingerprints must be byte-exact: the registry's text-
                // tolerant hash deliberately folds CRLF, even in binary files.
                uint64_t sourceHash=1469598103934665603ull;
                for (auto byte:sourceBytes) { sourceHash^=byte; sourceHash*=1099511628211ull; }
                CompiledModelInfo info{CompiledModelVersion,source->guid.m_Value,sourceHash};
                if (!verify && fs::exists(target)) {
                    CompiledModelInfo cached;
                    auto decoded=DecodeCompiledModel(Read(target),info.assetGuid,&cached);
                    if (decoded.ok && cached.sourceHash==info.sourceHash) { ++unchanged; continue; }
                }
                auto imported=GLTFLoader::LoadModelGLB(sourceBytes);
                Check(imported.ok,source->virtualPath+": "+imported.error);
                auto compiled=EncodeCompiledModel(imported.model,info);
                auto decoded=DecodeCompiledModel(compiled,info.assetGuid);
                Check(decoded.ok,"Compiled round trip failed: "+decoded.error);
                Check(EncodeCompiledModel(decoded.model,info)==compiled,"Compiled round trip changed model fields");
                if (verify) {
                    Check(fs::exists(target) && Read(target)==compiled,"Compiled resource is missing/stale/different; rebuild assets first: "+target.string());
                } else Write(target,compiled,*source,relative.generic_string());
                Summary((verify?"Verified ":"Compiled ")+relative.generic_string(),decoded.model);
                ++built;
            } catch (const std::exception& e) { std::cerr<<"[ModelCompiler] "<<source->virtualPath<<": "<<e.what()<<'\n'; ++failed; }
        }
        std::cout<<"[ModelCompiler] "<<(verify?"verified=":"compiled=")<<built<<" unchanged="<<unchanged<<" failed="<<failed<<'\n';
        return failed?1:0;
    }
}

int main(int argc,char** argv) {
    try {
        if (argc==4 && (std::string(argv[1])=="--compile-tree" || std::string(argv[1])=="--verify-tree"))
            return CompileTree(argv[2],argv[3],std::string(argv[1])=="--verify-tree");
        if (argc==3 && std::string(argv[1])=="--inspect") {
            CompiledModelInfo info;
            auto decoded=DecodeCompiledModel(Read(argv[2]),0,&info);
            Check(decoded.ok,decoded.error);
            std::cout<<"Crucible model v"<<info.version<<" GUID="<<AssetRegistry::GuidToString(xresource::instance_guid{info.assetGuid})<<'\n';
            Summary(argv[2],decoded.model); return 0;
        }
        std::cerr<<"Usage: CrucibleModelCompiler --compile-tree <AssetsSource> <Assets>\n"
                   "       CrucibleModelCompiler --verify-tree <AssetsSource> <Assets>\n"
                   "       CrucibleModelCompiler --inspect <file.cmodel>\n";
        return 2;
    } catch (const std::exception& e) { std::cerr<<"[ModelCompiler] "<<e.what()<<'\n'; return 1; }
}
