/*************************************************************************
* \file:         MenuTween.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          25 November 2026
* \version       1.0
* \brief         Moves a value from where it is now to a target over a set
*                time, with easing ("tweening"). The main menu uses it for
*                camera pans, the golem looking up, button fades and floats,
*                and the ground sliding back.
*
* \details
*    The script that starts a tween owns it and updates it every frame
*    (no hidden global runner, so the order of the menu's steps is all in
*    that one script):
*        pan = MenuTween.Move(camera.Transform, target, 1.5f);   // start
*        pan.Update(deltaTime);                                   // every frame
*        if (pan.Done) { ... next step ... }
*    Finish() jumps straight to the end value: that is how the intro is
*    skipped.
*
*    Every starter reads the CURRENT value as its "from", so a tween can
*    begin wherever the thing happens to be (Back pressed halfway through a
*    pan, say) without a jump.
*
*    Unity: a coroutine with Vector3.Lerp, or DOTween's transform.DOMove.
*    Our engine has no Animation window (no keyframed property clips), so
*    this does the same job in code.
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    /// <summary>How a tween speeds up and slows down over its time.</summary>
    public enum Ease
    {
        Linear,   // same speed all the way, then a dead stop: looks mechanical
        In,       // starts slow, ends fast: things falling
        Out,      // starts fast, ends slow: things arriving (buttons floating up)
        InOut     // slow, fast, slow: camera pans, the golem looking up (the default)
    }

    /// <summary>One running tween. Start one with Move, Rotate, RotateLocal, Fade, Slide or Wait.</summary>
    public sealed class MenuTween
    {
        private readonly float duration;          // seconds from start to end
        private readonly Ease ease;
        private readonly Action<float> apply;     // writes the value for an eased progress 0..1 (null for Wait)
        private float elapsed;                    // seconds so far

        // Private: callers use the starters below, which capture the "from" value.
        private MenuTween(float seconds, Ease ease, Action<float> apply)
        {
            duration = seconds;
            this.ease = ease;
            this.apply = apply;
        }

        /// <summary>True once the end value has been written. Update and Finish do nothing after.</summary>
        public bool Done { get; private set; }

        /*********************************************************************
         \brief  Moves the tween on by deltaTime seconds and writes the new
                 value. Call it once per frame from the owner's OnUpdate.
         *********************************************************************/
        public void Update(float deltaTime)
        {
            if (Done) return;
            elapsed += deltaTime;
            // A zero-length tween counts as finished at once (no divide by zero).
            float t = duration > 0f ? elapsed / duration : 1f;
            if (t >= 1f)
            {
                // Finish rather than apply(t): t can overshoot 1 on a long frame, and the end
                // value must be exact (a camera that stops 1 mm short drifts on the next pan).
                Finish();
                return;
            }
            if (apply != null) apply(Evaluate(ease, t));
        }   // MenuTween::Update()

        /// <summary>Jumps to the end value at once (skipping). Safe to call more than once.</summary>
        public void Finish()
        {
            if (Done) return;
            Done = true;
            if (apply != null) apply(1f);
        }   // MenuTween::Finish()

        // ------------------------------------------------------------------ starters

        /// <summary>Slides a Transform's WORLD position to `to` (camera pans, the ground sliding back).</summary>
        public static MenuTween Move(Transform transform, Vector3 to, float seconds, Ease ease = Ease.InOut)
        {
            Vector3 from = transform.WorldPosition;
            return new MenuTween(seconds, ease, t => transform.WorldPosition = from + (to - from) * t);
        }

        /// <summary>Turns a Transform's WORLD rotation to `to` (the camera aiming at its next spot).</summary>
        public static MenuTween Rotate(Transform transform, Quaternion to, float seconds, Ease ease = Ease.InOut)
        {
            Quaternion from = transform.WorldRotation;
            // Slerp, not Lerp: it turns at an even speed along the shortest arc.
            return new MenuTween(seconds, ease, t => transform.WorldRotation = Quaternion.Slerp(from, to, t));
        }

        /// <summary>
        /// Turns a Transform's rotation relative to its PARENT (the golem's head and torso). The
        /// head is a child of the torso, so turning both in local space adds up the way a neck
        /// does; in world space the head would ignore the torso's turn.
        /// </summary>
        public static MenuTween RotateLocal(Transform transform, Quaternion to, float seconds, Ease ease = Ease.InOut)
        {
            Quaternion from = transform.Rotation;
            return new MenuTween(seconds, ease, t => transform.Rotation = Quaternion.Slerp(from, to, t));
        }

        /// <summary>Fades a UI image's opacity to toAlpha (0 hidden, 1 solid). Its colour stays.</summary>
        public static MenuTween Fade(UIImage image, float toAlpha, float seconds, Ease ease = Ease.InOut)
        {
            Color from = image.Color;
            return new MenuTween(seconds, ease, t =>
            {
                Color colour = from;   // a struct copy: only its alpha changes
                colour.A = from.A + (toAlpha - from.A) * t;
                image.Color = colour;
            });
        }

        /// <summary>Fades a UI text's opacity to toAlpha (a button's label fades with its image).</summary>
        public static MenuTween Fade(UIText text, float toAlpha, float seconds, Ease ease = Ease.InOut)
        {
            Color from = text.Color;
            return new MenuTween(seconds, ease, t =>
            {
                Color colour = from;
                colour.A = from.A + (toAlpha - from.A) * t;
                text.Color = colour;
            });
        }

        /// <summary>
        /// Slides a UI element to `to` (reference pixels from its anchors, +y DOWN): the buttons
        /// floating up. Out by default: it arrives gently instead of stopping hard.
        /// </summary>
        public static MenuTween Slide(UIRect rect, Vector2 to, float seconds, Ease ease = Ease.Out)
        {
            Vector2 from = rect.Position;
            return new MenuTween(seconds, ease, t => rect.Position = from + (to - from) * t);
        }

        /// <summary>A pause: Done after `seconds`, changes nothing ("a short moment later", a clip's length).</summary>
        public static MenuTween Wait(float seconds)
        {
            return new MenuTween(seconds, Ease.Linear, null);
        }

        // ------------------------------------------------------------------ easing

        /*********************************************************************
         \brief  Bends linear progress t (0..1) into eased progress (0..1).
                 Cubic curves: gentle enough for a slow camera pan, and the
                 same ones DOTween calls InCubic / OutCubic / InOutCubic.
         *********************************************************************/
        public static float Evaluate(Ease ease, float t)
        {
            if (t <= 0f) return 0f;
            if (t >= 1f) return 1f;
            switch (ease)
            {
                case Ease.In:
                    return t * t * t;
                case Ease.Out:
                {
                    float u = 1f - t;   // the In curve, played backwards
                    return 1f - u * u * u;
                }
                case Ease.InOut:
                {
                    if (t < 0.5f) return 4f * t * t * t;   // first half: the In curve, squeezed
                    float u = -2f * t + 2f;                 // second half: the Out curve, squeezed
                    return 1f - u * u * u / 2f;
                }
                default:
                    return t;
            }
        }   // MenuTween::Evaluate()

    }   // MENUTWEEN CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
