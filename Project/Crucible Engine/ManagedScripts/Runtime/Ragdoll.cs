/*************************************************************************
* \file:         Ragdoll.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          23 November 2026
* \version       1.2
* \brief         C# access to the engine's ragdolls (ECSRagdollSystem).
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Ragdoll.Spawn(model, velocity, owner) goes to C++ (ManagedRagdollSpawn
*    in Bindings/RagdollBindings.cpp). The engine then builds the ragdoll
*    from the model's bones: one physics piece per rigid mesh, jointed, and
*    the model's mesh follows the pieces from then on. Port of Unity's
*    CombatRagdoll.Spawn.
*
*    The engine also removes it, with its owner: once every piece has been
*    at rest for 5 s, after 30 s at most, or when a newer ragdoll needs room
*    (at most 20; the oldest goes, one at rest first).
*
*    v1.1  owner, Count
*    v1.2  Spawn checks the model at once (false = it cannot be built)
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>Ragdolls built by the engine from an animated model's bones.</summary>
    public static class Ragdoll
    {
        /// <summary>
        /// Turns this entity's model into a ragdoll at the end of this frame (it needs a MeshRenderer
        /// with a model, and an AnimatedModel), every piece starting at velocity (m/s, world space).
        /// owner (say the enemy's root) is destroyed together with the ragdoll when the engine removes
        /// it; null means the model entity itself. False if there is no ragdoll system, the entity is
        /// gone, the velocity is not a real number, this model was already asked for, or it cannot
        /// be built (no model, no AnimatedModel, or no rigid pieces; the log says which). Checked
        /// now, so on false the caller can still do something else (Enemy topples).
        /// </summary>
        public static bool Spawn(Entity model, Vector3 velocity, Entity owner = null)
        {
            if (ReferenceEquals(model, null)) return false;
            int ownerIndex = ReferenceEquals(owner, null) ? -1 : unchecked((int)owner.Index);
            int ownerGeneration = ReferenceEquals(owner, null) ? 0 : unchecked((int)owner.Generation);
            return SpawnInternal(unchecked((int)model.Index), unchecked((int)model.Generation), ref velocity,
                                 ownerIndex, ownerGeneration) != 0;
        }

        /// <summary>How many ragdolls are alive now.</summary>
        public static int Count { get { return CountInternal(); } }

        // Implemented in C++: Engine/src/Scripting/Bindings/RagdollBindings.cpp.
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SpawnInternal(int index, int generation, ref Vector3 velocity, int ownerIndex, int ownerGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int CountInternal();
    }
}
