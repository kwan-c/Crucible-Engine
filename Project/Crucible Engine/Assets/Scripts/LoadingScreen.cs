/*************************************************************************
* \file:         LoadingScreen.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       2.0
* \brief         The loading screen shown on every scene change, with a
*                progress bar that follows the real asset loading.
*
* \details
*    Nothing to set up. The engine spawns Prefabs/LoadingScreen.cprefab (the
*    prefab this script sits on) at the start of every play session and keeps
*    it through every scene change (DontDestroyOnLoad). This script then
*    claims scene changes (Scene.ClaimLoads), so every one of them - a
*    script's Scene.Load / Scene.Reload, a UIButton set to LoadScene - is held
*    until the screen has covered the old scene and loaded the new one's
*    assets. Scene.LoadImmediate skips it.
*
*    To change how it looks, edit the prefab (keep its path and name). It must
*    have, under its root UICanvas:
*      "Background"  a full-screen UIImage (its alpha is faded)
*      "Content"     a parent of the parts below, hidden while fading
*        "Bar"       a UISlider (0-1, not interactable)
*        "Label"     a UIText ("Loading 42%"), optional
*
*    What happens on a scene change:
*      1. FadeIn    the black background fades in over the old scene.
*      2. Preload   the bar appears. The next scene's assets load a few each
*                   frame (Scene.StepPreload) while the old scene is still
*                   open, so the bar shows real progress, weighted by file
*                   size.
*      3. Switch    the held scene change is let through. The change itself
*                   is one frame; the screen stays on the full bar meanwhile.
*                   What was preloaded stays loaded through it.
*      4. FadeOut   the bar hides, the black fades away to the new scene.
*
*    Camera: the canvas is Screen Space, drawn over whatever camera is
*    active, so it needs no camera of its own; the new scene's camera takes
*    over as soon as it is loaded. A future 3D loading scene would put its
*    own Camera in the prefab, raise its Priority during the load and
*    disable it in FadeOut - the scene's camera is then Camera.Main again.
*
**************************************************************************/
namespace Crucible.Scripts
{
    public sealed class LoadingScreen : EntityScript
    {
        /// <summary>Seconds for the black background to fade in, and again to fade out.</summary>
        public float fadeTime = 0.3f;

        /// <summary>Shortest time the bar stays up, so a fast load doesn't just flicker.</summary>
        public float minimumShowTime = 0.5f;

        /// <summary>Milliseconds of loading per frame. Higher loads faster but the bar moves less smoothly.</summary>
        public float loadBudgetMs = 8f;

        /// <summary>How fast the bar catches up with the real progress (per second).</summary>
        public float barSpeed = 4f;

        enum State { Idle, FadeIn, Preload, Switch, FadeOut }

        static LoadingScreen instance;   // the live one, kept across scene loads

        State state = State.Idle;
        int serial;                      // Scene.HeldLoadSerial being preloaded
        float alpha;
        float shown;                     // bar value drawn (smoothed)
        float showTimer;
        int switchFrames;

        UICanvas canvas;
        UIImage background;
        Entity content;
        UISlider bar;
        UIText label;

        /// <summary>Is a scene change through the loading screen still running?</summary>
        public static bool IsLoading
        {
            get { return (instance != null && instance.state != State.Idle) || Scene.HasHeldLoad; }
        }

        public override void OnCreate()
        {
            // A second copy (someone placed the prefab in a scene too): the engine's one wins.
            if (instance != null && instance != this && instance.Entity.IsAlive)
            {
                Destroy(Entity);
                return;
            }
            instance = this;
            DontDestroyOnLoad(Entity);   // the engine already did; harmless if spawned some other way

            canvas = GetComponent<UICanvas>();
            Entity backgroundEntity = Entity.FindChild("Background");
            background = backgroundEntity != null ? backgroundEntity.GetComponent<UIImage>() : null;
            content = Entity.FindChild("Content");
            Entity barEntity = Entity.FindChild("Bar");
            bar = barEntity != null ? barEntity.GetComponent<UISlider>() : null;
            Entity labelEntity = Entity.FindChild("Label");
            label = labelEntity != null ? labelEntity.GetComponent<UIText>() : null;
            if (canvas == null || background == null || bar == null)
                Log.Warning("LoadingScreen: the prefab needs a UICanvas root with \"Background\" (UIImage) and \"Bar\" (UISlider) children");

            Hide();
            Scene.ClaimLoads(true);
        }

        public override void OnDestroy()
        {
            if (instance != this)
                return;
            instance = null;
            Scene.CancelPreload();
            Scene.ClaimLoads(false);   // a held scene change goes through without us
        }

        public override void OnUpdate(float deltaTime)
        {
            // The frame after a scene change can be long (the whole scene was built in it):
            // don't let that one frame finish a fade.
            float dt = deltaTime < 1f / 30f ? deltaTime : 1f / 30f;

            switch (state)
            {
                case State.Idle:
                    if (Scene.HasHeldLoad)
                        Begin();
                    return;

                case State.FadeIn:
                    alpha += fadeTime > 0f ? dt / fadeTime : 1f;
                    if (alpha >= 1f)
                    {
                        alpha = 1f;
                        StartPreload();
                        SetContentVisible(true);
                        showTimer = 0f;
                        state = State.Preload;
                    }
                    break;

                case State.Preload:
                {
                    // A newer scene change replaced the one being loaded: load that one instead.
                    if (Scene.HeldLoadSerial != serial)
                        StartPreload();

                    float progress = Scene.StepPreload(loadBudgetMs);
                    showTimer += dt;
                    // 90% for the assets; the last 10% is the scene change itself.
                    SetBar(progress * 0.9f, dt);
                    if (progress >= 1f && shown >= 0.89f && showTimer >= minimumShowTime)
                    {
                        Scene.ReleaseHeldLoad();
                        SetBar(1f, 0f);
                        switchFrames = 0;
                        state = State.Switch;
                    }
                    break;
                }

                case State.Switch:
                    // The scene change happens at the start of the next frame, before this OnUpdate.
                    // Wait one more so the new scene's first (slow) frame is drawn under the screen.
                    if (++switchFrames >= 2)
                    {
                        SetContentVisible(false);
                        state = State.FadeOut;
                    }
                    break;

                case State.FadeOut:
                    SetContentVisible(false);
                    alpha -= fadeTime > 0f ? dt / fadeTime : 1f;
                    if (alpha <= 0f)
                    {
                        Hide();
                        return;   // a scene change asked for meanwhile starts next frame
                    }
                    break;
            }

            SetAlpha(alpha);
        }

        void Begin()
        {
            alpha = 0f;
            shown = 0f;
            SetBar(0f, 0f);
            SetContentVisible(false);
            SetAlpha(0f);
            if (canvas != null)
                canvas.Enabled = true;
            state = State.FadeIn;
        }

        void StartPreload()
        {
            serial = Scene.HeldLoadSerial;
            Scene.BeginPreloadHeld();   // 0 queued (a new empty scene, all loaded): the bar just fills
        }

        void Hide()
        {
            state = State.Idle;
            alpha = 0f;
            SetContentVisible(false);
            SetAlpha(0f);
            if (canvas != null)
                canvas.Enabled = false;   // hidden: draws nothing and lets clicks through
        }

        void SetAlpha(float value)
        {
            if (background == null)
                return;
            Color c = background.Color;
            c.A = value;
            background.Color = c;
        }

        void SetContentVisible(bool visible)
        {
            if (content != null && content.IsActiveSelf != visible)
                content.SetActive(visible);
        }

        void SetBar(float target, float dt)
        {
            // Ease toward the real value so the bar glides instead of jumping per asset.
            shown = dt > 0f ? shown + (target - shown) * (1f - (float)System.Math.Exp(-barSpeed * dt)) : target;
            if (target - shown < 0.005f)
                shown = target;
            if (bar != null)
                bar.Value = shown;
            if (label != null)
                label.Text = "Loading " + (int)(shown * 100f + 0.5f) + "%";
        }
    }
}
