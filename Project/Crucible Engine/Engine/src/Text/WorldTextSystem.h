/*************************************************************************
  \file       WorldTextSystem.h
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    WorldTextSystem: lays out every WorldText (Text/WorldText.h), builds its
    glyph quads and submits them to the Renderer, which draws them inside
    the 3D scene (WorldTextPass).
  \details
    Runs in PreRender after HierarchyTransformSystem, in Edit mode too, so
    text shows in the Scene view while you place it. Layout is cached
    (TextLayoutCache): a text is only laid out again when its text, a
    layout setting or its font changes - moving it costs nothing extra.

    The quads come from Text/TextBuilder.h, the same as UIText's, so text,
    rich text, bold, underline and the effects look the same as on a canvas.
    Overflow other than Overflow cuts glyphs exactly at the box's edges
    (a glyph half outside is drawn half).
*************************************************************************/
#pragma once

#include "ECS/Systems/IECSSystems.h"
#include "ECS/Core/ECSCore.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/Query.h"
#include "Graphics/WorldTextTypes.h"
#include "Text/TextBuilder.h"
#include "EngineExport.h"

#include <optional>
#include <vector>

namespace Crucible {

    class Renderer;
    class ResourceManager;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members in an exported class; Engine.dll and its clients share one toolchain
#endif

    /*************************************************************************
     \brief  Turns one laid-out WorldText into the vertices and runs the
             Renderer takes (WorldTextTypes.h): six corners per quad in the
             text's plane (world units from the entity's position, +y up),
             the box placed by text.pivot, cut to the box unless overflow
             is Overflow.
     \param[in] font     The font `layout` was made with.
     \param[in] scratch  Reused for the quads (TextBuilder.h).
     \param[out] vertices, runs  Cleared first.
     \return How far the farthest corner is from the entity's position, in
             the plane's units (before the Transform's scale). 0 = nothing.
     *************************************************************************/
    ENGINE_API float BuildWorldTextGeometry(const FontAsset& font, const TextLayoutResult& layout, const WorldText& text,
                                            TextQuads& scratch, std::vector<WorldTextVertex>& vertices, std::vector<WorldTextRun>& runs);

    class ENGINE_API WorldTextSystem final : public IECSSystem {
    public:
        /*********************************************************************
         \param[in] renderer   The Core-owned Renderer (receives the text).
         \param[in] resources  The Core-owned ResourceManager: fonts. Null =
                               nothing is drawn.
         *********************************************************************/
        WorldTextSystem(Renderer& renderer, ResourceManager* resources) : m_renderer(renderer), m_resources(resources) {}

        SystemStage Stage() const override { return SystemStage::PreRender; }

        /// Reads WorldTransform + WorldText; after HierarchyTransformSystem; main thread (calls the Renderer).
        void Describe(SystemDesc& desc) const override;

        /// Lays out (when needed), builds and submits every WorldText.
        void Update(World& world, SystemContext& ctx) override;

    private:
        using TextQuery = Query<const WorldTransform, const WorldText>;

        Renderer&                    m_renderer;
        ResourceManager*             m_resources = nullptr;
        std::optional<TextQuery>     m_query;
        TextLayoutCache              m_cache{ "World Text" };

        // Reused every frame, so steady-state frames don't allocate.
        TextQuads                    m_quads;
        std::vector<WorldTextVertex> m_vertices;
        std::vector<WorldTextRun>    m_runs;
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
