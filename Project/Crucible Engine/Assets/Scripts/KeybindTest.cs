using System;

namespace Crucible.Scripts
{
    // Tests InputSettings (the player's keybinds, saved in UserSettings.json) and PlayerController.
    //   1. Attach to the mech (the entity with PlayerController + CharacterController).
    //   2. Press Play, then:
    //        F1  swap WASD <-> arrow keys
    //        F2  rebind jump: press F2, then the key you want (Esc cancels)
    //        F3  toggle sprint (PlayerController.MoveSpeed x2)
    //        F4  switch the PlayerController off / on (the mech stops and ignores keys)
    //        F5  log the current settings
    //        F6  save to UserSettings.json (next to Editor.exe / the game .exe)
    //        F7  reset the keys to W A S D / Space / GamepadA
    //   3. To check saving: change keys, F6, close the editor, reopen - the keys should stay.
    //      Without F6 they last until the editor closes (Stop does not undo them).
    public sealed class KeybindTest : EntityScript
    {
        PlayerController pc;
        float baseSpeed;
        bool sprint, waitingForJumpKey;

        public override void OnCreate()
        {
            pc = GetComponent<PlayerController>();
            if (pc == null) Log.Warning("[KeybindTest] " + Entity + " has no PlayerController component.");
            else baseSpeed = pc.MoveSpeed;
            LogSettings();
        }

        void LogSettings()
        {
            Log.Info("[KeybindTest] forward " + InputSettings.ForwardKey + ", back " + InputSettings.BackKey +
                     ", left " + InputSettings.LeftKey + ", right " + InputSettings.RightKey +
                     ", jump " + InputSettings.JumpKey + " / " + InputSettings.JumpButton +
                     ", mouse " + InputSettings.MouseSensitivity + ", stick " + InputSettings.StickSensitivity +
                     ", invertY " + InputSettings.InvertY +
                     (pc != null ? ", speed " + pc.MoveSpeed + ", enabled " + Entity.IsEnabled<PlayerController>() : ""));
        }

        public override void OnUpdate(float deltaTime)
        {
            // Waiting for the new jump key: the first key pressed this frame wins.
            if (waitingForJumpKey)
            {
                if (Input.IsPressed(KeyCode.Escape)) { waitingForJumpKey = false; Log.Info("[KeybindTest] Rebind cancelled"); return; }
                foreach (KeyCode key in Enum.GetValues(typeof(KeyCode)))
                {
                    if (key == KeyCode.Unknown || key == KeyCode.Count || key == KeyCode.F2 || !Input.IsPressed(key))
                        continue;
                    InputSettings.JumpKey = key;
                    waitingForJumpKey = false;
                    Log.Info("[KeybindTest] Jump is now " + InputSettings.JumpKey);
                    break;
                }
                return;
            }

            if (Input.IsPressed(KeyCode.F1))
            {
                bool arrows = InputSettings.ForwardKey != KeyCode.Up;
                InputSettings.ForwardKey = arrows ? KeyCode.Up : KeyCode.W;
                InputSettings.BackKey = arrows ? KeyCode.Down : KeyCode.S;
                InputSettings.LeftKey = arrows ? KeyCode.Left : KeyCode.A;
                InputSettings.RightKey = arrows ? KeyCode.Right : KeyCode.D;
                LogSettings();
            }
            if (Input.IsPressed(KeyCode.F2))
            {
                waitingForJumpKey = true;
                Log.Info("[KeybindTest] Press the new jump key (Esc cancels)");
            }
            if (pc != null && Input.IsPressed(KeyCode.F3))
            {
                sprint = !sprint;
                pc.MoveSpeed = sprint ? baseSpeed * 2f : baseSpeed;
                Log.Info("[KeybindTest] MoveSpeed " + pc.MoveSpeed);
            }
            if (pc != null && Input.IsPressed(KeyCode.F4))
            {
                bool on = !Entity.IsEnabled<PlayerController>();
                Entity.SetEnabled<PlayerController>(on);
                Log.Info("[KeybindTest] PlayerController enabled " + on);
            }
            if (Input.IsPressed(KeyCode.F5))
                LogSettings();
            if (Input.IsPressed(KeyCode.F6))
                Log.Info("[KeybindTest] Saved: " + InputSettings.Save());
            if (Input.IsPressed(KeyCode.F7))
            {
                InputSettings.ResetKeys();
                LogSettings();
            }
        }
    }
}
