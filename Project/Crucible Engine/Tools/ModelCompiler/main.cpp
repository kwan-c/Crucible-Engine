#include "AssetPipe/AssetManager.h"
#include "AssetPipe/GLTFLoader.h"
#include "Resources/CompiledModel.h"
#include "ModelBuild.h"     // Tools/Common: the compile itself, shared with the editor's Asset Browser
#include "AssetBuildReport.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;
using namespace Crucible;

namespace {
    void Check(bool ok,const std::string& error) { if (!ok) throw std::runtime_error(error); }

    using ModelBuild::ReadFile;

    void Summary(const std::string& name,const ModelResource& m) {
        std::cout<<name<<": "<<ModelBuild::Summary(m)<<'\n';
        for (size_t i=0;i<m.animations.size();++i)
            std::cout<<"  clip["<<i<<"] "<<m.animations[i].name<<" duration="<<m.animations[i].duration
                     <<" channels="<<m.animations[i].channels.size()<<'\n';
        for (const auto& warning:m.warnings) std::cout<<"  warning: "<<warning<<'\n';
    }

    // compiledOnly: in Resources a compiled file shares its GUID with the source copy Cook may
    // have put next to it (CannonTex.bill and CannonTex.png), so only compiled files must differ.
    void UniqueGuids(const AssetManager& assets,bool compiledOnly=false) {
        std::unordered_set<uint64_t> ids;
        for (const auto& entry:assets.GetAll())
            if (!compiledOnly || AssetManager::IsCompiledExtension(entry.extension))
                Check(ids.insert(entry.guid.m_Value).second,"Duplicate asset GUID in "+assets.GetAssetRoot()+": "+entry.virtualPath);
    }

    // problems: each model that failed, for the --report file.
    int CompileTree(const fs::path& sourceRoot,const fs::path& outputRoot,bool verify,std::vector<AssetBuildReport::Problem>& problems) {
        Check(fs::is_directory(sourceRoot) && fs::is_directory(outputRoot),"Source and runtime asset roots must exist");
        const auto sourcePath=fs::weakly_canonical(sourceRoot),outputPath=fs::weakly_canonical(outputRoot);
        auto within=[](const fs::path& a,const fs::path& b) {
            auto relative=a.lexically_relative(b);
            return !relative.empty() && *relative.begin()!="..";
        };
        Check(!within(sourcePath,outputPath) && !within(outputPath,sourcePath),"Source and runtime roots must be separate, non-nested directories");
        AssetManager sources,outputs;
        Check(sources.Init(sourcePath.string()) && outputs.Init(outputPath.string()),"Cannot index asset roots");
        UniqueGuids(sources); UniqueGuids(outputs,true);
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
                const auto sourceBytes=ReadFile(source->absolutePath);
                const uint64_t guid=source->guid.m_Value;
                const uint64_t sourceHash=ModelBuild::SourceHash(sourceBytes);
                if (!verify && ModelBuild::IsUpToDate(target,guid,sourceHash)) { ++unchanged; continue; }
                auto compiled=ModelBuild::Compile(sourceBytes,guid,sourceHash);
                if (verify) {
                    Check(fs::exists(target) && ReadFile(target)==compiled.bytes,"Compiled resource is missing/stale/different; rebuild assets first: "+target.string());
                } else ModelBuild::Write(target,compiled.bytes,guid,relative.generic_string());
                Summary((verify?"Verified ":"Compiled ")+relative.generic_string(),compiled.model);
                ++built;
            } catch (const std::exception& e) {
                std::cerr<<"[ModelCompiler] "<<source->virtualPath<<": "<<e.what()<<'\n'; ++failed;
                problems.push_back({source->virtualPath,e.what()});
            }
        }
        std::cout<<"[ModelCompiler] "<<(verify?"verified=":"compiled=")<<built<<" unchanged="<<unchanged<<" failed="<<failed<<'\n';
        return failed?1:0;
    }
}

int main(int argc,char** argv) {
    try {
        // --compile-tree <Assets> <Resources> [--report <file>]: with --report, models that fail
        // are written to <file> instead of failing the build (see AssetBuildReport.h).
        const bool tree=argc>=4 && (std::string(argv[1])=="--compile-tree" || std::string(argv[1])=="--verify-tree");
        const bool report=tree && argc==6 && std::string(argv[4])=="--report";
        if (tree && (argc==4 || report)) {
            std::vector<AssetBuildReport::Problem> problems;
            int result=0;
            try { result=CompileTree(argv[2],argv[3],std::string(argv[1])=="--verify-tree",problems); }
            catch (const std::exception& e) {
                if (!report) throw;
                std::cerr<<"[ModelCompiler] "<<e.what()<<'\n';
                problems.push_back({"Assets (all models)",e.what()});
            }
            if (!report) return result;
            if (!AssetBuildReport::Write(argv[5],problems)) { std::cerr<<"[ModelCompiler] Cannot write report "<<argv[5]<<'\n'; return 1; }
            if (!problems.empty())
                std::cout<<"[ModelCompiler] warning: "<<problems.size()<<" model(s) failed to compile. The build carries on; "
                           "the editor lists them and blocks Play until they are fixed.\n";
            return 0;
        }
        if (argc==3 && std::string(argv[1])=="--inspect") {
            CompiledModelInfo info;
            auto decoded=DecodeCompiledModel(ReadFile(argv[2]),0,&info);
            Check(decoded.ok,decoded.error);
            std::cout<<"Crucible model v"<<info.version<<" GUID="<<AssetRegistry::GuidToString(xresource::instance_guid{info.assetGuid})<<'\n';
            Summary(argv[2],decoded.model); return 0;
        }
        std::cerr<<"Usage: CrucibleModelCompiler --compile-tree <Assets> <Resources> [--report <file>]\n"
                   "       CrucibleModelCompiler --verify-tree <Assets> <Resources> [--report <file>]\n"
                   "       CrucibleModelCompiler --inspect <file.cmodel>\n";
        return 2;
    } catch (const std::exception& e) { std::cerr<<"[ModelCompiler] "<<e.what()<<'\n'; return 1; }
}
