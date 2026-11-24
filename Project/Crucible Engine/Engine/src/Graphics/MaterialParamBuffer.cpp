/*************************************************************************
  \file       MaterialParamBuffer.cpp
  \brief
    See MaterialParamBuffer.h.
*************************************************************************/
#include "Graphics/MaterialParamBuffer.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Crucible {

    static_assert(sizeof(glm::vec4) == 16, "the parameter buffer is an array of float4 (StructuredBuffer<float4> in Surface.slang)");

    // ------------------------------------------------------------------------------------------------------------- //
    //  RANGES
    // ------------------------------------------------------------------------------------------------------------- //

    uint32_t MaterialParamRanges::Allocate(uint32_t count) {

        if (count == 0) { return kNone; }

        // First fit among what was given back...
        for (size_t i = 0; i < m_free.size(); ++i) {
            Run& run = m_free[i];
            if (run.count < count) { continue; }
            const uint32_t offset = run.offset;
            if (run.count == count) {
                m_free.erase(m_free.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                run.offset += count;
                run.count  -= count;
            }
            m_inUse += count;
            return offset;
        }

        // ... else from the end that has never been used.
        if (count > m_capacity - m_end) { return kNone; }
        const uint32_t offset = m_end;
        m_end   += count;
        m_inUse += count;
        return offset;

    }   // MaterialParamRanges::Allocate()

    void MaterialParamRanges::Free(uint32_t offset, uint32_t count) {

        if (offset == kNone || count == 0) { return; }
        m_inUse -= count;

        // Into its place by offset, then joined to a freed run on either side.
        auto run = m_free.insert(std::lower_bound(m_free.begin(), m_free.end(), offset,
                                                  [](const Run& r, uint32_t value) { return r.offset < value; }),
                                 Run{ offset, count });
        if (const auto next = run + 1; next != m_free.end() && run->offset + run->count == next->offset) {
            run->count += next->count;
            run = m_free.erase(next) - 1;
        }
        if (run != m_free.begin()) {
            const auto previous = run - 1;
            if (previous->offset + previous->count == run->offset) {
                previous->count += run->count;
                run = m_free.erase(run) - 1;
            }
        }

        // A freed run that reaches the end gives the end back (it is always the last one).
        if (run->offset + run->count == m_end) {
            m_end = run->offset;
            m_free.erase(run);
        }

    }   // MaterialParamRanges::Free()

    // ------------------------------------------------------------------------------------------------------------- //
    //  BUFFER
    // ------------------------------------------------------------------------------------------------------------- //

    void MaterialParamBuffer::Init(VulkanContext& context, uint32_t framesInFlight) {

        m_values.assign(kCapacity, glm::vec4(0.0f));

        // Host-visible and persistently mapped, like the light buffers: written by the CPU once
        // per change, read by the GPU every frame.
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            m_buffers.emplace_back();
            m_buffers.back().Create(context, sizeof(glm::vec4) * kCapacity,
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_mapped.emplace_back(m_buffers.back().Map());
            std::memset(m_mapped.back(), 0, sizeof(glm::vec4) * kCapacity);
        }
        m_uploaded.assign(framesInFlight, 0);

    }   // MaterialParamBuffer::Init()

    void MaterialParamBuffer::Write(uint32_t offset, std::span<const glm::vec4> values) {

        if (values.empty()) { return; }
        if (offset >= kCapacity || values.size() > kCapacity - offset) {
            throw std::out_of_range("[Renderer] MaterialParamBuffer::Write past the end of the parameter buffer");
        }
        std::memcpy(m_values.data() + offset, values.data(), values.size_bytes());
        ++m_change;

    }   // MaterialParamBuffer::Write()

    void MaterialParamBuffer::Upload(uint32_t frameIndex) {

        if (frameIndex >= m_buffers.size() || m_uploaded[frameIndex] == m_change) { return; }

        // Everything in use, in one copy: it is small, and changes are rare (an edit in the
        // Inspector, a material loading). What lies past the end is never read.
        std::memcpy(m_mapped[frameIndex], m_values.data(), sizeof(glm::vec4) * m_ranges.End());
        m_uploaded[frameIndex] = m_change;

    }   // MaterialParamBuffer::Upload()

}   // NAMESPACE CRUCIBLE
