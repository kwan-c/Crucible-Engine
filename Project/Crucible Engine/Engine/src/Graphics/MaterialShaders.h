/*************************************************************************
  \file       MaterialShaders.h
  \brief
    The mesh shaders materials are drawn with, and the scene pipelines
    built from each: the built-in one (DefaultShader.slang) and any number
    of others - a second hand-written surface, later every shader graph.
  \details
    A shader here is compiled code (SPIR-V with the entry points vertMain
    and fragMain, made of a surface function + SurfaceTemplate.slang) and a
    name. Many materials share one shader; a material names its shader by
    MaterialShaderHandle (MaterialData::shader, Invalid = the built-in one).

    Pipelines are built per MSAA level - a solid and a cut-out one, the two
    ScenePass draws meshes with - and only for the level in use:
    - The built-in shader is built for every level at Init(), on the
      calling thread. It is what everything else falls back to, so it must
      always be there.
    - Every other shader, and every reload, is built on a background
      thread. Until a shader's pipelines for the level in use are ready,
      its materials draw with the built-in shader (or with the shader's own
      previous version, when it is being reloaded): the frame never waits
      for the driver.
    - A build that fails is reported once and not tried again until the
      shader's code changes.

    Update() is the one call per frame: it takes in what the background
    thread has finished (old pipelines go to the DeferredDestroyQueue, as
    frames in flight may still draw with them) and queues what the level in
    use still needs.

    All shaders share ScenePass's pipeline layout, so switching shader
    between two draws is one pipeline bind; set 0 stays bound.

    Everything except the background thread's own work is frame-thread only.
*************************************************************************/
#pragma once

#include "MaterialShaderHandle.h"
#include "Vulkan/VulkanContext.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Crucible {

    class DeferredDestroyQueue;

    class MaterialShaders {
    public:
        /// MSAA levels a pipeline can be built for: 1, 2, 4 samples (RenderLayouts::kMsaaLevels).
        static constexpr uint32_t kLevels = 3;

        /// The entry points every mesh shader has (SurfaceTemplate.slang).
        static constexpr const char* kVertexEntry   = "vertMain";
        static constexpr const char* kFragmentEntry = "fragMain";

        /*********************************************************************
         \brief  Builds one MSAA level's two scene pipelines from a shader
                 module: ScenePass::BuildMeshPipelines. Called on the
                 background thread as well as the frame thread, so it must
                 only read state that no longer changes.
         *********************************************************************/
        using Builder = std::function<void(vk::ShaderModule shader, uint32_t level, vk::raii::Pipeline& solid, vk::raii::Pipeline& cutout)>;

        MaterialShaders() = default;
        ~MaterialShaders();   // stops the background thread (waits for a build that is running)
        MaterialShaders(const MaterialShaders&) = delete;
        MaterialShaders& operator=(const MaterialShaders&) = delete;

        /*********************************************************************
         \brief  Registers the built-in shader as handle 0 and builds its
                 pipelines for every MSAA level, now. Call once, after the
                 pass that owns `builder` has its layout.
         \param[in] context       The VulkanContext to create modules through.
         \param[in] builder       See Builder.
         \param[in] defaultName   The built-in shader's name ("DefaultShader").
         \param[in] defaultSpirv  Its compiled code.
         \throws If the built-in shader cannot be built: nothing could be drawn.
         *********************************************************************/
        void Init(VulkanContext& context, Builder builder, std::string defaultName, std::vector<uint32_t> defaultSpirv);

        /// The built-in shader's handle (what an Invalid handle is drawn with).
        static MaterialShaderHandle Default() { return MaterialShaderHandle{ 0 }; }

        /// The shader registered under `name`, or an Invalid handle.
        MaterialShaderHandle Find(std::string_view name) const;

        /*********************************************************************
         \brief  Registers a shader. Its pipelines are built in the
                 background from the next Update() on.
         \param[in] name   Unique; use Find() first.
         \param[in] spirv  Its compiled code, or empty if there is none yet
                           (its materials draw with the built-in shader
                           until SetCode() supplies some).
         \return Its handle. Shaders are never removed, so it stays valid.
         *********************************************************************/
        MaterialShaderHandle Add(std::string name, std::vector<uint32_t> spirv);

        /*********************************************************************
         \brief  Replaces a shader's code (a reload). Its materials keep
                 drawing with the pipelines they have until the new ones are
                 built.
         \param[in] shader  The shader; Default() reloads the built-in one.
         \param[in] spirv   The new compiled code.
         \param[out] error  Why it was refused, when it was.
         \return false if `spirv` is not a module with both entry points, or
                 the handle is not one of ours. Nothing changes then.
         *********************************************************************/
        bool SetCode(MaterialShaderHandle shader, std::vector<uint32_t> spirv, std::string& error);

        /*********************************************************************
         \brief  Call once per frame, before recording: swaps in the
                 pipelines the background thread has finished and queues
                 builds for whatever the level in use is missing.
         \param[in] levelInUse   MSAA level the scene is drawn at this frame
                                 (RenderLayouts::MsaaIndex).
         \param[in,out] retired  Takes the pipelines that were replaced.
         *********************************************************************/
        void Update(uint32_t levelInUse, DeferredDestroyQueue& retired);

        /*********************************************************************
         \brief  The pipeline to draw a material with: its shader's, or the
                 built-in shader's while that is not built for this level.
         \param[in] shader  MaterialData::shader (Invalid = built-in).
         \param[in] level   MSAA level of the target being drawn into.
         \param[in] cutout  true for cut-out materials (alphaCutoff > 0).
         *********************************************************************/
        vk::Pipeline Pipeline(MaterialShaderHandle shader, uint32_t level, bool cutout) const;

        /// How many shaders there are; their handles are 0 .. Count() - 1.
        uint32_t Count() const { return static_cast<uint32_t>(m_entries.size()); }

        /// A shader's name. Empty for a handle that is not one of ours (Invalid included).
        const std::string& Name(MaterialShaderHandle shader) const;

        /// Whether the shader's own pipelines exist for this level (false = it draws with the built-in ones).
        bool IsBuilt(MaterialShaderHandle shader, uint32_t level) const;

        /*********************************************************************
         \brief  Whether `spirv` is a SPIR-V module that has both mesh entry
                 points. A pipeline made from a module without them is
                 undefined behaviour, not an error, so this is checked first.
         \param[out] error  What is wrong, when something is.
         *********************************************************************/
        static bool CheckCode(std::span<const uint32_t> spirv, std::string& error);

    private:
        using Code = std::shared_ptr<const std::vector<uint32_t>>;

        struct Level {
            vk::raii::Pipeline solid  = nullptr;
            vk::raii::Pipeline cutout = nullptr;   // the same with kCutout on
            uint32_t built     = 0;                // version of the code these were built from (0 = none)
            uint32_t requested = 0;                // newest version a build was queued for
        };
        struct Entry {
            std::string name;
            Code        code;                      // null = no compiled code yet
            uint32_t    version = 0;               // +1 each time the code changes; 0 = no code
            uint32_t    failed  = 0;               // newest version whose build failed (not retried)
            std::array<Level, kLevels> levels;
        };
        struct Job {                               // frame thread -> background thread
            uint32_t shader = 0, version = 0, level = 0;
            Code     code;
        };
        struct Built {                             // background thread -> frame thread
            uint32_t shader = 0, version = 0, level = 0;
            vk::raii::Pipeline solid  = nullptr;
            vk::raii::Pipeline cutout = nullptr;
            std::string        error;              // empty = built
        };

        void Build(std::span<const uint32_t> spirv, uint32_t level, vk::raii::Pipeline& solid, vk::raii::Pipeline& cutout) const;
        void WorkerMain();

        VulkanContext*     m_context = nullptr;
        Builder            m_builder;
        std::deque<Entry>  m_entries;              // by handle id; 0 = the built-in shader. Frame thread only.

        std::mutex              m_mutex;           // guards the fields below
        std::condition_variable m_wake;            // background thread: a job, or stop
        std::deque<Job>         m_jobs;
        std::vector<Built>      m_built;
        std::thread             m_worker;          // started by the first job
        bool                    m_stop = false;
    };

}   // NAMESPACE CRUCIBLE
