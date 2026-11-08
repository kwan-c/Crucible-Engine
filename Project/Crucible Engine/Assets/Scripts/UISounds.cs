/*************************************************************************
* \file:         UISounds.cs
* \brief         Plays a hover sound and a click sound for every UIButton
*                under the listed canvases.
*
* \details
*    Setup (Inspector):
*      1. Put this script on an entity that also has an AudioSource
*         (the "SFX" entity in Main Menu Scene).
*      2. Drag button_hover / button_click onto Button Hover / Button Click.
*      3. Button Roots: comma-separated names of the entities to search
*         for buttons (usually your canvases). Every UIButton under them,
*         at any depth, gets sounds - no per-button setup.
*
*    Clicks come from OnButtonClicked, which the engine sends for every
*    button. Hover has no event, so each button's State is polled and the
*    hover sound plays on the frame the mouse moves onto it.
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class UISounds : EntityScript
    {
        public AudioClip buttonHover;
        public AudioClip buttonClick;
        public float hoverVolume = 1f;
        public float clickVolume = 1f;

        /// <summary>Names of the entities to search for buttons, e.g. "Main Menu,OptionsCanvas".</summary>
        public string buttonRoots = "Main Menu,OptionsCanvas";

        private AudioSource source;
        private readonly List<UIButton> buttons = new List<UIButton>();
        private readonly List<bool> wasOver = new List<bool>();
        private bool scanned;

        public override void OnCreate()
        {
            if (!HasComponent<AudioSource>())
                Log.Warning("UISounds on '" + Entity.Name + "' needs an AudioSource component to play sounds.");
            source = AudioSource;
        }

        public override void OnUpdate(float deltaTime)
        {
            // Scan on the first update so every entity in the scene exists by then.
            if (!scanned)
            {
                FindButtons();
                scanned = true;
            }

            for (int i = 0; i < buttons.Count; ++i)
            {
                UIState state = buttons[i].State;
                bool over = state == UIState.Hovered || state == UIState.Pressed;

                // Only on entering: Pressed -> Hovered after a click doesn't replay it.
                if (over && !wasOver[i])
                    Play(buttonHover, hoverVolume);

                wasOver[i] = over;
            }
        }

        public override void OnButtonClicked(Entity button)
        {
            Play(buttonClick, clickVolume);
        }

        public override void OnDestroy()
        {
            buttons.Clear();
            wasOver.Clear();
        }

        /// <summary>Re-collects the buttons (call it if you spawn buttons at runtime).</summary>
        public void FindButtons()
        {
            buttons.Clear();
            wasOver.Clear();

            if (string.IsNullOrEmpty(buttonRoots))
                return;

            foreach (string rawName in buttonRoots.Split(','))
            {
                string name = rawName.Trim();
                if (name.Length == 0)
                    continue;

                Entity root = Entity.Find(name);
                if (root == null || !root.IsAlive)
                {
                    Log.Warning("UISounds: no entity named '" + name + "' to search for buttons.");
                    continue;
                }
                CollectButtons(root);
            }
        }

        private void CollectButtons(Entity entity)
        {
            UIButton button = entity.GetComponent<UIButton>();
            if (button != null)
            {
                buttons.Add(button);
                // Start "already over" if the mouse is on it, so nothing plays on load.
                UIState state = button.State;
                wasOver.Add(state == UIState.Hovered || state == UIState.Pressed);
            }

            Transform transform = entity.Transform;
            int count = transform.ChildCount;
            for (int i = 0; i < count; ++i)
            {
                Entity child = transform.GetChild(i);
                if (child != null && child.IsAlive)
                    CollectButtons(child);
            }
        }

        private void Play(AudioClip clip, float volume)
        {
            if (clip != null && source != null)
                source.PlayOneShot(clip, volume);
        }
    }
}
