/*************************************************************************
* \file:         Terrain.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.2
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
* \history
*    -v1.0       first version
*    -v1.1       TerrainRegion.Tunnel
*    -v1.2       TerrainVolume: Build with a cave volume
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
        /// <summary>A cave tunnel's floor: walkable (only Cliff is a wall).</summary>
        Tunnel = 3,
    }

    /// <summary>
    /// Ground a height grid can't hold - caves, overhangs - as a box of rock and air: a density per
    /// sample, above 0 rock, below 0 air (the surface is where it crosses 0). Sample (x, y, z) sits at
    /// Origin + (x, y, z) * VoxelSize in the terrain's own space; Density is x fastest, then z, then y
    /// (Index). It replaces the height grid's cells under it, so to meet them without a crack: Origin's
    /// x and z sit on a grid corner, VoxelSize divides the cell size, the box ends on a corner, and the
    /// samples along its sides are the grid's own ground there (height - y).
    /// </summary>
    public sealed class TerrainVolume
    {
        public Vector3 Origin;
        public float VoxelSize = 1f;
        public int CountX, CountY, CountZ;      // samples per axis, at least 2 each
        public float[] Density;                 // CountX * CountY * CountZ

        public TerrainVolume(Vector3 origin, float voxelSize, int countX, int countY, int countZ)
        {
            Origin = origin; VoxelSize = voxelSize; CountX = countX; CountY = countY; CountZ = countZ;
            Density = new float[countX * countY * countZ];
        }

        public int Index(int x, int y, int z) { return (y * CountZ + z) * CountX + x; }
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
            return Build(cols, rows, cellSize, heights, regions, null);
        }

        /// <summary>
        /// Replaces the ground, with a cave volume in place of the grid cells under it (see
        /// TerrainVolume). Drawn with the ground: ground colour facing up, Tunnel colour on cave floors,
        /// cliff colour on anything steeper. Null for no volume.
        /// </summary>
        /// <returns>False if the sizes disagree, a value is not a number, a region is unknown or the
        /// volume doesn't line up with the grid; the old ground stays.</returns>
        public bool Build(int cols, int rows, float cellSize, float[] heights, byte[] regions, TerrainVolume volume)
        {
            if (heights == null || regions == null) return false;
            if (volume == null)
                return BuildInternal(I, G, cols, rows, cellSize, heights, regions, 0f, 0f, 0f, 1f, 0, 0, 0, null) != 0;
            return BuildInternal(I, G, cols, rows, cellSize, heights, regions, volume.Origin.X, volume.Origin.Y, volume.Origin.Z,
                                 volume.VoxelSize, volume.CountX, volume.CountY, volume.CountZ, volume.Density) != 0;
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
        private static extern int BuildInternal(int index, int generation, int cols, int rows, float cellSize, float[] heights, byte[] regions,
                                                 float originX, float originY, float originZ, float voxelSize,
                                                 int countX, int countY, int countZ, float[] density);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsBuiltInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GroundHeightInternal(int index, int generation, float x, float z, out float height);
    }
}
