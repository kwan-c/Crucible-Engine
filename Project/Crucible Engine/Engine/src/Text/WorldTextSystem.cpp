/*************************************************************************
  \file       WorldTextSystem.cpp
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    See WorldTextSystem.h.
*************************************************************************/
#include "Text/WorldTextSystem.h"

#include <algorithm>
#include <cmath>

#include "ECS/World.h"
#include "ECS/HierarchyTransformSystem.h"
#include "Graphics/RenderSystem.h"
#include "Resources/ResourceManager.h"

namespace Crucible {

    namespace {

        bool SameRun(const WorldUIRun& run, TextureHandle texture, UIDrawMode mode, float pixelRange, const UITextEffects& effects) {
            return run.texture == texture && run.mode == mode && run.pixelRange == pixelRange && run.effects == effects;
        }

    }   // namespace

    float BuildWorldTextGeometry(const FontAsset& font, const TextLayoutResult& layout, const WorldText& text,
                                 TextQuads& scratch, std::vector<WorldUIVertex>& vertices, std::vector<WorldUIRun>& runs) {

        vertices.clear();
        runs.clear();

        // pixelsPerUnit 0: bitmap bold steps by the font's baked size (nothing on screen to go by).
        BuildTextQuads(font, layout, TextStyleOf(text), 0.0f, scratch);
        if (scratch.quads.empty()) { return 0.0f; }

        const glm::vec2 size  = glm::max(text.size, glm::vec2(0.0f));
        const glm::vec2 pivot = text.pivot * size;              // box-local (+y down) point on the entity
        const bool      cut   = text.overflow != TextOverflow::Overflow;
        float reach = 0.0f;

        for (const TextQuad& q : scratch.quads) {
            glm::vec4 rect = q.rect;
            glm::vec4 uv   = q.uv;
            const bool solid = q.kind == TextQuadKind::Underline;

            // Truncate / Ellipsis: cut at the box's edges, the atlas rect cut by the same share.
            if (cut) {
                const glm::vec4 clipped(std::max(rect.x, 0.0f), std::max(rect.y, 0.0f), std::min(rect.z, size.x), std::min(rect.w, size.y));
                if (clipped.z <= clipped.x || clipped.w <= clipped.y) { continue; }
                if (!solid && rect.z > rect.x && rect.w > rect.y) {
                    const glm::vec2 du((uv.z - uv.x) / (rect.z - rect.x), (uv.w - uv.y) / (rect.w - rect.y));
                    uv = glm::vec4(uv.x + (clipped.x - rect.x) * du.x, uv.y + (clipped.y - rect.y) * du.y,
                                   uv.z - (rect.z - clipped.z) * du.x, uv.w - (rect.w - clipped.w) * du.y);
                }
                rect = clipped;
            }
            if (rect.z <= rect.x || rect.w <= rect.y) { continue; }

            // Box-local (+y down, origin top-left) -> the text's plane (+y up, origin at the pivot).
            const float x0 = rect.x - pivot.x, x1 = rect.z - pivot.x;
            const float yTop = pivot.y - rect.y, yBottom = pivot.y - rect.w;
            const uint32_t colour = PackUIColour(q.colour);

            const TextureHandle texture    = solid ? TextureHandle{} : font.texture;
            const UIDrawMode    mode       = solid ? UIDrawMode::Image : font.drawMode;
            const float         pixelRange = (!solid && scratch.field) ? font.data.m_atlas.pixelRange : 0.0f;
            const UITextEffects effects    = (!solid && scratch.field) ? q.effects : UITextEffects{};

            const uint32_t first = static_cast<uint32_t>(vertices.size());
            if (solid) { uv = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f); }
            // Counter-clockwise seen from the front (+z of the plane): bottom-left, bottom-right, top-right; bottom-left, top-right, top-left.
            const WorldUIVertex bl{ { x0, yBottom }, { uv.x, uv.w }, colour };
            const WorldUIVertex br{ { x1, yBottom }, { uv.z, uv.w }, colour };
            const WorldUIVertex tr{ { x1, yTop },    { uv.z, uv.y }, colour };
            const WorldUIVertex tl{ { x0, yTop },    { uv.x, uv.y }, colour };
            vertices.insert(vertices.end(), { bl, br, tr, bl, tr, tl });

            if (!runs.empty() && SameRun(runs.back(), texture, mode, pixelRange, effects) && runs.back().firstVertex + runs.back().vertexCount == first) {
                runs.back().vertexCount += 6;
            } else {
                runs.push_back(WorldUIRun{ .texture = texture, .mode = mode, .pixelRange = pixelRange, .effects = effects,
                                             .firstVertex = first, .vertexCount = 6 });
            }

            for (const WorldUIVertex& v : { bl, br, tr, tl }) { reach = std::max(reach, glm::length(v.local)); }
        }
        return reach;

    }   // BuildWorldTextGeometry()

    void WorldTextSystem::Describe(SystemDesc& desc) const {

        desc.Uses<TextQuery>()                        // reads WorldTransform + WorldText
            .Reads<SceneHidden>()                     // editor scene visibility: the panel is flagged
            .RunAfter<HierarchyTransformSystem>()     // WorldTransform must already be this frame's
            .Affinity(SystemAffinity::MainThread);    // touches the Renderer and ResourceManager

    }   // WorldTextSystem::Describe()

    void WorldTextSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {

        if (m_resources == nullptr) { return; }
        if (!m_query) { m_query.emplace(world.CreateQuery<const WorldTransform, const WorldText>()); }

        m_cache.Tick();

        m_query->ForEach([&](Entity entity, const WorldTransform& worldTransform, const WorldText& text) {

            if (!worldTransform.valid || text.text.empty() || text.colour.a <= 0.0f) { return; }

            const glm::mat4& m = worldTransform.matrix;
            const glm::vec3 right(m[0]), up(m[1]);
            const float scale = std::max(glm::length(right), glm::length(up));
            if (!(scale > 0.0f)) { return; }   // scaled to nothing

            const FontAsset* font = text.fontAsset != 0 ? m_resources->GetFont(text.fontAsset) : nullptr;
            if (font == nullptr) { font = m_resources->GetDefaultFont(); }   // none picked, or it can't be loaded
            if (font == nullptr || !font->texture.IsValid()) { return; }

            // Bitmap fonts: the largest baked size (closest up is the common case in the world).
            TextLayoutSettings settings = TextLayoutSettingsOf(text, glm::max(text.size, glm::vec2(0.0f)));
            const bool bitmap = font->data.m_outputType == FontOutputType::Bitmap;
            settings.sizeGroup = bitmap && !font->data.m_groups.empty() ? font->data.m_groups.size() - 1 : 0;

            const TextLayoutResult& layout = m_cache.Get(TextLayoutCache::KeyFor(entity.index, entity.generation, settings.sizeGroup),
                                                         entity.index, *font, text.text, settings);
            const float reach = BuildWorldTextGeometry(*font, layout, text, m_quads, m_vertices, m_runs);
            if (m_runs.empty()) { return; }

            WorldUIDesc desc;
            desc.anchor      = glm::vec3(m[3]);
            desc.right       = right;
            desc.up          = up;
            desc.radius      = reach * scale;
            desc.facing      = text.billboard == WorldText::Billboard::FaceCamera ? WorldUIFacing::FaceCamera
                             : text.billboard == WorldText::Billboard::Upright    ? WorldUIFacing::Upright
                             :                                                      WorldUIFacing::Transform;
            desc.alwaysOnTop = text.alwaysOnTop;
            desc.doubleSided = text.doubleSided;
            desc.intensity   = std::max(text.intensity, 0.0f);
            if (world.HasComponent<SceneHidden>(entity)) { desc.views = WorldUIViews::NotInSceneViews; }   // hidden in the editor's Scene view only
            if (text.fade) {
                desc.fadeStart = std::max(text.fadeStart, 0.0f);
                desc.fadeEnd   = std::max(text.fadeEnd, desc.fadeStart + 0.001f);
            }
            m_renderer.SubmitWorldUI(desc, m_vertices, m_runs);
        });

    }   // WorldTextSystem::Update()

}   // NAMESPACE CRUCIBLE
