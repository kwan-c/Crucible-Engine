/*************************************************************************
* \file:         MeshRendererBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief         Internal calls behind the C# MeshRenderer and
*                MaterialParams classes and the built-in Mesh class
*                (ManagedScripts/Runtime/MeshRenderer.cs, Mesh.cs).
*                Material / Model names are in UIBindings.cpp.
*
* \details
*    Bindings only: MeshRenderer and MaterialParams (Components.h) are
*    unchanged. RenderExtractSystem resolves MeshRenderer's asset ids
*    (modelAsset, materialAsset, materialOverrides) and reads MaterialParams
*    every frame, so a change made in OnUpdate is drawn that same frame.
*    AnimatedModelSystem notices a new model and starts its animator over.
*
*    Asset ids cross as two int halves (like AudioClip). Setting a material
*    clears the resolved handle too, so the id (or, with none, the default /
*    the model's imported materials) is what draws - the same as the
*    Inspector's pickers. Setting a model clears the plain mesh, the model
*    part root and the slot overrides, as the Inspector does.
*
*    Material slot counts come from the model's definition (ResourceManager,
*    from SetResources); the model is loaded if it is not yet.
*
*    Built-in meshes (C# Mesh.Cube ...) are not asset files: their ids are
*    ResourceManager::BuiltinId(name), checked against the ResourceManager,
*    and set with its AssignMesh (which also clears the model).
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Resources/ResourceManager.h"
#include "Resources/ModelResource.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <string>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		thread_local ResourceManager* resources = nullptr;

		// Highest slot a script may override when the model can't be read (keeps a typo from
		// growing the vector without bound).
		constexpr int32_t kMaxSlots = 256;

		uint64_t JoinId(int32_t low, int32_t high)
		{
			return (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | static_cast<uint32_t>(low);
		}

		void SplitId(uint64_t id, int32_t* low, int32_t* high)
		{
			*low = static_cast<int32_t>(static_cast<uint32_t>(id));
			*high = static_cast<int32_t>(static_cast<uint32_t>(id >> 32));
		}

		template <typename C>
		const C* ReadComponent(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Read<C>(entity);
		}

		template <typename C>
		C* WriteComponent(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Write<C>(entity);
		}

		/*********************************************************************
		 \brief  How many material slots `renderer`'s model has, or -1 if it
		         has no model or the model can't be read.
		 *********************************************************************/
		int32_t SlotCount(const MeshRenderer& renderer)
		{
			if (!resources || renderer.modelAsset == 0)
				return -1;
			const auto model = resources->GetModel(renderer.modelAsset);
			return model ? static_cast<int32_t>(model->materials.size()) : -1;
		}

		// ======================================================================
		// MeshRenderer: visible
		// ======================================================================

		int32_t ManagedMeshRendererGetVisible(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const MeshRenderer* renderer = out ? ReadComponent<MeshRenderer>(index, generation) : nullptr;
				if (!renderer)
					return 0;
				*out = renderer->visible ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedMeshRendererSetVisible(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				MeshRenderer* renderer = WriteComponent<MeshRenderer>(index, generation);
				if (!renderer)
					return 0;
				renderer->visible = value != 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// MeshRenderer: material (all slots) and model
		// ======================================================================

		int32_t ManagedMeshRendererGetMaterial(int32_t index, int32_t generation, int32_t* low, int32_t* high) noexcept
		{
			try
			{
				const MeshRenderer* renderer = (low && high) ? ReadComponent<MeshRenderer>(index, generation) : nullptr;
				if (!renderer)
					return 0;
				SplitId(renderer->materialAsset, low, high);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# MeshRenderer.Material (set). 0 = none (the default
		         material, or a model's own). Anything else must be a .mat.
		 *********************************************************************/
		int32_t ManagedMeshRendererSetMaterial(int32_t index, int32_t generation, int32_t low, int32_t high) noexcept
		{
			try
			{
				const uint64_t id = JoinId(low, high);
				if (id != 0 && !EntityContext::IsMaterialAsset(id))
					return 0;
				const MeshRenderer* current = ReadComponent<MeshRenderer>(index, generation);
				if (!current)
					return 0;
				if (current->materialAsset == id)
					return 1;
				MeshRenderer* renderer = WriteComponent<MeshRenderer>(index, generation);
				if (!renderer)
					return 0;
				renderer->materialAsset = id;
				renderer->material = {};   // the id (or the default) draws, never a stale handle
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedMeshRendererGetModel(int32_t index, int32_t generation, int32_t* low, int32_t* high) noexcept
		{
			try
			{
				const MeshRenderer* renderer = (low && high) ? ReadComponent<MeshRenderer>(index, generation) : nullptr;
				if (!renderer)
					return 0;
				SplitId(renderer->modelAsset, low, high);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# MeshRenderer.Model (set). 0 = none (draws nothing unless
		         a plain mesh is set). Like the Inspector's Model picker: the
		         plain mesh, part root and slot overrides are cleared.
		 *********************************************************************/
		int32_t ManagedMeshRendererSetModel(int32_t index, int32_t generation, int32_t low, int32_t high) noexcept
		{
			try
			{
				const uint64_t id = JoinId(low, high);
				if (id != 0 && !EntityContext::IsModelAsset(id))
					return 0;
				const MeshRenderer* current = ReadComponent<MeshRenderer>(index, generation);
				if (!current)
					return 0;
				if (current->modelAsset == id)
					return 1;
				MeshRenderer* renderer = WriteComponent<MeshRenderer>(index, generation);
				if (!renderer)
					return 0;
				renderer->modelAsset = id;
				renderer->modelRoot = InvalidModelIndex;
				renderer->materialOverrides.clear();
				if (id != 0)
				{
					renderer->meshAsset = 0;
					renderer->mesh = {};
				}
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// MeshRenderer: material slots (models only)
		// ======================================================================

		int32_t ManagedMeshRendererGetMaterialCount(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const MeshRenderer* renderer = out ? ReadComponent<MeshRenderer>(index, generation) : nullptr;
				if (!renderer)
					return 0;
				const int32_t count = SlotCount(*renderer);
				*out = count > 0 ? count : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedMeshRendererGetSlotMaterial(int32_t index, int32_t generation, int32_t slot, int32_t* low,
		                                           int32_t* high) noexcept
		{
			try
			{
				const MeshRenderer* renderer = (low && high) ? ReadComponent<MeshRenderer>(index, generation) : nullptr;
				if (!renderer || slot < 0)
					return 0;
				const uint64_t id = static_cast<std::size_t>(slot) < renderer->materialOverrides.size()
				                        ? renderer->materialOverrides[static_cast<std::size_t>(slot)]
				                        : 0;
				SplitId(id, low, high);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# MeshRenderer.SetMaterial(slot, material): overrides one of
		         the model's material slots (0 = back to the model's own /
		         Material). The slot must exist on the model.
		 *********************************************************************/
		int32_t ManagedMeshRendererSetSlotMaterial(int32_t index, int32_t generation, int32_t slot, int32_t low,
		                                           int32_t high) noexcept
		{
			try
			{
				const uint64_t id = JoinId(low, high);
				if (slot < 0 || slot >= kMaxSlots || (id != 0 && !EntityContext::IsMaterialAsset(id)))
					return 0;
				const MeshRenderer* current = ReadComponent<MeshRenderer>(index, generation);
				if (!current || current->modelAsset == 0)
					return 0;
				const int32_t count = SlotCount(*current);
				if (count >= 0 && slot >= count)
					return 0;
				const std::size_t s = static_cast<std::size_t>(slot);
				const uint64_t existing = s < current->materialOverrides.size() ? current->materialOverrides[s] : 0;
				if (existing == id)
					return 1;
				MeshRenderer* renderer = WriteComponent<MeshRenderer>(index, generation);
				if (!renderer)
					return 0;
				if (renderer->materialOverrides.size() <= s)
					renderer->materialOverrides.resize(s + 1, 0);
				renderer->materialOverrides[s] = id;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Built-in meshes (Mesh.Cube ...): ids are ResourceManager::BuiltinId(name),
		// fixed hashes of the primitive's name, not asset files.
		// ======================================================================

		/*********************************************************************
		 \brief  true if `id` is one of the renderer's built-in primitive
		         meshes ("Default Material" is built in too, but no mesh).
		 *********************************************************************/
		bool IsBuiltinMesh(uint64_t id)
		{
			if (!resources || id == 0)
				return false;
			const std::string name = resources->GetName(id);
			return ResourceManager::BuiltinId(name.c_str()) == id && resources->GetMesh(id).IsValid();
		}

		/*********************************************************************
		 \brief  C# Mesh.Cube etc.: the id of the built-in mesh `name`, or 0 if
		         there is none by that name.
		 *********************************************************************/
		int32_t ManagedMeshGetBuiltin(MonoString* name, int32_t* low, int32_t* high) noexcept
		{
			try
			{
				if (!low || !high)
					return 0;
				GcRoot root(reinterpret_cast<MonoObject*>(name));
				std::string text;
				if (!root.Get() || !FromManagedString(reinterpret_cast<MonoString*>(root.Get()), text))
					return 0;
				const uint64_t id = ResourceManager::BuiltinId(text.c_str());
				if (!IsBuiltinMesh(id))
					return 0;
				SplitId(id, low, high);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Mesh.Name: "Cube", ... (or a model file's path, for a
		         MeshRenderer whose mesh came from one in the Inspector).
		 *********************************************************************/
		MonoString* ManagedMeshGetName(int32_t low, int32_t high) noexcept
		{
			try
			{
				const uint64_t id = JoinId(low, high);
				if (!resources || id == 0)
					return nullptr;
				std::wstring wide;
				if (!ToUtf16(resources->GetName(id), wide))
					return nullptr;
				return mono_string_new_utf16(mono_domain_get(), reinterpret_cast<const mono_unichar2*>(wide.data()),
				                             static_cast<int32_t>(wide.size()));
			}
			catch (...)
			{
				return nullptr;
			}
		}

		int32_t ManagedMeshRendererGetMesh(int32_t index, int32_t generation, int32_t* low, int32_t* high) noexcept
		{
			try
			{
				const MeshRenderer* renderer = (low && high) ? ReadComponent<MeshRenderer>(index, generation) : nullptr;
				if (!renderer)
					return 0;
				SplitId(renderer->meshAsset, low, high);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# MeshRenderer.Mesh (set): a built-in mesh, or 0 for none.
		         ResourceManager::AssignMesh, as the Inspector's Mesh picker:
		         sets the id and its handle, and clears the model (a model
		         always wins over the mesh) and its slot overrides.
		 *********************************************************************/
		int32_t ManagedMeshRendererSetMesh(int32_t index, int32_t generation, int32_t low, int32_t high) noexcept
		{
			try
			{
				const uint64_t id = JoinId(low, high);
				if (!resources || (id != 0 && !IsBuiltinMesh(id)))
					return 0;
				const MeshRenderer* current = ReadComponent<MeshRenderer>(index, generation);
				if (!current)
					return 0;
				if (current->meshAsset == id && current->modelAsset == 0)
					return 1;
				MeshRenderer* renderer = WriteComponent<MeshRenderer>(index, generation);
				if (!renderer)
					return 0;
				resources->AssignMesh(*renderer, id);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// MaterialParams
		// ======================================================================

		int32_t ManagedMaterialParamsGetEmissiveScale(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				const MaterialParams* params = out ? ReadComponent<MaterialParams>(index, generation) : nullptr;
				if (!params)
					return 0;
				*out = params->emissiveScale;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedMaterialParamsSetEmissiveScale(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value) || *value < 0.0f)
					return 0;
				MaterialParams* params = WriteComponent<MaterialParams>(index, generation);
				if (!params)
					return 0;
				params->emissiveScale = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedMaterialParamsGetCustom(int32_t index, int32_t generation, ScriptVec3* out) noexcept
		{
			try
			{
				const MaterialParams* params = out ? ReadComponent<MaterialParams>(index, generation) : nullptr;
				if (!params)
					return 0;
				*out = {params->custom.x, params->custom.y, params->custom.z};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedMaterialParamsSetCustom(int32_t index, int32_t generation, const ScriptVec3* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(value->x) || !std::isfinite(value->y) || !std::isfinite(value->z))
					return 0;
				MaterialParams* params = WriteComponent<MaterialParams>(index, generation);
				if (!params)
					return 0;
				params->custom = {value->x, value->y, value->z};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename Fn>
		void Add(const char* name, Fn* function)
		{
			mono_add_internal_call(name, reinterpret_cast<const void*>(function));
		}
	}

	void SetResources(ResourceManager* manager)
	{
		resources = manager;
	}

	void RegisterMeshRendererBindings()
	{
		Add("Crucible.MeshRenderer::GetVisibleInternal", &ManagedMeshRendererGetVisible);
		Add("Crucible.MeshRenderer::SetVisibleInternal", &ManagedMeshRendererSetVisible);
		Add("Crucible.MeshRenderer::GetMaterialInternal", &ManagedMeshRendererGetMaterial);
		Add("Crucible.MeshRenderer::SetMaterialInternal", &ManagedMeshRendererSetMaterial);
		Add("Crucible.MeshRenderer::GetModelInternal", &ManagedMeshRendererGetModel);
		Add("Crucible.MeshRenderer::SetModelInternal", &ManagedMeshRendererSetModel);
		Add("Crucible.MeshRenderer::GetMaterialCountInternal", &ManagedMeshRendererGetMaterialCount);
		Add("Crucible.MeshRenderer::GetSlotMaterialInternal", &ManagedMeshRendererGetSlotMaterial);
		Add("Crucible.MeshRenderer::SetSlotMaterialInternal", &ManagedMeshRendererSetSlotMaterial);
		Add("Crucible.MeshRenderer::GetMeshInternal", &ManagedMeshRendererGetMesh);
		Add("Crucible.MeshRenderer::SetMeshInternal", &ManagedMeshRendererSetMesh);

		Add("Crucible.Mesh::GetBuiltinInternal", &ManagedMeshGetBuiltin);
		Add("Crucible.Mesh::GetNameInternal", &ManagedMeshGetName);

		Add("Crucible.MaterialParams::GetEmissiveScaleInternal", &ManagedMaterialParamsGetEmissiveScale);
		Add("Crucible.MaterialParams::SetEmissiveScaleInternal", &ManagedMaterialParamsSetEmissiveScale);
		Add("Crucible.MaterialParams::GetCustomInternal", &ManagedMaterialParamsGetCustom);
		Add("Crucible.MaterialParams::SetCustomInternal", &ManagedMaterialParamsSetCustom);
	}
}
#endif
