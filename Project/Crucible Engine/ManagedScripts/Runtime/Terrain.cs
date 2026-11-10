/*************************************************************************
* \file:         Terrain.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
* \brief         Script access to a Terrain: hand the engine a generated
*                height grid, and ask how high the ground is. Part of
*                Crucible.ScriptCore.dll.
*
* \details
*    The level generator builds the grid and gives it to the terrain:
*
*        var terrain = Entity.Find("Terrain").GetComponent<Terrain>();
*        terrain.Build(cols, rows, 4f, heights, regions);
*        Navigation.Rebake();     // enemies path on the new ground
*
*    heights: (cols + 1) * (rows + 1) corner heights, row-major (index =
*    z * (cols + 1) + x), corner (0, 0) at the grid's -X, -Z corner. The grid
*    is centred on the terrain entity. regions: cols * rows cells, one
*    TerrainRegion each, which sets the cell's colour.
*
*    The engine draws it (ECSTerrainSystem) and collides with it (the
*    entity's Collider, set to HeightField). Nothing is saved: build it
*    again every Play, from the same seed, and it is the same ground.
*
*    C++ side: Engine/src/Scripting/Bindings/TerrainBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>What a terrain cell is: sets its colour. Same values as TerrainRegion in Terrain/TerrainComponents.h.</summary>
    public enum TerrainRegion : byte
    {
        /// <summary>Walkable ground.</summary>
        Ground = 0,
        /// <summary>Too steep to walk: a wall.</summary>
        Cliff = 1,
        /// <summary>A road between chunks.</summary>
        Road = 2,
    }

    /// <summary>Generated uneven ground (Unity's Terrain): built from a height grid by a script, drawn and collided with by the engine.</summary>
    public sealed class Terrain
    {
        private readonly EntityIdentity entity;

        internal Terrain(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this terrain is on.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        /// <summary>
        /// Replaces the ground. Drawn and collided with from the next frame; call
        /// Navigation.Rebake() afterwards so enemies path on it.
        /// </summary>
        /// <param name="cols">Cells along X.</param>
        /// <param name="rows">Cells along Z.</param>
        /// <param name="cellSize">Metres per cell, both ways.</param>
        /// <param name="heights">(cols + 1) * (rows + 1) corner heights, row-major.</param>
        /// <param name="regions">cols * rows cells, one TerrainRegion each (as bytes).</param>
        /// <returns>False if the sizes disagree, a height is not a number, or a region is unknown; the old ground stays.</returns>
        public bool Build(int cols, int rows, float cellSize, float[] heights, byte[] regions)
        {
            if (heights == null || regions == null) return false;
            return BuildInternal(I, G, cols, rows, cellSize, heights, regions) != 0;
        }

        /// <summary>True once a script has built this terrain's ground this session.</summary>
        public bool IsBuilt
        {
            get { int v; return IsBuiltInternal(I, G, out v) != 0 && v != 0; }
        }

        /// <summary>
        /// The world height of the ground under world (x, z), on the exact triangle
        /// the player would stand on.
        /// </summary>
        /// <returns>False off the terrain or before it is built.</returns>
        public bool GroundHeight(float x, float z, out float height)
        {
            return GroundHeightInternal(I, G, x, z, out height) != 0;
        }

        public override string ToString() { return "Terrain on " + Entity; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int BuildInternal(int index, int generation, int cols, int rows, float cellSize, float[] heights, byte[] regions);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsBuiltInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GroundHeightInternal(int index, int generation, float x, float z, out float height);
    }
}
