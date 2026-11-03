   using System;
   namespace Crucible.Scripts
   {
       public sealed class TransformTest : EntityScript
       {
           public float spinSpeed = 90f;   // degrees per second
           float time;

           public override void OnCreate()
           {
               Log.Info("Start position " + Transform.Position + ", scale " + Transform.Scale);
           }

           public override void OnUpdate(float dt)
           {
               time += dt;
               Transform.Rotation = Quaternion.Euler(0, time * spinSpeed, 0);                       // spin
               Transform.Position = new Vector3((float)Math.Sin(time) * 2f, 0, 0);                  // slide
               float s = 1f + 0.25f * (float)Math.Sin(time * 3f);
               Transform.Scale = new Vector3(s, s, s);                                              // pulse
           }
       }
   }