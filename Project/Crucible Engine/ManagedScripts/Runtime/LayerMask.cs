/*************************************************************************
* \file:         LayerMask.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          7 November 2026
* \version       1.0
* \brief         Layer masks for physics queries: which PhysicsLayers a
*                ray or sphere may hit. Unity's LayerMask.GetMask. Part of
*                Crucible.ScriptCore.dll.
*
* \details
*    A mask is a plain int with one bit per PhysicsLayer, exactly like
*    Unity's, so it passes straight into a query:
*
*        int walls = LayerMask.GetMask(PhysicsLayer.NonMoving, PhysicsLayer.Moving);
*        if (Physics.Raycast(eye, forward, out hit, 20f, walls, Entity)) ...
*
*    Built from layer names rather than 1 << n, so it can't be got wrong.
*    Unity's GetMask takes strings; ours takes the PhysicsLayer enum, so a
*    typo is a compile error instead of a silent empty mask.
*
**************************************************************************/
namespace Crucible
{
    /// <summary>Layer masks for physics queries (Unity's LayerMask): one bit per PhysicsLayer.</summary>
    public static class LayerMask
    {
        /// <summary>Every layer: what a query hits when given no mask.</summary>
        public const int All = ~0;

        /// <summary>A mask with just these layers in it.</summary>
        public static int GetMask(params PhysicsLayer[] layers)
        {
            int mask = 0;
            foreach (PhysicsLayer layer in layers) mask |= 1 << (int)layer;
            return mask;
        }
    }
}
