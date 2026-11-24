/*************************************************************************
  \file       MaterialParamBuffer.h
  \brief
    The parameter buffer: every material's values for its shader's own
    parameters (MaterialShaderParams), one float4 per parameter, all
    materials one after another. Set 0, binding 11 - materialParams in
    Surface.slang, read with MaterialParam(slot).
  \details
    Each material that has parameters owns a run of float4s (a range); the
    scene pass pushes where it starts (PushConstants::paramOffset), so the
    shader's parameter n is materialParams[paramOffset + n].

    - MaterialParamRanges hands out and takes back the ranges. Nothing in
      it is about Vulkan.
    - MaterialParamBuffer keeps the values on the CPU and one GPU buffer
      per frame in flight, like the lights: Upload(frame) copies the values
      into that frame's buffer if they changed since it was last filled, so
      an Inspector edit is a write into the CPU copy and nothing else - no
      descriptor is rewritten and nothing waits for the GPU.

    The buffers have a fixed size (kCapacity float4s), so the descriptors
    that point at them never change. A material that asks for a range when
    the buffer is full gets none and draws with offset 0: wrong values, but
    nothing out of bounds - and that is reported.

    Frame thread only.
*************************************************************************/
#pragma once

#include "Math/Math.h"
#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanResource.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Crucible {

    /*************************************************************************
     \brief  Hands out runs of slots from [0, capacity): first fit among the
             freed runs, else from the unused end. Freed runs next to each
             other are joined, and a freed run at the end gives the end back.
     *************************************************************************/
    class MaterialParamRanges {
    public:
        static constexpr uint32_t kNone = 0xFFFFFFFFu;

        explicit MaterialParamRanges(uint32_t capacity = 0) : m_capacity(capacity) {}

        /// A run of `count` slots: its first slot, or kNone if there is no room (or count is 0).
        uint32_t Allocate(uint32_t count);

        /// Gives a run from Allocate back. (offset kNone or count 0: nothing happens.)
        void Free(uint32_t offset, uint32_t count);

        uint32_t Capacity() const { return m_capacity; }
        uint32_t End() const      { return m_end; }      ///< one past the last slot in use
        uint32_t InUse() const    { return m_inUse; }    ///< slots handed out now

    private:
        struct Run { uint32_t offset, count; };

        uint32_t         m_capacity = 0;
        uint32_t         m_end = 0;       // everything from here on has never been handed out (or was given back)
        uint32_t         m_inUse = 0;
        std::vector<Run> m_free;          // freed runs below m_end, by offset, never touching each other
    };

    class MaterialParamBuffer {
    public:
        /// float4s in the buffer: 256 KB per frame in flight.
        static constexpr uint32_t kCapacity = 16384;
        static constexpr uint32_t kNone     = MaterialParamRanges::kNone;

        /// Creates the buffers (one per frame in flight, kCapacity float4s each, zeroed). Call once.
        void Init(VulkanContext& context, uint32_t framesInFlight);

        /// A range of `count` float4s for one material: where it starts, or kNone (full, or count 0).
        uint32_t Allocate(uint32_t count) { return m_ranges.Allocate(count); }
        void     Free(uint32_t offset, uint32_t count) { m_ranges.Free(offset, count); }

        /// Sets the values from `offset` on. Reaches the GPU at each frame's next Upload().
        void Write(uint32_t offset, std::span<const glm::vec4> values);

        /*********************************************************************
         \brief  Copies the values into frame `frameIndex`'s buffer if they
                 changed since it was last filled. Call once per frame, after
                 BeginFrame() has waited on that frame index's fence (the
                 GPU is then done reading that buffer).
         *********************************************************************/
        void Upload(uint32_t frameIndex);

        size_t     BufferCount() const         { return m_buffers.size(); }
        vk::Buffer Buffer(size_t frame) const  { return *m_buffers[frame].GetBuffer(); }
        uint32_t   InUse() const               { return m_ranges.InUse(); }

    private:
        MaterialParamRanges       m_ranges{ kCapacity };
        std::vector<glm::vec4>    m_values;       // the CPU copy, kCapacity long
        uint64_t                  m_change = 1;   // +1 per Write()
        std::vector<VulkanBuffer> m_buffers;      // per frame in flight
        std::vector<void*>        m_mapped;
        std::vector<uint64_t>     m_uploaded;     // m_change when that frame's buffer was last filled
    };

}   // NAMESPACE CRUCIBLE
