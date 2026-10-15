/*************************************************************************
  \file       GpuProfiler.cpp
  \brief
    See GpuProfiler.h.
*************************************************************************/
#include "Graphics/GpuProfiler.h"

namespace Crucible {

    void GpuProfiler::Init(VulkanContext& context, uint32_t framesInFlight) {

        m_context = &context;

        // Timestamps are optional: without them the GPU half of RenderFrameStats stays empty.
        const vk::PhysicalDeviceLimits limits = context.GetPhysicalDevice().getProperties().limits;
        if (!limits.timestampComputeAndGraphics || limits.timestampPeriod <= 0.0f) { return; }

        const uint32_t frames = framesInFlight;
        vk::QueryPoolCreateInfo info{ .queryType = vk::QueryType::eTimestamp, .queryCount = kMaxGpuMarks * frames };
        m_timestampPool     = vk::raii::QueryPool(context.GetDevice(), info);
        m_timestampPeriodNs = limits.timestampPeriod;
        m_gpuMarks.resize(frames);

    }   // GpuProfiler::Init()

    void GpuProfiler::BeginFrame(vk::CommandBuffer cmd, uint32_t frameIndex) {

        m_markFrame = frameIndex;
        if (*m_timestampPool) {
            m_gpuMarks[frameIndex].clear();
            cmd.resetQueryPool(*m_timestampPool, frameIndex * kMaxGpuMarks, kMaxGpuMarks);
        }

    }   // GpuProfiler::BeginFrame()

    void GpuProfiler::Mark(vk::CommandBuffer cmd, GpuSection section) {

        if (!*m_timestampPool) { return; }
        std::vector<GpuSection>& marks = m_gpuMarks[m_markFrame];
        if (marks.size() >= kMaxGpuMarks) { return; }

        // "Bottom of pipe": the time at which everything recorded before this point has finished.
        cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *m_timestampPool,
                           m_markFrame * kMaxGpuMarks + static_cast<uint32_t>(marks.size()));
        marks.push_back(section);

    }   // GpuProfiler::Mark()

    void GpuProfiler::Read(uint32_t frameIndex, RenderFrameStats& stats) const {

        if (!*m_timestampPool) { return; }
        const std::vector<GpuSection>& marks = m_gpuMarks[frameIndex];
        if (marks.size() < 2) { return; }

        // Two numbers per timestamp: the time, and whether it has been written. A frame that was
        // recorded but never submitted leaves them unwritten; skip it rather than wait.
        const uint32_t count = static_cast<uint32_t>(marks.size());
        std::vector<uint64_t> data(static_cast<size_t>(count) * 2);
        const vk::Result result = (*m_context->GetDevice()).getQueryPoolResults(
            *m_timestampPool, frameIndex * kMaxGpuMarks, count,
            data.size() * sizeof(uint64_t), data.data(), 2 * sizeof(uint64_t),
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
        if (result != vk::Result::eSuccess) { return; }
        for (uint32_t i = 0; i < count; ++i) { if (data[static_cast<size_t>(i) * 2 + 1] == 0) { return; } }

        // The time between two marks belongs to the section the earlier one started.
        float sections[static_cast<size_t>(GpuSection::Count)]{};
        float total = 0.0f;
        for (uint32_t i = 0; i + 1 < count; ++i) {
            const uint64_t a = data[static_cast<size_t>(i) * 2], b = data[static_cast<size_t>(i + 1) * 2];
            if (b <= a) { continue; }
            const float ms = static_cast<float>(static_cast<double>(b - a) * static_cast<double>(m_timestampPeriodNs) * 1e-6);
            sections[static_cast<size_t>(marks[i])] += ms;
            total += ms;
        }
        for (size_t i = 0; i < static_cast<size_t>(GpuSection::Count); ++i) {
            stats.gpuMs[i] += (sections[i] - stats.gpuMs[i]) * 0.1f;
        }
        stats.gpuTotalMs += (total - stats.gpuTotalMs) * 0.1f;
        stats.gpuValid = true;

    }   // GpuProfiler::Read()

}   // NAMESPACE CRUCIBLE
