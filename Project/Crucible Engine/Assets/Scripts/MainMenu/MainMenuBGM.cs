/*************************************************************************
* \file:         MainMenuBGM.cs
* \brief         The main menu's background music: starts as soon as the
*                menu scene loads and loops until the scene is left.
*
* \details
*    Goes on the scene's "manager" entity, which also needs an AudioSource
*    component (add it in the Inspector; scripts can't add components).
*    That AudioSource is used for the music only: the menu's sound effects
*    (MainMenuIntro, MainMenuFlow, TorsoSelector) are played with
*    Audio.PlayClipAtPoint instead, so they stay on the SFX bus.
*
*    The music plays on the Music bus, so the options menu's Music slider
*    controls it, and fades in from silence over fadeInSeconds. It is 2D
*    (spatialBlend 0): the same in both ears wherever the camera goes.
*
**************************************************************************/
namespace Crucible.Scripts
{
    public sealed class MainMenuBGM : EntityScript
    {
        public AudioClip music;              // the menu track
        public float volume = 0.6f;          // 0..1, before the player's Music slider
        public bool loop = true;
        public float fadeInSeconds = 1.5f;   // 0 = full volume at once

        private AudioSource source;
        private MenuTween fadeIn;

        public override void OnCreate()
        {
            if (music == null) { Log.Warning("MainMenuBGM: no music clip set."); return; }
            if (!HasComponent<AudioSource>())
            {
                Log.Warning("MainMenuBGM: the '" + Entity.Name + "' entity needs an AudioSource component to play music.");
                return;
            }
            PlayMusic();
        }

        public override void OnUpdate(float deltaTime)
        {
            if (fadeIn == null) return;
            fadeIn.Update(deltaTime);
            if (fadeIn.Done) fadeIn = null;
        }

        /*********************************************************************
         \brief  Starts the menu music from the beginning: Music bus, 2D,
                 looping, fading in from silence over fadeInSeconds.
         *********************************************************************/
        public void PlayMusic()
        {
            source = AudioSource;
            source.Clip = music;
            source.Bus = AudioBus.Music;
            source.Loop = loop;
            source.SpatialBlend = 0f;
            source.Volume = fadeInSeconds > 0f ? 0f : volume;
            source.Play();
            fadeIn = fadeInSeconds > 0f
                ? MenuTween.Value(0f, volume, fadeInSeconds, v => source.Volume = v, Ease.Linear)
                : null;
        }

        /// <summary>Stops the menu music at once.</summary>
        public void StopMusic()
        {
            fadeIn = null;
            if (source != null) source.Stop();
        }

    }   // MAINMENUBGM CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
