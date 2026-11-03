using System;

namespace Crucible.Scripts
{
    // Tests MeshRenderer (material / model swaps, slots) and MaterialParams (glow).
    //   1. Attach to an entity with a MeshRenderer (a model works best; a plain mesh tests M).
    //      Fields: "Alt Material" = a .mat, "Alt Model" = another model file.
    //      Add a MaterialParams component and give the material some emissive to test E.
    //   2. Press Play, then:
    //        V  toggle Visible
    //        M  swap Material: Alt Material <-> what it had
    //        N  swap Model: Alt Model <-> what it had (slot overrides are cleared)
    //        S  put Alt Material on the next material slot (wraps; logs which slot)
    //        X  clear every slot override
    //        E  toggle a glow pulse (MaterialParams.EmissiveScale 0 .. 4)
    //        C  cycle the tint (MaterialParams.Tint): red -> green -> blue -> none
    //        B  cycle built-in meshes: Cube -> Sphere -> ... -> Disc (clears the model; N brings it back)
    //        L  log the current state
    public sealed class MeshRendererTest : EntityScript
    {
        public Material altMaterial;
        public Model altModel;

        MeshRenderer mesh;
        MaterialParams glow;
        int nextTint;
        Material originalMaterial;
        Model originalModel;
        int nextSlot;
        int nextBuiltin;
        bool pulsing;

        public override void OnCreate()
        {
            mesh = GetComponent<MeshRenderer>();
            glow = GetComponent<MaterialParams>();
            if (mesh == null) { Log.Warning("[MeshRendererTest] " + Entity + " has no MeshRenderer."); return; }
            originalMaterial = mesh.Material;
            originalModel = mesh.Model;
            LogState();
            if (glow == null) Log.Info("[MeshRendererTest] no MaterialParams component - E does nothing.");
        }

        void LogState()
        {
            string slots = "";
            for (int i = 0; i < mesh.MaterialCount; ++i)
            {
                Material m = mesh.GetMaterial(i);
                slots += " [" + i + ": " + (m != null ? m.Name : "-") + "]";
            }
            Log.Info("[MeshRendererTest] visible " + mesh.Visible +
                     ", model " + (mesh.Model != null ? mesh.Model.Name : "none") +
                     ", mesh " + (mesh.Mesh != null ? mesh.Mesh.Name : "none") +
                     ", material " + (mesh.Material != null ? mesh.Material.Name : "default") +
                     ", " + mesh.MaterialCount + " slots" + slots +
                     (glow != null ? ", emissive x" + glow.EmissiveScale : ""));
        }

        public override void OnUpdate(float deltaTime)
        {
            if (mesh == null)
                return;

            if (Input.IsPressed(KeyCode.V)) { mesh.Visible = !mesh.Visible; LogState(); }

            if (Input.IsPressed(KeyCode.M))
            {
                if (altMaterial == null) Log.Warning("[MeshRendererTest] Alt Material is not set.");
                else { mesh.Material = mesh.Material == altMaterial ? originalMaterial : altMaterial; LogState(); }
            }

            if (Input.IsPressed(KeyCode.N))
            {
                if (altModel == null) Log.Warning("[MeshRendererTest] Alt Model is not set.");
                else { mesh.Model = mesh.Model == altModel ? originalModel : altModel; nextSlot = 0; LogState(); }
            }

            if (Input.IsPressed(KeyCode.S))
            {
                int count = mesh.MaterialCount;
                if (altMaterial == null || count == 0) Log.Warning("[MeshRendererTest] needs Alt Material and a model with slots.");
                else
                {
                    int slot = nextSlot % count;
                    nextSlot++;
                    Log.Info("[MeshRendererTest] slot " + slot + " -> " + altMaterial.Name + ": " + mesh.SetMaterial(slot, altMaterial));
                    LogState();
                }
            }

            if (Input.IsPressed(KeyCode.X))
            {
                for (int i = 0; i < mesh.MaterialCount; ++i) mesh.SetMaterial(i, null);
                Log.Info("[MeshRendererTest] slot overrides cleared; bad slot refused: " + !mesh.SetMaterial(999, altMaterial));
                LogState();
            }

            if (glow != null)
            {
                if (Input.IsPressed(KeyCode.E)) { pulsing = !pulsing; if (!pulsing) glow.EmissiveScale = 1f; Log.Info("[MeshRendererTest] pulse " + pulsing); }
                if (pulsing) glow.EmissiveScale = 2f + 2f * (float)Math.Sin(Time.ElapsedTime * 4.0);

                if (Input.IsPressed(KeyCode.C))
                {
                    Color[] tints = { Color.Red, Color.Green, Color.Blue };
                    if (nextTint < tints.Length) glow.Tint = tints[nextTint];
                    else glow.ClearTint();
                    nextTint = (nextTint + 1) % (tints.Length + 1);
                    Log.Info("[MeshRendererTest] tint " + glow.Tint + " (Custom " + glow.Custom + ")");
                }
            }

            if (Input.IsPressed(KeyCode.B))
            {
                Mesh[] builtins = { Mesh.Cube, Mesh.Sphere, Mesh.Capsule, Mesh.Cylinder, Mesh.Cone, Mesh.Pyramid, Mesh.Plane, Mesh.Disc };
                Mesh next = builtins[nextBuiltin % builtins.Length];
                nextBuiltin++;
                if (next == null) Log.Warning("[MeshRendererTest] a built-in mesh is missing (renamed in the renderer?)");
                else { mesh.Mesh = next; LogState(); }
            }

            if (Input.IsPressed(KeyCode.L)) LogState();
        }
    }
}
