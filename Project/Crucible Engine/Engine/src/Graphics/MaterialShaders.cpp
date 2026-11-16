/*************************************************************************
  \file       MaterialShaders.cpp
  \brief
    See MaterialShaders.h.
*************************************************************************/
#include "Graphics/MaterialShaders.h"

#include "Graphics/Vulkan/DeferredDestroy.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Diagnostics/Logger.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace Crucible {

    namespace {

        // SPIR-V, as far as CheckCode needs it (see the SPIR-V specification, "Binary Form").
        constexpr uint32_t kSpirvMagic       = 0x07230203u;
        constexpr size_t   kSpirvHeaderWords = 5;
        constexpr uint32_t kOpEntryPoint     = 15;   // execution model, entry point id, name, interface ids...
        constexpr uint32_t kOpFunction       = 54;   // entry points are all declared before the first function
        constexpr uint32_t kModelVertex      = 0;
        constexpr uint32_t kModelFragment    = 4;

        // A SPIR-V literal string: UTF-8, nul-terminated, packed four bytes to a word (low byte first).
        std::string LiteralString(std::span<const uint32_t> words) {
            std::string text;
            for (const uint32_t word : words) {
                for (uint32_t shift = 0; shift < 32; shift += 8) {
                    const char c = static_cast<char>((word >> shift) & 0xFFu);
                    if (c == '\0') { return text; }
                    text.push_back(c);
                }
            }
            return text;
        }

        constexpr uint32_t kSampleCounts[MaterialShaders::kLevels] = { 1, 2, 4 };

    }   // anonymous namespace

    // ------------------------------------------------------------------------------------------------------------- //
    //  CHECKING CODE
    // ------------------------------------------------------------------------------------------------------------- //

    bool MaterialShaders::CheckCode(std::span<const uint32_t> spirv, std::string& error) {

        if (spirv.size() < kSpirvHeaderWords || spirv.front() != kSpirvMagic) {
            error = "not a SPIR-V module (" + std::to_string(spirv.size()) + " words)";
            return false;
        }

        // Walk the instructions (word count in the high half of the first word, opcode in the
        // low half) as far as the first function, noting the entry points on the way.
        bool hasVertex = false, hasFragment = false;
        for (size_t i = kSpirvHeaderWords; i < spirv.size();) {
            const uint32_t wordCount = spirv[i] >> 16;
            const uint32_t opcode    = spirv[i] & 0xFFFFu;
            if (wordCount == 0 || i + wordCount > spirv.size()) {
                error = "the SPIR-V module is damaged (an instruction runs past its end)";
                return false;
            }
            if (opcode == kOpFunction) { break; }
            if (opcode == kOpEntryPoint && wordCount >= 4) {
                const uint32_t    model = spirv[i + 1];
                const std::string name  = LiteralString(spirv.subspan(i + 3, wordCount - 3));
                if (model == kModelVertex   && name == kVertexEntry)   { hasVertex = true; }
                if (model == kModelFragment && name == kFragmentEntry) { hasFragment = true; }
            }
            i += wordCount;
        }

        if (!hasVertex || !hasFragment) {
            error = std::string("it has no ") + (!hasVertex ? kVertexEntry : kFragmentEntry) +
                    " entry point - a mesh shader is a surface function followed by #include \"SurfaceTemplate.slang\"";
            return false;
        }
        return true;

    }   // MaterialShaders::CheckCode()

    // ------------------------------------------------------------------------------------------------------------- //
    //  SHADERS
    // ------------------------------------------------------------------------------------------------------------- //

    MaterialShaders::~MaterialShaders() {

        {
            const std::lock_guard lock(m_mutex);
            m_stop = true;
            m_jobs.clear();
        }
        m_wake.notify_all();
        if (m_worker.joinable()) { m_worker.join(); }   // before the pipelines, and the pass the builder belongs to, go

    }   // MaterialShaders::~MaterialShaders()

    void MaterialShaders::Build(std::span<const uint32_t> spirv, uint32_t level, vk::raii::Pipeline& solid, vk::raii::Pipeline& cutout) const {

        // The module is only needed while the pipelines are being made.
        const vk::raii::ShaderModule shaderModule = CreateShaderModule(*m_context, spirv);
        m_builder(*shaderModule, level, solid, cutout);

    }   // MaterialShaders::Build()

    void MaterialShaders::Init(VulkanContext& context, Builder builder, std::string defaultName, std::vector<uint32_t> defaultSpirv) {

        m_context = &context;
        m_builder = std::move(builder);

        std::string error;
        if (!CheckCode(defaultSpirv, error)) {
            throw std::runtime_error("[Renderer | Shaders] " + defaultName + ".spv cannot be used: " + error +
                                     " - try rebuilding to recompile the shaders");
        }

        // Every level, now: whatever else is missing or still being built is drawn with these.
        Entry entry;
        entry.name    = std::move(defaultName);
        entry.code    = std::make_shared<const std::vector<uint32_t>>(std::move(defaultSpirv));
        entry.version = 1;
        for (uint32_t level = 0; level < kLevels; ++level) {
            Build(*entry.code, level, entry.levels[level].solid, entry.levels[level].cutout);
            entry.levels[level].built = entry.levels[level].requested = entry.version;
        }
        m_entries.clear();
        m_entries.push_back(std::move(entry));

    }   // MaterialShaders::Init()

    MaterialShaderHandle MaterialShaders::Find(std::string_view name) const {

        for (uint32_t i = 0; i < Count(); ++i) {
            if (m_entries[i].name == name) { return MaterialShaderHandle{ i }; }
        }
        return {};

    }   // MaterialShaders::Find()

    MaterialShaderHandle MaterialShaders::Add(std::string name, std::vector<uint32_t> spirv) {

        Entry entry;
        entry.name = std::move(name);
        if (!spirv.empty()) {
            std::string error;
            if (CheckCode(spirv, error)) {
                entry.code    = std::make_shared<const std::vector<uint32_t>>(std::move(spirv));
                entry.version = 1;
            } else {
                CRU_ERROR("[Renderer | Shaders] {} cannot be used: {}. Its materials draw with the default shader.", entry.name, error);
            }
        }
        m_entries.push_back(std::move(entry));
        return MaterialShaderHandle{ Count() - 1 };

    }   // MaterialShaders::Add()

    bool MaterialShaders::SetCode(MaterialShaderHandle shader, std::vector<uint32_t> spirv, std::string& error) {

        if (shader.id >= Count()) {
            error = "that is not a loaded material shader";
            return false;
        }
        if (!CheckCode(spirv, error)) { return false; }

        // The next Update() sees a version nothing has been built for and queues it. The
        // pipelines already there keep drawing until then.
        Entry& entry = m_entries[shader.id];
        entry.code = std::make_shared<const std::vector<uint32_t>>(std::move(spirv));
        ++entry.version;
        return true;

    }   // MaterialShaders::SetCode()

    const std::string& MaterialShaders::Name(MaterialShaderHandle shader) const {

        static const std::string none;
        return shader.id < Count() ? m_entries[shader.id].name : none;

    }   // MaterialShaders::Name()

    const MaterialShaderParams& MaterialShaders::Params(MaterialShaderHandle shader) const {

        static const MaterialShaderParams none;
        return shader.id < Count() ? m_entries[shader.id].params : none;

    }   // MaterialShaders::Params()

    void MaterialShaders::SetParams(MaterialShaderHandle shader, MaterialShaderParams params) {

        if (shader.id < Count()) { m_entries[shader.id].params = std::move(params); }

    }   // MaterialShaders::SetParams()

    bool MaterialShaders::IsBuilt(MaterialShaderHandle shader, uint32_t level) const {

        return shader.id < Count() && level < kLevels && m_entries[shader.id].levels[level].built != 0;

    }   // MaterialShaders::IsBuilt()

    vk::Pipeline MaterialShaders::Pipeline(MaterialShaderHandle shader, uint32_t level, bool cutout) const {

        level = std::min(level, kLevels - 1);
        const Level& own = m_entries[shader.id < Count() ? shader.id : 0].levels[level];
        const Level& use = own.built != 0 ? own : m_entries[0].levels[level];   // the built-in shader always is
        return cutout ? *use.cutout : *use.solid;

    }   // MaterialShaders::Pipeline()

    // ------------------------------------------------------------------------------------------------------------- //
    //  BUILDING
    // ------------------------------------------------------------------------------------------------------------- //

    void MaterialShaders::Update(uint32_t levelInUse, DeferredDestroyQueue& retired) {

        // 1. What the background thread finished since the last frame.
        std::vector<Built> finished;
        {
            const std::lock_guard lock(m_mutex);
            finished.swap(m_built);
        }
        for (Built& built : finished) {
            Entry& entry = m_entries[built.shader];
            Level& level = entry.levels[built.level];

            if (!built.error.empty()) {
                // Not tried again until the code changes. Whatever was drawing keeps drawing.
                entry.failed = std::max(entry.failed, built.version);
                CRU_ERROR("[Renderer | Shaders] The pipelines of {} could not be built: {}. {}", entry.name, built.error,
                          level.built != 0 ? "Still drawing with its previous version." : "Its materials draw with the default shader.");
                continue;
            }
            if (built.version <= level.built) { continue; }   // older than what is drawn already: dropped, never used

            // Frames already recorded still draw with the old pair: the queue destroys them
            // once the GPU has finished with those frames.
            if (level.built != 0) {
                retired.Retire(std::move(level.solid));
                retired.Retire(std::move(level.cutout));
            }
            level.solid  = std::move(built.solid);
            level.cutout = std::move(built.cutout);
            level.built  = built.version;
            CRU_INFO("[Renderer | Shaders] {} is ready ({} sample{}).", entry.name, kSampleCounts[built.level], built.level == 0 ? "" : "s");
        }

        // 2. What the level in use is missing: a shader that was just added, new code for one,
        //    or the MSAA setting having changed.
        if (levelInUse >= kLevels) { return; }
        bool queued = false;
        for (uint32_t i = 0; i < Count(); ++i) {
            Entry& entry = m_entries[i];
            Level& level = entry.levels[levelInUse];
            if (!entry.code || entry.version == entry.failed || level.requested == entry.version) { continue; }
            level.requested = entry.version;

            const std::lock_guard lock(m_mutex);
            // Only the newest code is worth building: drop a waiting build of the same pipelines.
            std::erase_if(m_jobs, [&](const Job& job) { return job.shader == i && job.level == levelInUse; });
            m_jobs.push_back(Job{ i, entry.version, levelInUse, entry.code });
            if (!m_worker.joinable()) { m_worker = std::thread([this] { WorkerMain(); }); }
            queued = true;
        }
        if (queued) { m_wake.notify_one(); }

    }   // MaterialShaders::Update()

    void MaterialShaders::WorkerMain() {

        std::unique_lock lock(m_mutex);
        for (;;) {
            m_wake.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
            if (m_stop) { return; }

            Job job = std::move(m_jobs.front());
            m_jobs.pop_front();
            lock.unlock();

            // The slow part - the driver compiling the shader for this GPU - with nothing locked.
            Built built;
            built.shader  = job.shader;
            built.version = job.version;
            built.level   = job.level;
            try {
                Build(*job.code, job.level, built.solid, built.cutout);
            } catch (const std::exception& e) {
                built.solid  = nullptr;
                built.cutout = nullptr;
                built.error  = e.what();
                if (built.error.empty()) { built.error = "unknown error"; }
            }

            lock.lock();
            m_built.push_back(std::move(built));
        }

    }   // MaterialShaders::WorkerMain()

}   // NAMESPACE CRUCIBLE
