#include "ui/WorldLighting.h"

#include <cmath>

#include <gtest/gtest.h>

TEST(WorldLightingTests, UsesTheConfiguredStartingPhase)
{
    DayNightConfig config;
    config.ticksPerDay = 24000;
    config.startPhase = 0.0f;

    WorldLightingFrame frame = ComputeWorldLighting(0, config);

    EXPECT_NEAR(frame.phase, 0.0f, 0.0001f);
    EXPECT_NEAR(frame.ambientIntensity, 0.58f, 0.0001f);
    EXPECT_NEAR(frame.ambientColor.z, 0.80f, 0.0001f);
    EXPECT_NEAR(frame.localLightVisibility, 1.0f, 0.0001f);
}

TEST(WorldLightingTests, DefaultsToNoon)
{
    WorldLightingFrame frame = ComputeWorldLighting(0);

    EXPECT_NEAR(frame.phase, 0.50f, 0.0001f);
    EXPECT_GT(frame.ambientIntensity, 0.90f);
    EXPECT_GT(frame.shadowLength, 0.0f);
}

TEST(WorldLightingTests, DirectionalShadowVanishesAtNight)
{
    DayNightConfig config;
    config.startPhase = 0.0f;
    config.ticksPerDay = 24000;

    WorldLightingFrame night = ComputeWorldLighting(0, config);
    WorldLightingFrame morning = ComputeWorldLighting(7000, config);

    EXPECT_FLOAT_EQ(night.shadowLength, 0.0f);
    EXPECT_GT(morning.shadowLength, 0.0f);
}

TEST(WorldLightingTests, RepeatsExactlyAfterOneDay)
{
    DayNightConfig config;
    config.ticksPerDay = 24000;
    config.startPhase = 0.0f;

    WorldLightingFrame first = ComputeWorldLighting(5123, config);
    WorldLightingFrame repeated = ComputeWorldLighting(5123 + config.ticksPerDay, config);

    EXPECT_NEAR(repeated.phase, first.phase, 0.0001f);
    EXPECT_NEAR(repeated.ambientIntensity, first.ambientIntensity, 0.0001f);
    EXPECT_NEAR(repeated.ambientColor.x, first.ambientColor.x, 0.0001f);
    EXPECT_NEAR(repeated.localLightVisibility, first.localLightVisibility, 0.0001f);
}

TEST(WorldLightingTests, HonorsTheMinimumAmbientFloor)
{
    DayNightConfig config;
    config.startPhase = 0.0f;
    config.minAmbient = 0.80f;

    WorldLightingFrame frame = ComputeWorldLighting(0, config);

    EXPECT_NEAR(frame.ambientIntensity, 0.80f, 0.0001f);
}

TEST(WorldLightingTests, InterpolatesSmoothlyAcrossDawn)
{
    DayNightConfig config;
    config.ticksPerDay = 24000;
    config.startPhase = 0.0f;
    constexpr std::uint64_t dawnTick = 5500;

    WorldLightingFrame before = ComputeWorldLighting(dawnTick - 1, config);
    WorldLightingFrame after = ComputeWorldLighting(dawnTick + 1, config);

    EXPECT_LT(std::abs(after.ambientIntensity - before.ambientIntensity), 0.002f);
    EXPECT_LT(std::abs(after.ambientColor.y - before.ambientColor.y), 0.002f);
}

TEST(WorldLightingTests, BuildingLightsAreOnlyVisibleAtNight)
{
    DayNightConfig config;
    config.ticksPerDay = 24000;
    config.startPhase = 0.0f;

    const WorldLightingFrame night = ComputeWorldLighting(0, config);
    const WorldLightingFrame day = ComputeWorldLighting(12000, config);
    const WorldLightingFrame dusk = ComputeWorldLighting(20000, config);

    EXPECT_FLOAT_EQ(night.localLightVisibility, 1.0f);
    EXPECT_FLOAT_EQ(day.localLightVisibility, 0.0f);
    EXPECT_FLOAT_EQ(dusk.localLightVisibility, 0.0f);
}

TEST(WorldLightingTests, DaylightDoesNotUseNightLightSaturationBoost)
{
    DayNightConfig config;
    config.ticksPerDay = 24000;
    config.startPhase = 0.0f;

    const WorldLightingFrame day = ComputeWorldLighting(12000, config);

    EXPECT_LE(day.saturation, 1.0f);
    EXPECT_FLOAT_EQ(day.localLightVisibility, 0.0f);
}

TEST(WorldLightingTests, EssentialLightKeepsItsScreenFootprintWhenZoomedOut)
{
    LightEmitterView light;
    light.radiusWorld = 160.0f;
    light.minimumScreenRadius = 56.0f;

    EXPECT_FLOAT_EQ(ResolveScreenLightRadius(light, 1.0f), 160.0f);
    EXPECT_FLOAT_EQ(ResolveScreenLightRadius(light, 0.10f), 56.0f);
    EXPECT_FLOAT_EQ(ResolveScreenLightRadius(light, 0.10f, 0.5f), 28.0f);
}

TEST(WorldLightingTests, OrdinaryLightStillScalesWithCameraZoom)
{
    LightEmitterView light;
    light.radiusWorld = 96.0f;

    EXPECT_FLOAT_EQ(ResolveScreenLightRadius(light, 1.0f), 96.0f);
    EXPECT_FLOAT_EQ(ResolveScreenLightRadius(light, 0.5f), 48.0f);
}

TEST(WorldLightingTests, AdditiveLightTintStoresIntensityPerEmitter)
{
    const Color warm = EncodeAdditiveLightTint(Color{255, 160, 80, 255}, 0.20f);
    const Color cool = EncodeAdditiveLightTint(Color{60, 140, 240, 255}, 0.20f);

    EXPECT_EQ(warm.r, 51);
    EXPECT_EQ(warm.g, 32);
    EXPECT_EQ(warm.b, 16);
    EXPECT_GT(warm.r, warm.b);
    EXPECT_GT(cool.b, cool.r);
    EXPECT_EQ(warm.a, 255);
    EXPECT_EQ(cool.a, 255);
}

TEST(WorldLightingTests, AdditiveLightTintClampsInvalidIntensity)
{
    const Color dark = EncodeAdditiveLightTint(Color{255, 200, 100, 255}, -1.0f);
    const Color saturated = EncodeAdditiveLightTint(Color{255, 200, 100, 255}, 4.0f);

    EXPECT_EQ(dark.r, 0);
    EXPECT_EQ(dark.g, 0);
    EXPECT_EQ(dark.b, 0);
    EXPECT_EQ(saturated.r, 255);
    EXPECT_EQ(saturated.g, 255);
    EXPECT_EQ(saturated.b, 255);
}
