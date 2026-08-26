#include "ui/WorldLighting.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    struct LightingKeyframe
    {
        float phase;
        Vector3 ambientColor;
        float ambientIntensity;
        float saturation;
        float localLightVisibility;
    };

    constexpr std::array<LightingKeyframe, 6> Keyframes{{
        {0.0f,       {0.58f, 0.64f, 0.80f}, 0.58f, 0.84f, 1.00f}, // Night
        {5.5f / 24,  {1.00f, 0.70f, 0.54f}, 0.78f, 0.94f, 0.00f}, // Dawn
        {9.0f / 24,  {1.00f, 0.98f, 0.94f}, 1.00f, 1.00f, 0.00f}, // Day
        {18.5f / 24, {1.00f, 0.64f, 0.44f}, 0.72f, 0.92f, 0.00f}, // Dusk
        {21.0f / 24, {0.58f, 0.64f, 0.80f}, 0.58f, 0.84f, 1.00f}, // Night
        {1.0f,       {0.58f, 0.64f, 0.80f}, 0.58f, 0.84f, 1.00f}, // Wrap
    }};

    float SmoothStep(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    float Lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    Vector3 Lerp(Vector3 a, Vector3 b, float t)
    {
        return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)};
    }
}

WorldLightingFrame ComputeWorldLighting(std::uint64_t simulationTick, const DayNightConfig& config)
{
    const std::uint64_t ticksPerDay = std::max<std::uint64_t>(1, config.ticksPerDay);
    float phase = std::fmod(config.startPhase +
                                static_cast<float>(simulationTick % ticksPerDay) / static_cast<float>(ticksPerDay),
                            1.0f);
    if (phase < 0.0f)
        phase += 1.0f;

    const LightingKeyframe* left = &Keyframes.front();
    const LightingKeyframe* right = &Keyframes.back();
    for (size_t i = 0; i + 1 < Keyframes.size(); i++)
    {
        if (phase >= Keyframes[i].phase && phase <= Keyframes[i + 1].phase)
        {
            left = &Keyframes[i];
            right = &Keyframes[i + 1];
            break;
        }
    }

    float span = std::max(0.0001f, right->phase - left->phase);
    float blend = SmoothStep((phase - left->phase) / span);

    WorldLightingFrame frame;
    frame.phase = phase;
    frame.ambientColor = Lerp(left->ambientColor, right->ambientColor, blend);
    frame.ambientIntensity = std::max(std::clamp(config.minAmbient, 0.0f, 1.0f),
                                      Lerp(left->ambientIntensity, right->ambientIntensity, blend));
    frame.saturation = Lerp(left->saturation, right->saturation, blend);
    frame.localLightVisibility = Lerp(left->localLightVisibility, right->localLightVisibility, blend);

    // Building lamps are presentation-only night lights. Keep them fully on
    // through the night and fully off as soon as daylight starts; interpolating
    // their visibility through dawn/day made the whole map look milky even
    // when the sun was already up.
    constexpr float DawnPhase = 5.5f / 24.0f;
    constexpr float NightPhase = 21.0f / 24.0f;
    frame.localLightVisibility = (phase < DawnPhase || phase >= NightPhase) ? 1.0f : 0.0f;

    // Kept deliberately stable and limited to daylight. The future directional
    // shadow pass can use this without making the sun spin through the night.
    float daylight = SmoothStep((phase - DawnPhase) / (NightPhase - DawnPhase));
    float sunHeight = std::sin(daylight * 3.14159265358979323846f);
    Vector2 rawDirection{-0.7f + daylight * 1.4f, 0.7f};
    float directionLength = std::sqrt(rawDirection.x * rawDirection.x + rawDirection.y * rawDirection.y);
    frame.sunDirection = directionLength > 0.0f
        ? Vector2{rawDirection.x / directionLength, rawDirection.y / directionLength}
        : Vector2{-0.7f, 0.7f};
    frame.shadowLength = sunHeight > 0.0f ? Lerp(144.0f, 24.0f, sunHeight) : 0.0f;

    return frame;
}

float ResolveScreenLightRadius(const LightEmitterView& light, float cameraZoom,
                               float targetScale)
{
    const float safeZoom = std::max(0.0f, cameraZoom);
    const float safeScale = std::max(0.0f, targetScale);
    const float worldRadius = light.radiusWorld * safeZoom * safeScale;
    const float minimumRadius = light.minimumScreenRadius * safeScale;
    return std::max({1.0f, worldRadius, minimumRadius});
}

Color EncodeAdditiveLightTint(Color color, float intensity)
{
    const float safeIntensity = std::max(0.0f, intensity);
    const auto scaledChannel = [safeIntensity](unsigned char channel)
    {
        return static_cast<unsigned char>(std::clamp(
            std::round(static_cast<float>(channel) * safeIntensity), 0.0f, 255.0f));
    };
    return {scaledChannel(color.r), scaledChannel(color.g),
            scaledChannel(color.b), 255};
}
