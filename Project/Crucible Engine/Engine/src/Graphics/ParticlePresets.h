#pragma once
/*************************************************************************
* \file:         ParticlePresets.h
* \brief
*                Ready-made ParticleEmitter settings for the common effects.
*
* \details
*                A ParticleEmitter has a lot of fields; these presets are
*                sensible starting points to tweak in the Inspector. The
*                editor's Hierarchy "+" menu uses them, and gameplay code can
*                too:
*                \code
*                Entity e = world.CreateEntity();
*                world.AddComponent<ParticleEmitter>(e, MakeParticlePreset(ParticlePreset::Sparks));
*                \endcode
*                Colours are authored in sRGB, like every colour in the
*                Inspector; ParticleSystem converts them.
**************************************************************************/

#include "ECS/Components/Components.h"

namespace Crucible {

    enum class ParticlePreset { Sparks, Fire, Smoke, Splash };

    /*************************************************************************
     \brief  A ParticleEmitter filled in for one of the common effects.
     \param[in] preset Which effect.
     \return The component, ready to add to an entity.
     *************************************************************************/
    inline ParticleEmitter MakeParticlePreset(ParticlePreset preset) {

        ParticleEmitter p;
        switch (preset) {

        case ParticlePreset::Sparks:
            // A burst of fast, bright streaks that arc under gravity and die quickly. Lights its
            // surroundings for as long as the sparks are hot.
            p.loop = true;            p.duration = 1.5f;
            p.rate = 0.f;             p.burstCount = 60;        p.maxParticles = 200;
            p.shape = ParticleShape::Point;                     p.spreadDeg = 70.f;
            p.lifetimeMin = 0.35f;    p.lifetimeMax = 0.9f;
            p.speedMin = 3.f;         p.speedMax = 8.f;
            p.sizeMin = 0.025f;       p.sizeMax = 0.05f;        p.endSizeScale = 0.3f;
            p.gravityScale = 1.f;     p.drag = 0.8f;            p.stretch = 0.04f;
            p.colourStart = { 1.f, 0.85f, 0.45f };              p.colourEnd = { 1.f, 0.3f, 0.05f };
            p.alphaStart = 1.f;       p.alphaEnd = 0.f;         p.brightness = 12.f;
            p.blend = ParticleBlend::Additive;
            p.emitLight = true;       p.lightColour = { 1.f, 0.6f, 0.25f };
            p.lightIntensity = 10.f;  p.lightRange = 6.f;
            break;

        case ParticlePreset::Fire:
            // Soft blobs rising and shrinking, yellow at the base fading to dark red. A steady
            // warm light.
            p.loop = true;            p.duration = 5.f;
            p.rate = 45.f;            p.burstCount = 0;         p.maxParticles = 300;
            p.shape = ParticleShape::Sphere;                    p.shapeRadius = 0.2f;   p.spreadDeg = 12.f;
            p.lifetimeMin = 0.6f;     p.lifetimeMax = 1.2f;
            p.speedMin = 0.8f;        p.speedMax = 1.8f;
            p.sizeMin = 0.35f;        p.sizeMax = 0.6f;         p.endSizeScale = 0.25f;
            p.gravityScale = -0.15f;  p.drag = 0.5f;            p.spinDeg = 90.f;
            p.colourStart = { 1.f, 0.75f, 0.3f };               p.colourEnd = { 0.8f, 0.12f, 0.02f };
            p.alphaStart = 0.55f;     p.alphaEnd = 0.f;         p.brightness = 3.f;
            p.blend = ParticleBlend::Additive;
            p.emitLight = true;       p.lightColour = { 1.f, 0.55f, 0.2f };
            p.lightIntensity = 14.f;  p.lightRange = 8.f;
            break;

        case ParticlePreset::Smoke:
            // Big, slow, grey puffs that grow and thin out as they rise. Alpha-blended, no light.
            p.loop = true;            p.duration = 5.f;
            p.rate = 12.f;            p.burstCount = 0;         p.maxParticles = 200;
            p.shape = ParticleShape::Sphere;                    p.shapeRadius = 0.15f;  p.spreadDeg = 15.f;
            p.lifetimeMin = 2.5f;     p.lifetimeMax = 4.f;
            p.speedMin = 0.5f;        p.speedMax = 1.f;
            p.sizeMin = 0.5f;         p.sizeMax = 0.8f;         p.endSizeScale = 3.f;
            p.gravityScale = -0.03f;  p.drag = 0.3f;            p.spinDeg = 30.f;
            p.colourStart = { 0.35f, 0.35f, 0.36f };            p.colourEnd = { 0.6f, 0.6f, 0.62f };
            p.alphaStart = 0.35f;     p.alphaEnd = 0.f;         p.brightness = 1.f;
            p.blend = ParticleBlend::Alpha;
            p.emitLight = false;
            break;

        case ParticlePreset::Splash:
            // One burst of droplets thrown up and outward, falling back under gravity.
            p.loop = true;            p.duration = 2.f;
            p.rate = 0.f;             p.burstCount = 80;        p.maxParticles = 200;
            p.shape = ParticleShape::Sphere;                    p.shapeRadius = 0.1f;   p.spreadDeg = 50.f;
            p.lifetimeMin = 0.5f;     p.lifetimeMax = 1.0f;
            p.speedMin = 2.5f;        p.speedMax = 5.5f;
            p.sizeMin = 0.04f;        p.sizeMax = 0.09f;        p.endSizeScale = 0.5f;
            p.gravityScale = 1.f;     p.drag = 0.4f;            p.stretch = 0.03f;
            p.colourStart = { 0.75f, 0.88f, 1.f };              p.colourEnd = { 0.6f, 0.8f, 1.f };
            p.alphaStart = 0.8f;      p.alphaEnd = 0.f;         p.brightness = 1.f;
            p.blend = ParticleBlend::Alpha;
            p.emitLight = false;
            break;
        }
        return p;
    }

}   // NAMESPACE CRUCIBLE
