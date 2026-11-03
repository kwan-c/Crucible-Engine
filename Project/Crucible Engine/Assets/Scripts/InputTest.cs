using System;

namespace Crucible.Scripts
{
    // Attach to any entity with a Transform, press Play, and watch the Console.
    //   - Every key, mouse button and gamepad button logs "pressed" / "released" once per press.
    //   - WASD / arrow keys / left stick move the entity (tests IsDown and the stick).
    //   - Scroll, triggers and gamepad connect / disconnect log when they change.
    //   - F1 prints a full snapshot of the mouse and gamepad state.
    public sealed class InputTest : EntityScript
    {
        public float moveSpeed = 3f;          // units per second
        public bool logMouseMovement = false; // logs MouseDelta every frame the mouse moves (noisy)

        KeyCode[] allKeys;
        bool wasGamepadConnected;
        float lastLeftTrigger, lastRightTrigger;

        public override void OnCreate()
        {
            // Cache the key list once - Enum.GetValues allocates.
            var values = (KeyCode[])Enum.GetValues(typeof(KeyCode));
            allKeys = Array.FindAll(values, k => k != KeyCode.Unknown && k != KeyCode.Count);

            wasGamepadConnected = Input.IsGamepadConnected;
            Log.Info("[InputTest] Ready. Testing " + allKeys.Length + " keys. Gamepad: " +
                     (wasGamepadConnected ? Input.GamepadName : "none") + ". Press F1 for a snapshot.");
        }

        public override void OnUpdate(float deltaTime)
        {
            LogKeyEvents();
            Move(deltaTime);
            LogMouse();
            LogGamepad();

            if (Input.IsPressed(KeyCode.F1))
                LogSnapshot();
        }

        // IsPressed / IsReleased should each fire exactly once per press. IsDown is shown on
        // release so you can confirm it was still false by then.
        void LogKeyEvents()
        {
            foreach (KeyCode key in allKeys)
            {
                if (Input.IsPressed(key))
                    Log.Info("[InputTest] " + key + " pressed (IsDown=" + Input.IsDown(key) + ")");
                if (Input.IsReleased(key))
                    Log.Info("[InputTest] " + key + " released (IsDown=" + Input.IsDown(key) + ")");
            }
        }

        void Move(float deltaTime)
        {
            float x = 0f, z = 0f;
            if (Input.IsDown(KeyCode.W) || Input.IsDown(KeyCode.Up))    z += 1f;
            if (Input.IsDown(KeyCode.S) || Input.IsDown(KeyCode.Down))  z -= 1f;
            if (Input.IsDown(KeyCode.D) || Input.IsDown(KeyCode.Right)) x += 1f;
            if (Input.IsDown(KeyCode.A) || Input.IsDown(KeyCode.Left))  x -= 1f;

            Vector2 stick = Input.GamepadLeftStick;  // already -1..1 with a dead zone
            x += stick.X;
            z += stick.Y;

            if (x != 0f || z != 0f)
                Transform.Position += new Vector3(x, 0f, z) * (moveSpeed * deltaTime);
        }

        void LogMouse()
        {
            Vector2 scroll = Input.MouseScroll;
            if (scroll.X != 0f || scroll.Y != 0f)
                Log.Info("[InputTest] Scroll " + scroll);

            if (Input.IsPressed(KeyCode.MouseLeft))
                Log.Info("[InputTest] Click at " + Input.MousePosition + ", over UI: " + Input.IsPointerOverUI);

            if (logMouseMovement)
            {
                Vector2 delta = Input.MouseDelta;
                if (delta.X != 0f || delta.Y != 0f)
                    Log.Info("[InputTest] Mouse delta " + delta + " (locked: " + Input.IsMouseLocked + ")");
            }
        }

        void LogGamepad()
        {
            bool connected = Input.IsGamepadConnected;
            if (connected != wasGamepadConnected)
            {
                Log.Info(connected ? "[InputTest] Gamepad connected: " + Input.GamepadName
                                   : "[InputTest] Gamepad disconnected");
                wasGamepadConnected = connected;
            }

            // Log in steps of a quarter so a slow pull doesn't flood the Console.
            float lt = Input.GamepadLeftTrigger, rt = Input.GamepadRightTrigger;
            if (Math.Abs(lt - lastLeftTrigger) >= 0.25f || (lt == 0f && lastLeftTrigger != 0f))
            {
                Log.Info("[InputTest] Left trigger " + lt.ToString("0.00"));
                lastLeftTrigger = lt;
            }
            if (Math.Abs(rt - lastRightTrigger) >= 0.25f || (rt == 0f && lastRightTrigger != 0f))
            {
                Log.Info("[InputTest] Right trigger " + rt.ToString("0.00"));
                lastRightTrigger = rt;
            }
        }

        void LogSnapshot()
        {
            Log.Info("[InputTest] --- Snapshot ---");
            Log.Info("[InputTest] Mouse position " + Input.MousePosition + ", delta " + Input.MouseDelta +
                     ", scroll " + Input.MouseScroll);
            Log.Info("[InputTest] Mouse locked: " + Input.IsMouseLocked + ", pointer over UI: " + Input.IsPointerOverUI);
            Log.Info("[InputTest] Gamepad connected: " + Input.IsGamepadConnected + " (\"" + Input.GamepadName + "\")");
            Log.Info("[InputTest] Sticks L " + Input.GamepadLeftStick + " R " + Input.GamepadRightStick +
                     ", triggers L " + Input.GamepadLeftTrigger.ToString("0.00") +
                     " R " + Input.GamepadRightTrigger.ToString("0.00"));
        }
    }
}
