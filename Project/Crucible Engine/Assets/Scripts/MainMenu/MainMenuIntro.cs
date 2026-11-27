/*************************************************************************
* \file:         MainMenuIntro.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          25 November 2026
* \version       1.0
* \brief         The main menu's boot intro: the golem powers on and looks
*                up at the camera, the LITHO / MACHIA logo slams in, then
*                the Start / Options / Quit buttons fade in.
*
* \details
*    Goes on the scene's "manager" entity. In order:
*      1. Waking    the torso straightens (torsoSeconds); the head starts
*                   headDelay later and lifts (headSeconds), so the body
*                   wakes first and then looks up. As the head starts to
*                   lift, the head light (MainMenuHeadLight's Sprite and
*                   Sparks) fades its alphaStart from 0 up to the
*                   Inspector value (headLightFadeSeconds).
*      2. Beat      a short pause once the golem is looking at the camera.
*      3. Slam      the logo is switched on and its Animator gets PlayStart
*                   (Logo_StartIdle -> Logo_Start -> Logo_Idle, see
*                   MainMenuLogo.animcontroller). The engine can't tell a
*                   script when a clip ends, so this waits logoSeconds (the
*                   clip's length).
*      4. Buttons   the three buttons fade in together, then become
*                   clickable.
*    Click, Space, Enter or Esc jumps straight to the end. IsFinished then
*    tells the next script (Start, the camera pans) that the menu is ready.
*
*    Unity: a Timeline, or a coroutine with yield-waits; here it is a small
*    state machine stepped by OnUpdate, using MenuTween for the motion.
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class MainMenuIntro : EntityScript
    {
        // ---- The golem (set in the Inspector) ----
        public Entity torso;                 // the "Torso" entity: the head is its child, so it turns too
        public Entity head;                  // the "Head" entity
        public float torsoHunched = 20f;     // degrees forward around X when powered off (Jinhao's numbers)
        public float torsoUpright = 0f;
        public float headHunched = 30f;      // relative to the torso
        public float headUpright = 16f;      // still tipped a little: the camera sits below its eyes
        public float torsoSeconds = 3f;
        public float headDelay = 0.6f;       // head starts this long after the torso
        public float headSeconds = 2.5f;

        // ---- The logo ----
        public Entity logo;                  // the "logo" entity with the MeshRenderer and AnimatedModel
        public float beatSeconds = 0.3f;     // pause between the golem settling and the slam
        public float logoSeconds = 1.83f;    // Logo_Start's length: LITHO lands ~0.7 s, MACHIA ~1.5 s

        // ---- The buttons ----
        public Entity startButton;           // each has a UIImage and a child "Text" with a UIText
        public Entity optionsButton;
        public Entity quitButton;
        public float buttonFadeSeconds = 0.6f;

        // ---- The head light (the glow on the golem's head) ----
        public Entity headLight;                 // the "MainMenuHeadLight" entity (found by name if left empty)
        public float headLightFadeSeconds = 2.5f;  // 0% -> 100% of each emitter's Inspector alphaStart

        // ---- Sounds (played at the camera on the SFX bus; empty = silent) ----
        public AudioClip headLightSound;     // the head light powering on (as its fade starts)
        public float headLightVolume = 1f;
        public AudioClip logoStartSound;     // the logo's Logo_Start slam
        public float logoStartVolume = 1f;

        /// <summary>True once the buttons are in and clickable (also after a skip, one frame later).</summary>
        public bool IsFinished { get; private set; }

        private enum Phase { Waking, Beat, Slam, Buttons, Finished }
        private Phase phase = Phase.Waking;
        private float phaseTime;             // seconds since the current phase began

        private MenuTween torsoTween;
        private MenuTween headTween;         // null until headDelay has passed
        private MenuTween wait;              // the Beat and the Slam
        private readonly List<MenuTween> fades = new List<MenuTween>();
        private bool slamStarted;            // PlayStart sent (decides what Skip must do)

        // One button's parts, with the opacities the scene gave them (faded back up to these).
        private struct ButtonParts
        {
            public UIButton button;
            public UIImage image;
            public UIText text;
            public float imageAlpha;
            public float textAlpha;
        }
        private readonly List<ButtonParts> buttons = new List<ButtonParts>();

        // One head light emitter (Sprite, Sparks), with the alphaStart the Inspector gave it.
        private struct GlowParts
        {
            public ParticleEmitter emitter;
            public float alphaStart;
        }
        private readonly List<GlowParts> headLightGlows = new List<GlowParts>();
        private MenuTween headLightFade;     // null until the head starts to lift

        public override void OnCreate()
        {
            if (torso == null) Log.Warning("MainMenuIntro: torso is not set.");
            if (head == null)  Log.Warning("MainMenuIntro: head is not set.");
            if (logo == null)  Log.Warning("MainMenuIntro: logo is not set.");

            // Powered off: hunched over the table. Set here, not in the scene, so the scene can be
            // saved in any pose and the intro still starts from the same place.
            if (torso != null) torso.Transform.Rotation = Quaternion.Euler(torsoHunched, 0f, 0f);
            if (head != null)  head.Transform.Rotation  = Quaternion.Euler(headHunched, 0f, 0f);

            // The logo stays hidden until its slam. Its controller starts in Logo_StartIdle (letters
            // off-screen), but the scene can't be trusted to have the renderer on or off.
            if (logo != null) logo.SetEnabled<MeshRenderer>(false);

            AddButton(startButton);
            AddButton(optionsButton);
            AddButton(quitButton);

            // The head light starts dark and powers up with the head (see StartHeadLightFade).
            if (headLight == null) headLight = Entity.Find("MainMenuHeadLight");
            if (headLight == null) Log.Warning("MainMenuIntro: headLight is not set (no \"MainMenuHeadLight\" found).");
            AddHeadLightGlow("Sprite");
            AddHeadLightGlow("Sparks");

            if (torso != null) torsoTween = MenuTween.RotateLocal(torso.Transform, Quaternion.Euler(torsoUpright, 0f, 0f), torsoSeconds);
            Enter(Phase.Waking);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (phase == Phase.Finished)
            {
                // The buttons go live one frame AFTER the intro ends. A button only counts a press
                // that lands while it is clickable, so the click that skipped the intro (pressed
                // last frame) can't also press Start if the mouse happened to be over it.
                if (!IsFinished) EnableButtons();
                return;
            }

            if (Input.IsPressed(KeyCode.MouseLeft) || Input.IsPressed(KeyCode.Space) ||
                Input.IsPressed(KeyCode.Enter) || Input.IsPressed(KeyCode.Escape))
            {
                Skip();
                return;
            }

            phaseTime += deltaTime;

            // The head light fade runs on its own clock, so it may outlast the Waking phase.
            if (headLightFade != null) headLightFade.Update(deltaTime);

            switch (phase)
            {
                case Phase.Waking:
                    if (torsoTween != null) torsoTween.Update(deltaTime);
                    if (headTween == null && head != null && phaseTime >= headDelay)
                        headTween = MenuTween.RotateLocal(head.Transform, Quaternion.Euler(headUpright, 0f, 0f), headSeconds);
                    // The head light powers up as the head starts to lift (or at once with no head).
                    if (headLightFade == null && (head == null || phaseTime >= headDelay))
                        StartHeadLightFade();
                    if (headTween != null) headTween.Update(deltaTime);
                    // Done when both have arrived (a missing torso or head counts as arrived).
                    bool torsoDone = torsoTween == null || torsoTween.Done;
                    bool headDone = head == null || (headTween != null && headTween.Done);
                    if (torsoDone && headDone)
                    {
                        wait = MenuTween.Wait(beatSeconds);
                        Enter(Phase.Beat);
                    }
                    break;

                case Phase.Beat:
                    wait.Update(deltaTime);
                    if (wait.Done)
                    {
                        StartSlam();
                        wait = MenuTween.Wait(logoSeconds);
                        Enter(Phase.Slam);
                    }
                    break;

                case Phase.Slam:
                    wait.Update(deltaTime);
                    if (wait.Done)
                    {
                        StartFades();
                        Enter(Phase.Buttons);
                    }
                    break;

                case Phase.Buttons:
                    bool allDone = true;
                    foreach (MenuTween fade in fades)
                    {
                        fade.Update(deltaTime);
                        if (!fade.Done) allDone = false;
                    }
                    if (allDone) Finish();
                    break;
            }
        }

        /*********************************************************************
         \brief  Jumps to the end of the intro from whatever phase it is in:
                 golem upright, logo stacked, buttons shown and clickable.
         *********************************************************************/
        private void Skip()
        {
            if (torsoTween != null) torsoTween.Finish();
            // The head may not have started yet (still in headDelay): put it there directly.
            if (head != null) head.Transform.Rotation = Quaternion.Euler(headUpright, 0f, 0f);

            // Head light straight to full (it may not have started fading yet).
            if (headLightFade == null) StartHeadLightFade();
            headLightFade.Finish();

            if (logo != null)
            {
                logo.SetEnabled<MeshRenderer>(true);
                // Skip leads from Logo_StartIdle and from Logo_Start straight to Logo_Idle. If the
                // slam had already ended (logo in Logo_Idle) nothing is sent: a Skip with nowhere
                // to go would stay set in the Animator (see MainMenuLogo.animcontroller).
                if (!slamStarted || phase == Phase.Slam) logo.Animator.SetTrigger("Skip");
            }

            if (phase != Phase.Buttons) StartFades();
            foreach (MenuTween fade in fades) fade.Finish();
            Finish();
        }   // MainMenuIntro::Skip()

        // Remembers one button's parts and hides it: invisible and not clickable until the fade.
        private void AddButton(Entity entity)
        {
            if (entity == null) { Log.Warning("MainMenuIntro: a button is not set."); return; }
            ButtonParts parts = new ButtonParts();
            parts.button = entity.GetComponent<UIButton>();
            parts.image = entity.GetComponent<UIImage>();
            Entity label = entity.FindChild("Text");
            parts.text = label != null ? label.GetComponent<UIText>() : null;
            if (parts.button != null) parts.button.Interactable = false;   // a skip click can't press it
            if (parts.image != null) { parts.imageAlpha = parts.image.Color.A; SetAlpha(parts.image, 0f); }
            if (parts.text != null)  { parts.textAlpha = parts.text.Color.A;  SetAlpha(parts.text, 0f); }
            buttons.Add(parts);
        }

        // Remembers one head light emitter's Inspector alphaStart and sets it to 0 (dark).
        private void AddHeadLightGlow(string childName)
        {
            if (headLight == null) return;
            Entity child = headLight.FindChild(childName);
            ParticleEmitter emitter = child != null ? child.GetComponent<ParticleEmitter>() : null;
            if (emitter == null) { Log.Warning("MainMenuIntro: no \"" + childName + "\" ParticleEmitter under the head light."); return; }
            GlowParts glow = new GlowParts();
            glow.emitter = emitter;
            glow.alphaStart = emitter.StartColor.A;   // the alphaStart typed in the Inspector
            // Readable and settable even while the component is switched off (the scene saves
            // these off): it keeps its data, it just isn't run or drawn until StartHeadLightFade.
            SetAlphaStart(emitter, 0f);
            headLightGlows.Add(glow);
        }

        /*********************************************************************
         \brief  Fades the head light's Sprite and Sparks alphaStart from 0%
                 to 100% of the value each had in the Inspector, over
                 headLightFadeSeconds.
         *********************************************************************/
        private void StartHeadLightFade()
        {
            headLightFade = MenuTween.Value(0f, 1f, headLightFadeSeconds, SetHeadLightLevel);
            // The scene saves Sprite and Sparks switched off: on now, starting from alpha 0.
            foreach (GlowParts glow in headLightGlows)
            {
                glow.emitter.Entity.SetEnabled<ParticleEmitter>(true);
                glow.emitter.Play();
            }
            PlayHeadLightSound();
        }

        /// <summary>The head light's power-on sound (headLightSound), as its alphaStart starts to rise.</summary>
        private void PlayHeadLightSound() { PlaySfx(headLightSound, headLightVolume); }

        /// <summary>The logo's slam sound (logoStartSound), as Logo_Start begins.</summary>
        private void PlayLogoStartSound() { PlaySfx(logoStartSound, logoStartVolume); }

        // Plays a clip once at the menu camera (MainMenuFlow's viewCamera, else this entity) on the
        // SFX bus. Audio.PlayClipAtPoint needs no AudioSource, so the manager's own AudioSource is
        // left to the music (MainMenuBGM).
        private void PlaySfx(AudioClip clip, float volume)
        {
            if (clip == null) return;
            MainMenuFlow flow = Entity.GetComponent<MainMenuFlow>();
            Entity ear = flow != null && flow.viewCamera != null ? flow.viewCamera : Entity;
            Audio.PlayClipAtPoint(clip, ear.Transform.WorldPosition, volume, AudioBus.SFX);
        }

        // Sets every head light emitter to `level` (0..1) of its Inspector alphaStart.
        private void SetHeadLightLevel(float level)
        {
            foreach (GlowParts glow in headLightGlows)
                SetAlphaStart(glow.emitter, glow.alphaStart * level);
        }

        // Switches the logo on and starts its slam.
        private void StartSlam()
        {
            slamStarted = true;
            if (logo == null) return;
            logo.SetEnabled<MeshRenderer>(true);
            if (!logo.Animator.SetTrigger("PlayStart"))
                Log.Warning("MainMenuIntro: the logo's Animator has no PlayStart trigger (MainMenuLogo.animcontroller).");
            else
                PlayLogoStartSound();
        }

        // Fades every button's picture and label back up to the opacity the scene gave it.
        private void StartFades()
        {
            fades.Clear();
            foreach (ButtonParts parts in buttons)
            {
                if (parts.image != null) fades.Add(MenuTween.Fade(parts.image, parts.imageAlpha, buttonFadeSeconds));
                if (parts.text != null)  fades.Add(MenuTween.Fade(parts.text, parts.textAlpha, buttonFadeSeconds));
            }
        }

        // The end, normal or skipped. The buttons go live next frame (see OnUpdate).
        private void Finish()
        {
            Enter(Phase.Finished);
        }

        // Buttons clickable, then IsFinished up for the next script.
        private void EnableButtons()
        {
            foreach (ButtonParts parts in buttons)
                if (parts.button != null) parts.button.Interactable = true;
            IsFinished = true;
        }

        private void Enter(Phase next)
        {
            phase = next;
            phaseTime = 0f;
        }

        private static void SetAlpha(UIImage image, float alpha) { Color c = image.Color; c.A = alpha; image.Color = c; }
        private static void SetAlpha(UIText text, float alpha)   { Color c = text.Color;  c.A = alpha; text.Color = c; }
        private static void SetAlphaStart(ParticleEmitter emitter, float alpha) { Color c = emitter.StartColor; c.A = alpha; emitter.StartColor = c; }

    }   // MAINMENUINTRO CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
