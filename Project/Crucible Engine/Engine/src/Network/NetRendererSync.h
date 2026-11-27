/*************************************************************************
* \file:         NetRendererSync.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                A network object's renderers -
*                model, material, mesh, material slots, colour, visibility
*                and MaterialParams - the "Renderers" serializer and the
*                client's side.
*
* \details
*                Slots (NetRenderers on the object's root, added by the
*                network layer): 0 is the root, i is syncedChildren[i - 1]
*                for the first kSlots - 1 synced children. A slot is carried
*                when its entity resolves on the host and has a MeshRenderer.
*
*                On the wire: u8 mask of the slots carried, then for each a
*                u8 field mask and only the fields that are not their
*                defaults:
*                  visible        (a bit of the field mask)
*                  colour         4 x float (when not 0, 0, 0, 0)
*                  model          u64 asset id, then modelRoot (u32) when
*                                 it is not the whole model
*                  material, mesh u64 asset ids
*                  slot overrides u8 count (1 to kMaterialSlots, trailing
*                                 0s dropped), count x u64
*                  MaterialParams emissive scale, tint (4 x float), when
*                                 the host's entity has the component
*                at most 1 + 8 x 94 bytes. Non-finite numbers are never
*                sent (the defaults are); a client refuses them, and a mask
*                bit past the object's slots.
*
*                A client applies every carried slot right after the record
*                (ApplyRenderers): only fields that differ are written, so
*                an unchanged renderer is not stamped as changed (and an
*                AnimatedModel does not start over). A model change clears
*                the plain mesh and the slot overrides first, as the
*                Inspector and C# MeshRenderer.Model do; then the carried
*                overrides are applied. Asset ids are .meta GUIDs (or a
*                built-in mesh's id), the same on every machine with the
*                same project. A shader graph's parameters belong to its
*                material asset: changing material carries them.
*
*                Not carried: runtime-made meshes and materials (no asset
*                id), more than kMaterialSlots slot overrides (the first are
*                sent, the host warns once), renderers below the first
*                kSlots - 1 synced children (session start refuses an
*                object whose renderer would be skipped that way).
*
* \history
*    -v1.0       renderer sync: the Renderers serializer and the client's side
*
**************************************************************************/
#pragma once

#include "ECS/Core/ECSCore.h"
#include "EngineExport.h"

namespace Crucible
{
	class World;
	struct NetPrefabDesc;
	struct ReplicatedComponent;

	/// The Renderers serializer (ReplicationRegistry::RegisterBuiltIns registers it).
	ReplicatedComponent RenderersSerializer();

	/// The entity of renderer slot `slot` of `root` (see the file notes), or kNullEntity when
	/// there is none or its synced-child path does not resolve.
	ENGINE_API Entity RendererSlotEntity(const World& world, Entity root, const NetPrefabDesc& desc, unsigned slot);

	/// Client, after a record was applied to `root`: every carried slot's values into that
	/// entity's MeshRenderer and MaterialParams. Returns how many renderers changed.
	ENGINE_API unsigned ApplyRenderers(World& world, Entity root, const NetPrefabDesc& desc);
}
