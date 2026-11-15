/*************************************************************************
  \file       DeferredDestroy.h
  \brief
    A queue for GPU objects that were replaced while the GPU may still be
    using them: Retire(std::move(object)) takes the object, and Release()
    (once per frame) destroys it a few frames later, when no frame that
    could have used it is still in flight.
  \details
    Made for swapping a shader's pipelines while the program runs
    (MaterialShaders::Update): the frames the GPU has not finished still
    draw with the old pipelines.
    Anything movable can be retired - a vk::raii::Pipeline, a std::array of
    them, a vk::raii::ShaderModule, a descriptor set - without the queue
    knowing its type. It is the same waiting rule RenderResources::
    ReleaseDestroyed uses for meshes, textures and materials.

    Declare the queue AFTER everything its contents were created from
    (pools, layouts): members are destroyed in reverse order, so what is
    still waiting in the queue at shutdown is destroyed first, while the
    objects it came from are still alive.

    Not thread-safe: call Retire and Release from the frame thread.
*************************************************************************/
#pragma once

#include "VulkanFrameManager.h"   // MAX_FRAMES_IN_FLIGHT

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Crucible {

    class DeferredDestroyQueue {
    public:
        DeferredDestroyQueue() = default;
        DeferredDestroyQueue(const DeferredDestroyQueue&) = delete;
        DeferredDestroyQueue& operator=(const DeferredDestroyQueue&) = delete;

        /*********************************************************************
         \brief  Takes ownership of an object the GPU may still be using. Its
                 destructor runs inside a later Release(), once every frame
                 that could have used it has finished.
         \param[in] object  Pass with std::move: the queue keeps it.
         *********************************************************************/
        template <typename T>
        void Retire(T&& object) {
            static_assert(!std::is_lvalue_reference_v<T>, "Retire takes ownership: pass std::move(object)");
            using Stored = std::remove_cv_t<std::remove_reference_t<T>>;
            m_retired.push_back({ m_releaseCount, std::make_unique<Holder<Stored>>(std::move(object)) });
        }

        /*********************************************************************
         \brief  Destroys what was retired long enough ago. Call once per
                 frame, after BeginFrame() has waited on the frame's fence
                 (next to RenderResources::ReleaseDestroyed).
         *********************************************************************/
        void Release() {
            ++m_releaseCount;

            // Retired at count c (during frame n): frames up to n may still be on the GPU. Each
            // later call has waited for one more frame's fence, so after MAX_FRAMES_IN_FLIGHT + 1
            // more calls every one of them has finished - one frame of margin on top of what is
            // strictly needed. (The same rule as RenderResources::ReleaseDestroyed.)
            std::erase_if(m_retired, [&](const Retired& retired) {
                return m_releaseCount - retired.frame > VulkanFrameManager::MAX_FRAMES_IN_FLIGHT;
            });
        }

        /// Destroys everything now. Only after the device has gone idle (shutdown).
        void ReleaseAll() { m_retired.clear(); }

        /// Objects still waiting.
        size_t PendingCount() const { return m_retired.size(); }

    private:
        struct HolderBase {
            virtual ~HolderBase() = default;
        };
        template <typename T>
        struct Holder final : HolderBase {
            explicit Holder(T&& o) : object(std::move(o)) {}
            T object;
        };
        struct Retired {
            uint64_t                    frame = 0;   // Release() calls so far when it was retired
            std::unique_ptr<HolderBase> object;
        };

        std::vector<Retired> m_retired;
        uint64_t             m_releaseCount = 0;   // Release() calls so far
    };

}   // NAMESPACE CRUCIBLE
