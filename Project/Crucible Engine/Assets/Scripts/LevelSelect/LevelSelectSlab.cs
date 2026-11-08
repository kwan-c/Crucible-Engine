/*************************************************************************
* \file:         LevelSelectSlab.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          8 November 2026
* \version       1.0
* \brief         One quest slab on the level select table.
*
* \details
*    Put this on each slab (Tutorial_Slab, Level1_Slab, ...). Its fields are
*    the quest shown on the QuestDescSlab when the slab is clicked - edit
*    them in the Inspector - and the level that Accept loads.
*
*    LevelSelectController decides which slab is hovered or picked; this
*    script only moves itself: it rises a little and fades in its glow
*    light (a child Point light, "Glow" by default) while raised. The
*    light's Intensity as set in the scene is the full glow, so tune the
*    glow's colour, strength and range on that light.
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class LevelSelectSlab : EntityScript
    {
        // ---- The quest (shown on the QuestDescSlab) ----
        public string questTitle = "Quest Title";
        public string questType = "Main Quest";
        /// <summary>Type \n for a new line.</summary>
        public string description = "Quest description.";
        /// <summary>The level Accept loads, inside Assets, e.g. "Scenes/Game/Game Scene.scene".</summary>
        public string levelScene = "";

        // ---- Glow ----
        /// <summary>Name of the child Point light under the slab that glows while it is raised.</summary>
        public string glowLightName = "Glow";

        // ---- Hover box ----
        // The slab's size in its model's own units (Slab1.glb), used to tell when the mouse is
        // over it. Only change these for a differently shaped slab model.
        public float boxSizeX = 2.67f;
        public float boxSizeY = 0.51f;
        public float boxSizeZ = 2.24f;
        public float boxCenterY = 0.03f;

        // Every slab in the scene, for LevelSelectController.
        internal static readonly List<LevelSelectSlab> All = new List<LevelSelectSlab>();

        private Vector3 restPosition;   // where the slab sits on the table
        private float lift;             // how far it is raised now
        private float glow;             // 0..1 of the glow light's full intensity
        private Light glowLight;
        private float glowMax;

        public override void OnCreate()
        {
            All.Add(this);
            restPosition = Transform.Position;

            Entity glowEntity = Entity.FindChild(glowLightName);
            glowLight = glowEntity != null ? glowEntity.GetComponent<Light>() : null;
            if (glowLight != null)
            {
                glowMax = glowLight.Intensity;
                glowLight.Intensity = 0f;
            }
            else
            {
                Log.Warning("LevelSelectSlab on " + Entity.Name + ": no child light named '" + glowLightName +
                            "', so it will not glow. Add a child entity with a Point light.");
            }
        }

        public override void OnDestroy()
        {
            All.Remove(this);
        }

        /// <summary>The description with each \n typed in the Inspector turned into a line break.</summary>
        internal string DescriptionWithLineBreaks
        {
            get { return (description ?? string.Empty).Replace("\\n", "\n"); }
        }

        /// <summary>Where the slab sits when it is not raised (world space; the slabs are roots).</summary>
        internal Vector3 RestPosition { get { return restPosition; } }

        internal Vector3 BoxCenter { get { return new Vector3(0f, boxCenterY, 0f); } }
        internal Vector3 BoxSize { get { return new Vector3(boxSizeX, boxSizeY, boxSizeZ); } }

        /// <summary>Does the ray pass through the slab? distance: how far along the ray.</summary>
        internal bool IsHitBy(Ray ray, out float distance)
        {
            return PickBox.RayHits(ray, Entity, BoxCenter, BoxSize, out distance);
        }

        /// <summary>
        /// Moves the slab toward raised (lifted by liftHeight, glowing) or resting.
        /// sharpness: how quickly it gets there (higher is snappier).
        /// </summary>
        internal void Animate(bool raised, float liftHeight, float sharpness, float deltaTime)
        {
            float follow = 1f - (float)Math.Exp(-sharpness * deltaTime);
            lift += ((raised ? liftHeight : 0f) - lift) * follow;
            glow += ((raised ? 1f : 0f) - glow) * follow;

            Transform.Position = restPosition + Vector3.Up * lift;
            if (glowLight != null)
                glowLight.Intensity = glowMax * glow;
        }
    }
}
