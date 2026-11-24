/*************************************************************************
* \file:         TerrainBindings.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
* \brief         Internal calls behind the C# Terrain class
*                (ManagedScripts/Runtime/Terrain.cs).
*
* \details
*    Build hands a script's generated grid to the entity's Terrain
*    component (Terrain::Build); ECSTerrainSystem then draws it and
*    ECSPhysicsSystem collides with it. It is the first binding to take
*    arrays from C#: they arrive as MonoArray, and their lengths are
*    checked against the grid size before anything is read.
*
*    GroundHeight answers on the exact triangle under a point
*    (HeightGrid::SampleHeight), in world space through the entity's
*    WorldTransform. That handles moving, scaling and turning the terrain
*    about Y; tilting it (pitch or roll) is not supported.
*
*    Same shape as NavigationBindings.cpp: 1 = done, 0 = refused (dead
*    entity, no such component, a bad value); never throws into C#.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/Components/Components.h"   // WorldTransform, Terrain
#include "ECS/World.h"
#include "Terrain/TerrainComponents.h"

#if CRUCIBLE_WITH_MONO

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <mono/metadata/loader.h>
#include <mono/metadata/object.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;   // ScriptVec3

		// Past this the grid is a mistake rather than a level: 4096 x 4096
		// cells is 16 km at the usual 4 m, and ~400 MB of mesh.
		constexpr std::uint32_t kMaxCellsPerSide = 4096;

		std::size_t LengthOf(MonoArray* array)
		{
			return array ? static_cast<std::size_t>(mono_array_length(array)) : 0;
		}

		/*********************************************************************
		 \brief  C# Terrain.Build: replaces the entity's ground.
		 \param  heights (cols + 1) * (rows + 1) corner heights, row-major.
		 \param  regions cols * rows cells, one TerrainRegion each.
		 \return 1 when built; 0 for a dead entity, no Terrain, sizes that
		         disagree with cols and rows, or NaN heights (the old
		         ground is kept).
		 *********************************************************************/
		int32_t ManagedTerrainBuild(int32_t index, int32_t generation, int32_t cols, int32_t rows, float cellSize,
		                            MonoArray* heights, MonoArray* regions) noexcept
		{
			try
			{
				if (cols <= 0 || rows <= 0 || static_cast<std::uint32_t>(cols) > kMaxCellsPerSide ||
				    static_cast<std::uint32_t>(rows) > kMaxCellsPerSide || !(cellSize > 0.0f) || !std::isfinite(cellSize))
					return 0;

				const std::size_t corners = static_cast<std::size_t>(cols + 1) * static_cast<std::size_t>(rows + 1);
				const std::size_t cells   = static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows);
				if (LengthOf(heights) != corners || LengthOf(regions) != cells)
					return 0;

				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				Terrain* terrain = EntityContext::GetWorld()->Write<Terrain>(entity);
				if (!terrain)
					return 0;

				TerrainGrid grid;
				grid.heights.cols     = static_cast<std::uint32_t>(cols);
				grid.heights.rows     = static_cast<std::uint32_t>(rows);
				grid.heights.cellSize = cellSize;

				const float* heightData = mono_array_addr(heights, float, 0);
				grid.heights.heights.assign(heightData, heightData + corners);

				const std::uint8_t* regionData = mono_array_addr(regions, std::uint8_t, 0);
				grid.regions.assign(regionData, regionData + cells);

				return terrain->Build(std::move(grid)) ? 1 : 0;   // Build rejects NaN heights and unknown regions
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Terrain.IsBuilt: has a script given this terrain ground?
		 *********************************************************************/
		int32_t ManagedTerrainIsBuilt(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				if (!out)
					return 0;
				*out = 0;
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const Terrain* terrain = EntityContext::GetWorld()->Read<Terrain>(entity);
				if (!terrain)
					return 0;
				*out = terrain->m_grid != nullptr ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Terrain.GroundHeight: the world height of the ground
		         under world (x, z).
		 \return 1 with *outHeight; 0 off the grid, before Build, before the
		         entity's WorldTransform exists, or for a dead entity.
		 *********************************************************************/
		int32_t ManagedTerrainGroundHeight(int32_t index, int32_t generation, float x, float z, float* outHeight) noexcept
		{
			try
			{
				if (!outHeight || !std::isfinite(x) || !std::isfinite(z))
					return 0;
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const World& world = *EntityContext::GetWorld();
				const Terrain*        terrain   = world.Read<Terrain>(entity);
				const WorldTransform* transform = world.Read<WorldTransform>(entity);
				if (!terrain || !terrain->m_grid || !transform || !transform->valid)
					return 0;

				// Into the grid's own space, sample there, and back out. A world
				// vertical stays vertical as long as the terrain only turns about Y.
				const Math::vec4 local = glm::inverse(transform->matrix) * Math::vec4(x, 0.0f, z, 1.0f);
				float height = 0.0f;
				if (!terrain->m_grid->heights.SampleHeight(local.x, local.z, height))
					return 0;
				const Math::vec4 ground = transform->matrix * Math::vec4(local.x, height, local.z, 1.0f);
				*outHeight = ground.y;
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

	void RegisterTerrainBindings()
	{
		Add("Crucible.Terrain::BuildInternal", &ManagedTerrainBuild);
		Add("Crucible.Terrain::IsBuiltInternal", &ManagedTerrainIsBuilt);
		Add("Crucible.Terrain::GroundHeightInternal", &ManagedTerrainGroundHeight);
	}
}

#endif
