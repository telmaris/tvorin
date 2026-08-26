#include "ui/Renderer.h"
#include "core/FogOfWar.h"
#include "core/Log.h"
#include "core/RoadTopology.h"
#include "economy/Building.h"
#include "economy/Player.h"
#include "ui/TerrainRenderGeometry.h"
#include "ui/UiText.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool fogOfWarPreferenceEnabled = true;
    bool colorGradingPreferenceEnabled = true;
    bool retroFilterPreferenceEnabled = true;
    bool localLightBloomPreferenceEnabled = true;
    bool rainOverlayPreferenceEnabled = false;
    bool logisticsOverlayPreferenceEnabled = false;
    float sceneTransitionOverlayAlpha = 0.0f;

    struct MineralOverlayStyle
    {
        Color shadow;
        Color base;
        Color highlight;
        Color glow;
        float glowStrength;
        float lightRadius;
        float lightIntensity;
        float flickerAmount;
        float luminanceScale;
        float luminanceBias;
        float edgeHighlightStrength;
    };

    // Atlas 41 is grouped in twelve-cell blocks: four full variants followed
    // by eight directional rim variants. Keeping the palette here means the
    // source alpha (and therefore every authored rock shape) stays untouched.
    constexpr MineralOverlayStyle CoalOverlayStyle{
        {6, 9, 12, 255}, {25, 30, 35, 255}, {151, 159, 164, 255},
        {205, 212, 216, 255}, 0.44f, 88.0f, 0.026f, 0.025f, 1.18f, -0.11f, 0.78f};
    constexpr MineralOverlayStyle IronOverlayStyle{
        {20, 37, 64, 255}, {68, 124, 181, 255}, {183, 216, 237, 255},
        {63, 143, 213, 255}, 0.68f, 102.0f, 0.046f, 0.035f, 1.20f, 0.02f, 0.38f};
    constexpr MineralOverlayStyle CopperOverlayStyle{
        {67, 23, 19, 255}, {184, 70, 48, 255}, {242, 145, 103, 255},
        {225, 65, 39, 255}, 0.74f, 100.0f, 0.058f, 0.032f, 1.17f, 0.01f, 0.0f};
    constexpr MineralOverlayStyle StoneOverlayStyle{
        {51, 55, 60, 255}, {179, 185, 190, 255}, {240, 242, 238, 255},
        {223, 229, 228, 255}, 0.56f, 92.0f, 0.038f, 0.025f, 1.18f, 0.02f, 0.34f};

    const MineralOverlayStyle* GetMineralOverlayStyle(int textureId)
    {
        if (textureId >= 0 && textureId <= 11) return &CoalOverlayStyle;
        if (textureId >= 12 && textureId <= 23) return &IronOverlayStyle;
        if (textureId >= 24 && textureId <= 35) return &CopperOverlayStyle;
        if (textureId >= 36 && textureId <= 47) return &StoneOverlayStyle;
        return nullptr;
    }

    void DrawRenderTarget(Texture2D texture, Rectangle destination,
                          RenderTargetDestination target, Color tint = WHITE)
    {
        DrawTexturePro(texture, RenderTargetSourceRect(texture, target), destination,
                       {0.0f, 0.0f}, 0.0f, tint);
    }

    float SnapZoomToTileGrid(float zoom)
    {
        const float pixelsPerTile = std::max(1.0f,
            std::round(zoom * static_cast<float>(TILE_SIZE)));
        return pixelsPerTile / static_cast<float>(TILE_SIZE);
    }

    void SnapCameraTargetToRenderPixels(Camera2D& camera)
    {
        if (camera.zoom <= 0.0f)
            return;

        camera.target.x = std::round(camera.target.x * camera.zoom) / camera.zoom;
        camera.target.y = std::round(camera.target.y * camera.zoom) / camera.zoom;
    }

    // Bakery smoke is deliberately a separate visual effect instead of an
    // animated sprite sheet: the building remains completely stable while
    // only the smoke rises and disperses above its chimney.
    void DrawBakerySmoke(Vec2f position, Vec2i footprint, float elapsedTime)
    {
        const float width = static_cast<float>(footprint.x * TILE_SIZE);
        const float height = static_cast<float>(footprint.y * TILE_SIZE);
        const float chimneyX = position.x + width * 0.76f;
        const float chimneyY = RENDER_HEIGHT - position.y - height * 0.86f;
        constexpr float SmokeCycleSeconds = 2.4f;
        const float phaseOffset = std::fmod(position.x * 0.0031f + position.y * 0.0017f,
                                             SmokeCycleSeconds);

        for (int puff = 0; puff < 3; ++puff)
        {
            float age = std::fmod(elapsedTime + phaseOffset + puff * 0.72f,
                                  SmokeCycleSeconds) / SmokeCycleSeconds;
            if (age < 0.0f)
                age += 1.0f;

            const float drift = std::sin((age + static_cast<float>(puff) * 0.31f) * 5.2f) *
                                (2.0f + age * 4.0f);
            const float x = chimneyX + drift;
            const float y = chimneyY - 3.0f - age * 30.0f;
            const float radius = 3.0f + age * 5.0f;
            const float fade = 1.0f - age;
            const unsigned char alpha = static_cast<unsigned char>(42.0f + fade * 68.0f);
            DrawCircleGradient(static_cast<int>(std::round(x)), static_cast<int>(std::round(y)), radius,
                               Color{196, 202, 212, alpha}, Color{156, 165, 178, 0});
        }
    }

    void DrawDamageFlashOverlay(Vec2f position, Vec2i footprint, float remainingSeconds)
    {
        if (remainingSeconds <= 0.0f)
            return;
        const float width = footprint.x * TILE_SIZE;
        const float height = footprint.y * TILE_SIZE;
        const float top = RENDER_HEIGHT - position.y - height;
        const float pulse = 0.45f + 0.55f * std::abs(std::sin(static_cast<float>(GetTime()) * 10.0f));
        const unsigned char fillAlpha = static_cast<unsigned char>(18.0f + pulse * 28.0f);
        const unsigned char lineAlpha = static_cast<unsigned char>(125.0f + pulse * 110.0f);
        DrawRectangle(static_cast<int>(position.x), static_cast<int>(top),
                      static_cast<int>(width), static_cast<int>(height), Color{255, 58, 32, fillAlpha});
        DrawRectangleLinesEx({position.x - 2.0f, top - 2.0f, width + 4.0f, height + 4.0f},
                             2.5f, Color{255, 110, 72, lineAlpha});
    }

    void DrawRoadUtilizationOverlay(Vec2f position, float utilization,
                                    bool left, bool right, bool up, bool down)
    {
        utilization = std::clamp(utilization, 0.0f, 1.0f);
        if (utilization <= 0.01f)
            return;

        Color color{};
        if (utilization < 0.55f)
        {
            float t = utilization / 0.55f;
            color = Color{static_cast<unsigned char>(68.0f + t * 150.0f),
                          static_cast<unsigned char>(172.0f + t * 18.0f), 102, 255};
        }
        else
        {
            float t = (utilization - 0.55f) / 0.45f;
            color = Color{218, static_cast<unsigned char>(190.0f - t * 112.0f),
                          static_cast<unsigned char>(88.0f - t * 30.0f), 255};
        }
        const float top = RENDER_HEIGHT - position.y - TILE_SIZE;
        const Vector2 center{position.x + TILE_SIZE * 0.5f, top + TILE_SIZE * 0.5f};
        const float thickness = 10.0f + utilization * 8.0f;
        const float bloomScale = IsLocalLightBloomPreferenceEnabled() ? 1.25f : 1.0f;
        const Color outerGlow{color.r, color.g, color.b,
                              static_cast<unsigned char>(11.0f + utilization * 15.0f)};
        const Color innerGlow{color.r, color.g, color.b,
                              static_cast<unsigned char>(22.0f + utilization * 20.0f)};
        const Color fill{color.r, color.g, color.b,
                         static_cast<unsigned char>(58.0f + utilization * 42.0f)};
        BeginBlendMode(BLEND_ADDITIVE);
        const auto drawSegment = [&](Vector2 end)
        {
            DrawLineEx(center, end, thickness * 5.0f * bloomScale, outerGlow);
            DrawLineEx(center, end, thickness * 2.5f, innerGlow);
            DrawLineEx(center, end, thickness, fill);
        };

        if (left)  drawSegment({position.x, center.y});
        if (right) drawSegment({position.x + TILE_SIZE, center.y});
        if (up)    drawSegment({center.x, top + TILE_SIZE});
        if (down)  drawSegment({center.x, top});
        DrawCircleV(center, thickness * 2.50f * bloomScale, outerGlow);
        DrawCircleV(center, thickness * 1.25f, innerGlow);
        if (!left && !right && !up && !down)
            DrawCircleV(center, thickness * 0.55f, fill);
        else
            DrawCircleV(center, thickness * 0.50f, fill);
        EndBlendMode();
    }

    void DrawRoadSaturationIndicator(Vec2f position, bool saturated)
    {
        if (!saturated)
            return;
        const float top = RENDER_HEIGHT - position.y - TILE_SIZE;
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 6.0f);
        const Vector2 center{position.x + TILE_SIZE - 6.0f, top + 6.0f};
        DrawCircleV(center, 3.5f + pulse * 1.0f, Color{232, 91, 48, 255});
        DrawCircleV(center, 1.5f, Color{255, 222, 128, 255});
    }
}

bool IsFogOfWarPreferenceEnabled()
{
    return fogOfWarPreferenceEnabled;
}

void SetFogOfWarPreferenceEnabled(bool enabled)
{
    fogOfWarPreferenceEnabled = enabled;
}

bool IsColorGradingPreferenceEnabled()
{
    return colorGradingPreferenceEnabled;
}

void SetColorGradingPreferenceEnabled(bool enabled)
{
    colorGradingPreferenceEnabled = enabled;
}

bool IsRetroFilterPreferenceEnabled()
{
    return retroFilterPreferenceEnabled;
}

void SetRetroFilterPreferenceEnabled(bool enabled)
{
    retroFilterPreferenceEnabled = enabled;
}

bool IsLocalLightBloomPreferenceEnabled()
{
    return localLightBloomPreferenceEnabled;
}

void SetLocalLightBloomPreferenceEnabled(bool enabled)
{
    localLightBloomPreferenceEnabled = enabled;
}

bool IsRainOverlayPreferenceEnabled()
{
    return rainOverlayPreferenceEnabled;
}

void SetRainOverlayPreferenceEnabled(bool enabled)
{
    rainOverlayPreferenceEnabled = enabled;
}

bool IsLogisticsOverlayPreferenceEnabled()
{
    return logisticsOverlayPreferenceEnabled;
}

void SetLogisticsOverlayPreferenceEnabled(bool enabled)
{
    logisticsOverlayPreferenceEnabled = enabled;
}

void SetSceneTransitionOverlayAlpha(float alpha)
{
    sceneTransitionOverlayAlpha = std::clamp(alpha, 0.0f, 1.0f);
}

void DrawSceneTransitionOverlay()
{
    if (sceneTransitionOverlayAlpha <= 0.0f || !IsWindowReady())
        return;

    const unsigned char opacity = static_cast<unsigned char>(
        std::round(sceneTransitionOverlayAlpha * 255.0f));
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  Color{0, 0, 0, opacity});
}

int ResolveAnimationFrame(const AnimationClip& clip, float elapsedTime)
{
    if (clip.frameCount <= 1)
        return clip.startFrameId;  // Static: always the same frame.

    float totalDuration = clip.frameCount * clip.frameTime;
    float normalizedTime = clip.looping ? std::fmod(elapsedTime, totalDuration)
                                         : std::min(elapsedTime, totalDuration);

    int frameIndex = static_cast<int>(normalizedTime / clip.frameTime);
    frameIndex = std::clamp(frameIndex, 0, clip.frameCount - 1);
    return clip.startFrameId + frameIndex;
}

void CanvasLayer::Initialize(int width, int height)
{
    Shutdown();
    fbo = tvorin::ui::RenderTextureHandle{LoadRenderTexture(width, height)};
}

Rectangle RenderTargetSourceRect(Texture2D texture, RenderTargetDestination destination)
{
    const float sourceHeight = destination == RenderTargetDestination::OffscreenPass
        ? -static_cast<float>(texture.height)
        : static_cast<float>(texture.height);
    return {0.0f, 0.0f, static_cast<float>(texture.width),
            sourceHeight};
}

void CanvasLayer::Shutdown()
{
    if (IsWindowReady())
        fbo.Reset();
    else
        fbo.Forget();
}

void TextureAtlas::LoadTextureAtlas(const char* path, Vec2i tileSize)
{
    tex = tvorin::ui::TextureHandle{LoadTexture(path)};
    // World atlases contain neighbouring, unrelated cells. Point sampling
    // keeps their pixel-art edges discrete instead of blending from a
    // neighbour while the map is minified.
    if (tex.IsValid())
        SetTextureFilter(tex.Get(), TEXTURE_FILTER_POINT);
    size = tileSize;
    dim = {tex.Get().width / size.x, tex.Get().height / size.y};

    Log::Msg("[Texture Atlas]", "Loaded. Size: [", tex.Get().width, ", ", tex.Get().height, "] Dimensions: [", dim.x, ", ", dim.y, "]");
}

Rectangle TextureAtlas::GetRectFromId(int id)
{
    Rectangle rect;
    id = std::clamp(id, 0, std::max(0, dim.x * dim.y - 1));

    rect.height = size.y;
    rect.width = size.x;

    rect.x = (id % dim.x) * rect.width;
    rect.y = (id / dim.x) * rect.height;

    return rect;
}

void TextureAtlas::RegisterAnimation(int clipId, const AnimationClip& clip)
{
    animations[clipId] = clip;
}

AnimationClip TextureAtlas::GetAnimation(int clipId) const
{
    auto it = animations.find(clipId);
    return it != animations.end() ? it->second : AnimationClip{};
}

int TextureAtlas::GetFrameForAnimation(int clipId, float elapsedTime) const
{
    return ResolveAnimationFrame(GetAnimation(clipId), elapsedTime);
}

Renderer::Renderer()
{
    camera.offset = {0, 0};
    camera.target = {0 * TILE_SIZE, 0 * TILE_SIZE};
    camera.zoom = 1.25f;
    camera.rotation = 0.0f;
}

bool Renderer::InitializeWorldLayers()
{
    if (worldLayersInitialized)
        return true;

    if (!IsWindowReady())
    {
        Log::Msg("[Renderer]", "Cannot initialize world layers before InitWindow().");
        return false;
    }

    for (auto& layer : layers)
    {
        layer.Initialize();
        if (!layer.IsInitialized())
        {
            Log::Msg("[Renderer]", "Failed to allocate a world render layer.");
            Shutdown();
            return false;
        }
    }

    worldComposite.Initialize();
    if (!worldComposite.IsInitialized())
    {
        Log::Msg("[Renderer]", "Failed to allocate the world composite target.");
        Shutdown();
        return false;
    }

    litWorld.Initialize();
    if (!litWorld.IsInitialized())
    {
        Log::Msg("[Renderer] Failed to allocate the lit world target.");
        Shutdown();
        return false;
    }

    // Keep the light buffer at the same resolution as the world composite.
    // When this was half-resolution, the additive falloff was resampled at a
    // different pixel grid from the building sprite.  A light could therefore
    // look brighter or dimmer after a small pan/zoom even though neither its
    // world position nor intensity had changed.
    lightMap.Initialize(RENDER_WIDTH, RENDER_HEIGHT);
    if (!lightMap.IsInitialized())
    {
        Log::Msg("[Renderer] Failed to allocate the light map target.");
        Shutdown();
        return false;
    }
    SetTextureFilter(lightMap.fbo.Get().texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(lightMap.fbo.Get().texture, TEXTURE_WRAP_CLAMP);

    fogMask.Initialize(RENDER_WIDTH / 2, RENDER_HEIGHT / 2);
    if (!fogMask.IsInitialized())
    {
        Log::Msg("[Renderer] Failed to allocate the fog mask target.");
        Shutdown();
        return false;
    }
    SetTextureFilter(fogMask.fbo.Get().texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(fogMask.fbo.Get().texture, TEXTURE_WRAP_CLAMP);

    foggedWorld.Initialize();
    if (!foggedWorld.IsInitialized())
    {
        Log::Msg("[Renderer] Failed to allocate the fog compositing target.");
        Shutdown();
        return false;
    }

    postProcessedWorld.Initialize();
    if (!postProcessedWorld.IsInitialized())
    {
        Log::Msg("[Renderer] Failed to allocate the world postprocess target.");
        Shutdown();
        return false;
    }

    // Team colors remain optional until an individual material provides a
    // matching mask texture. A shader compile error must not prevent gameplay.
    shaderLibrary.LoadFragment(ShaderId::TeamColor, "assets/shaders/team_color.fs");
    shaderLibrary.LoadFragment(ShaderId::ResourceOverlay, "assets/shaders/resource_overlay.fs");
    shaderLibrary.LoadFragment(ShaderId::WorldLighting, "assets/shaders/world_lighting.fs");
    shaderLibrary.LoadFragment(ShaderId::RadialLight, "assets/shaders/radial_light.fs");
    shaderLibrary.LoadFragment(ShaderId::FogOfWar, "assets/shaders/fog_of_war.fs");
    shaderLibrary.LoadFragment(ShaderId::FogRoad, "assets/shaders/fog_road.fs");
    shaderLibrary.LoadFragment(ShaderId::WorldPostProcess, "assets/shaders/world_postprocess.fs");

    constexpr const char* RadialLightMaskPath = "assets/light/radial_light_mask_1024.png";
    if (FileExists(RadialLightMaskPath))
    {
        radialLightMask = tvorin::ui::TextureHandle{LoadTexture(RadialLightMaskPath)};
        if (!radialLightMask.IsValid())
            Log::Msg("[Renderer] Failed to load radial light mask: ", RadialLightMaskPath);
    }
    else
    {
        Log::Msg("[Renderer] Radial light mask not found: ", RadialLightMaskPath);
    }

    worldLayersInitialized = true;
    return true;
}

void Renderer::Shutdown()
{
    layerActive = false;

    shaderLibrary.Shutdown();

    for (auto& layer : layers)
        layer.Shutdown();
    worldComposite.Shutdown();
    litWorld.Shutdown();
    lightMap.Shutdown();
    fogMask.Shutdown();
    foggedWorld.Shutdown();
    postProcessedWorld.Shutdown();
    worldLayersInitialized = false;

    if (IsWindowReady())
    {
        buildingTextures.clear();
        atlasMap.clear();
        radialLightMask.Reset();
    }
    else
    {
        for (auto& [type, texture] : buildingTextures)
            texture.Forget();
        for (auto& [atlasId, atlas] : atlasMap)
            atlas.tex.Forget();
        radialLightMask.Forget();
    }

    buildingTextures.clear();
    buildingAnimations.clear();
    atlasMap.clear();
    dynamicLights.clear();
    fogReveals.clear();
    cachedSnapshotTick = std::numeric_limits<std::uint64_t>::max();
    cachedSnapshotCameraTarget = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    cachedSnapshotCameraZoom = -1.0f;
}

namespace
{
    float ScreenTopPaddingToRender(float screenPadding)
    {
        float scale = std::min(GetScreenWidth() / static_cast<float>(RENDER_WIDTH),
                               GetScreenHeight() / static_cast<float>(RENDER_HEIGHT));
        if (scale <= 0.0f)
            return 0.0f;

        float height = RENDER_HEIGHT * scale;
        float offsetY = (GetScreenHeight() - height) * 0.5f;
        return std::clamp((screenPadding - offsetY) / scale, 0.0f, static_cast<float>(RENDER_HEIGHT - 1));
    }
}

void Renderer::ClearDynamicLights()
{
    dynamicLights.clear();
}

void Renderer::ToggleNightPreview()
{
    nightPreviewEnabled = !nightPreviewEnabled;
    // A night preview without the day/night pass would not change the world,
    // so make the shortcut self-contained even after F6 previously disabled it.
    if (nightPreviewEnabled)
        renderSettings.dayNightCycle = true;
}

WorldLightingFrame Renderer::GetCurrentWorldLightingFrame() const
{
    if (!renderSettings.dayNightCycle)
        return WorldLightingFrame{};

    if (!nightPreviewEnabled)
        return ComputeWorldLighting(simulationTick, dayNightConfig);

    // Keep the visual frame exactly at the night keyframe while simulation
    // continues normally; no game time, save data, or network state changes.
    DayNightConfig previewConfig = dayNightConfig;
    previewConfig.startPhase = 0.0f;
    return ComputeWorldLighting(0, previewConfig);
}

void Renderer::ClearFogReveals()
{
    fogReveals.clear();
}

void Renderer::QueueFogReveal(const FogRevealView& reveal)
{
    if (reveal.radiusWorld <= 0.0f)
        return;

    // Cull by the whole influence circle, never by source position. A building
    // just outside the camera can still reveal a large portion of the screen.
    const Vec2f renderPosition = WorldToRender({reveal.worldPosition.x, reveal.worldPosition.y});
    // Keep a small guard for the shader's animated edge deformation.
    const float radiusRender = reveal.radiusWorld * std::max(0.0f, camera.zoom) * 1.05f;
    if (renderPosition.x + radiusRender < 0.0f ||
        renderPosition.x - radiusRender > static_cast<float>(RENDER_WIDTH) ||
        renderPosition.y + radiusRender < 0.0f ||
        renderPosition.y - radiusRender > static_cast<float>(RENDER_HEIGHT))
        return;

    fogReveals.push_back(reveal);
}

void Renderer::QueueBuildingFogReveal(BuildingType type, Vec2i footprint, Vec2f pos)
{
    const float radius = FogOfWar::BuildingRevealRadiusWorld(type, footprint);
    QueueFogReveal({{pos.x + footprint.x * TILE_SIZE * 0.50f,
                     pos.y + footprint.y * TILE_SIZE * 0.50f}, radius});
}

void Renderer::CycleDebugView()
{
    switch (renderSettings.debugView)
    {
        case RenderDebugView::Final:
            renderSettings.debugView = RenderDebugView::WorldAlbedo;
            break;
        case RenderDebugView::WorldAlbedo:
            renderSettings.debugView = RenderDebugView::LightMap;
            break;
        case RenderDebugView::LightMap:
            renderSettings.debugView = RenderDebugView::FogMask;
            break;
        case RenderDebugView::FogMask:
            renderSettings.debugView = RenderDebugView::Final;
            break;
    }
}

void Renderer::QueueDynamicLight(const LightEmitterView& light)
{
    constexpr size_t MaxDynamicLights = 1024;
    if (light.radiusWorld <= 0.0f || light.intensity <= 0.0f)
        return;

    if (dynamicLights.size() < MaxDynamicLights)
    {
        dynamicLights.push_back(light);
        return;
    }

    auto lowestPriority = std::min_element(dynamicLights.begin(), dynamicLights.end(),
                                           [](const LightEmitterView& lhs, const LightEmitterView& rhs) {
                                               return lhs.priority < rhs.priority;
                                           });
    if (lowestPriority != dynamicLights.end() && light.priority > lowestPriority->priority)
        *lowestPriority = light;
}

void Renderer::QueueBuildingLight(BuildingType type, Vec2i footprint, Vec2f pos, int stableId, bool isOperational)
{
    if (!isOperational || IsRoadLike(type))
        return;

    const Vector2 center{
        pos.x + static_cast<float>(footprint.x * TILE_SIZE) * 0.5f,
        pos.y + static_cast<float>(footprint.y * TILE_SIZE) * 0.5f};
    LightEmitterView light;
    switch (type)
    {
        case BuildingType::Foundry:
            // A tighter ember-red pool keeps the furnace readable without
            // turning its entire plot into a flat yellow wash.
            light = {center, Color{232, 92, 34, 255}, 232.0f, 0.56f, 0.70f, 0.0f, stableId, 30};
            break;
        case BuildingType::Smith:
            light = {center, Color{242, 126, 58, 255}, 208.0f, 0.58f, 0.68f, 0.0f, stableId, 20};
            break;
        case BuildingType::Inn:
            light = {center, Color{255, 178, 94, 255}, 192.0f, 0.50f, 0.65f, 0.0f, stableId, 10};
            break;
        default:
        {
            // Every finished building needs a small, steady night-time pool
            // of light so it remains legible. This is an environmental
            // fill/light from its occupied plot, not an emissive sample of
            // any coloured sprite pixel, so painted roofs and trims cannot
            // start appearing to blink.
            const float largestDimension = static_cast<float>(std::max(footprint.x, footprint.y));
            light = {center,
                     Color{255, 166, 84, 255}, 104.0f + largestDimension * 42.0f,
                     0.52f, 0.64f, 0.0f, stableId, 5};
            break;
        }
    }
    // Preserve a readable footprint at distant zoom and make every building
    // pool of light slightly more generous without changing gameplay space.
    light.radiusWorld *= 1.15f;
    light.minimumScreenRadius = 40.0f;
    QueueDynamicLight(light);
}

void Renderer::QueueResourceLight(int resourceOverlayTextureId, Vec2f pos, int stableId)
{
    const MineralOverlayStyle* style = GetMineralOverlayStyle(resourceOverlayTextureId);
    if (style == nullptr)
        return;

    LightEmitterView light{
        {pos.x + TILE_SIZE * 0.50f, pos.y + TILE_SIZE * 0.50f},
        style->glow,
        style->lightRadius,
        style->lightIntensity,
        0.72f,
        style->flickerAmount,
        stableId,
        -5,
        0.0f};
    // A deposit consists of many adjacent cells. A large minimum radius on
    // every cell makes their additive footprints overlap more as the camera
    // zooms out, eventually saturating the additive target and masking both the
    // material tint and day/night modulation. A tiny anti-aliasing floor is
    // enough; the field as a whole remains visible through its many cells.
    light.minimumScreenRadius = 4.0f;
    QueueDynamicLight(light);
}

void Renderer::DrawDynamicLightsToActiveTarget(const WorldLightingFrame& lighting,
                                               bool bloomEnabled)
{
    const Shader* radialLightShader = shaderLibrary.Find(ShaderId::RadialLight);
    if (radialLightShader == nullptr || !radialLightMask.IsValid() || dynamicLights.empty())
        return;

    // The destination FBO is already active. Use the identical Camera2D
    // matrix as world sprites and let the GPU clip emitters at target edges.
    // In particular, do not manually reject a source using GetWorldToScreen2D:
    // that duplicated the camera transform and made lights disappear while
    // panning even though their geometry still touched the target.
    const float safeCameraZoom = std::max(camera.zoom, 0.0001f);
    const int animationTimeLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "animationTime");
    const int stablePhaseLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "stablePhase");
    const int animationAmountLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "animationAmount");
    const int maskOnlyLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "maskOnly");
    const float animationTime = static_cast<float>(simulationTick) * 0.01f;
    const float stablePhase = 0.0f;
    const float animationAmount = 0.35f;
    const int maskOnly = 0;

    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(*radialLightShader);
    BeginMode2D(camera);
    if (animationTimeLocation >= 0)
        SetShaderValue(*radialLightShader, animationTimeLocation, &animationTime, SHADER_UNIFORM_FLOAT);
    if (stablePhaseLocation >= 0)
        SetShaderValue(*radialLightShader, stablePhaseLocation, &stablePhase, SHADER_UNIFORM_FLOAT);
    if (animationAmountLocation >= 0)
        SetShaderValue(*radialLightShader, animationAmountLocation, &animationAmount, SHADER_UNIFORM_FLOAT);
    if (maskOnlyLocation >= 0)
        SetShaderValue(*radialLightShader, maskOnlyLocation, &maskOnly, SHADER_UNIFORM_INT);

    // Per-emitter shader uniforms are unsafe here: raylib batches consecutive
    // quads using the same texture/shader, so changing a uniform before the
    // batch is flushed can apply the last visible mineral's color/intensity to
    // every preceding light. Encode additive RGB in each quad's vertex color
    // instead; it is stored per draw and remains stable across camera chunks.
    const auto drawPass = [&](float radiusScale, float intensityScale)
    {
        for (const LightEmitterView& light : dynamicLights)
        {
            const float screenRadius = ResolveScreenLightRadius(light, camera.zoom);
            const float radiusWorld = screenRadius / safeCameraZoom;
            const float flickerPhase = static_cast<float>(simulationTick % 100000) * 0.071f +
                                       static_cast<float>(light.stableId) * 0.618f;
            const float flicker = 1.0f + std::sin(flickerPhase) *
                                  std::clamp(light.flickerAmount, 0.0f, 1.0f);
            const float visibility = std::max(
                lighting.localLightVisibility,
                std::clamp(light.minimumVisibility, 0.0f, 1.0f));
            const float intensity = std::max(0.0f,
                light.intensity * visibility * flicker * intensityScale);
            const Color encodedLight = EncodeAdditiveLightTint(light.color, intensity);
            const float drawRadius = radiusWorld * radiusScale;
            Rectangle source{0.0f, 0.0f, static_cast<float>(radialLightMask.Get().width),
                             -static_cast<float>(radialLightMask.Get().height)};
            Rectangle destination{light.worldPosition.x - drawRadius,
                                  RENDER_HEIGHT - light.worldPosition.y - drawRadius,
                                   drawRadius * 2.0f,
                                   drawRadius * 2.0f};
            DrawTexturePro(radialLightMask.Get(), source, destination,
                           {0.0f, 0.0f}, 0.0f, encodedLight);
        }
    };

    if (bloomEnabled)
        drawPass(1.15f, 0.045f);
    drawPass(1.0f, 1.0f);

    EndMode2D();
    EndShaderMode();
    EndBlendMode();
}

void Renderer::DrawLightMap(const WorldLightingFrame& lighting, bool localLightsEnabled)
{
    if (!lightMap.IsInitialized())
        return;

    BeginTextureMode(lightMap.fbo.Get());
    ClearBackground(BLACK);
    if (localLightsEnabled)
        DrawDynamicLightsToActiveTarget(lighting, false);
    EndTextureMode();
}

void Renderer::DrawFogMask()
{
    if (!fogMask.IsInitialized())
        return;

    BeginTextureMode(fogMask.fbo.Get());
    ClearBackground(BLACK);

    const float mapScale = fogMask.fbo.Get().texture.width / static_cast<float>(RENDER_WIDTH);
    // fogMask is half-resolution, so use the same camera matrix scaled into
    // its target. The world and its visibility field now share one transform.
    Camera2D fogCamera = camera;
    fogCamera.zoom *= mapScale;
    const Shader* radialMaskShader = shaderLibrary.Find(ShaderId::RadialLight);
    int animationTimeLocation = -1;
    int animationAmountLocation = -1;
    int maskOnlyLocation = -1;
    float animationTime = static_cast<float>(simulationTick) * 0.01f;
    const float animationAmount = 0.85f;
    const int maskOnly = 1;
    if (radialMaskShader != nullptr && radialLightMask.IsValid())
    {
        animationTimeLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "animationTime");
        animationAmountLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "animationAmount");
        maskOnlyLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "maskOnly");
        BeginShaderMode(*radialMaskShader);
        if (animationTimeLocation >= 0)
            SetShaderValue(*radialMaskShader, animationTimeLocation, &animationTime, SHADER_UNIFORM_FLOAT);
        if (animationAmountLocation >= 0)
            SetShaderValue(*radialMaskShader, animationAmountLocation, &animationAmount, SHADER_UNIFORM_FLOAT);
        if (maskOnlyLocation >= 0)
            SetShaderValue(*radialMaskShader, maskOnlyLocation, &maskOnly, SHADER_UNIFORM_INT);
    }

    BeginMode2D(fogCamera);

    for (const FogRevealView& reveal : fogReveals)
    {
        const float radius = reveal.radiusWorld;
        const float centerX = reveal.worldPosition.x;
        const float centerY = RENDER_HEIGHT - reveal.worldPosition.y;

        if (radialLightMask.IsValid())
        {
            // The supplied alpha-gradient texture gives fog revealers a
            // natural falloff instead of the old concentric primitive discs.
            Rectangle source{0.0f, 0.0f, static_cast<float>(radialLightMask.Get().width),
                             -static_cast<float>(radialLightMask.Get().height)};
            Rectangle destination{centerX - radius, centerY - radius, radius * 2.0f, radius * 2.0f};
            DrawTexturePro(radialLightMask.Get(), source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
        }
        else
        {
            // The mask is optional at runtime; retain a readable fallback.
            DrawCircle(static_cast<int>(centerX), static_cast<int>(centerY), radius, WHITE);
        }
    }
    EndMode2D();
    if (radialMaskShader != nullptr && radialLightMask.IsValid())
        EndShaderMode();
    EndTextureMode();
}

// Draws all cached world layers and UI widgets to the window, then presents.
void Renderer::Draw(std::vector<UiWidget*> ui, double dt)
{
    DrawContent(std::move(ui), dt);
    PresentFrame();
}

// Issues all draw calls but does not present. See header for the locking rationale.
void Renderer::DrawContent(std::vector<UiWidget*> ui, double dt)
{
    BeginDrawing();
    ClearBackground(BLACK);

    float scale = std::min(GetScreenWidth() / static_cast<float>(RENDER_WIDTH),
                           GetScreenHeight() / static_cast<float>(RENDER_HEIGHT));
    float width = RENDER_WIDTH * scale;
    float height = RENDER_HEIGHT * scale;
    Vector2 offset{
        (GetScreenWidth() - width) * 0.5f,
        (GetScreenHeight() - height) * 0.5f};

    Rectangle dest{offset.x, offset.y, width, height};

    if (worldLayersInitialized)
    {
        // OptionsScene can change this while a GameScene remains constructed.
        // Synchronizing at draw time makes fog enable/disable immediately
        // reflect the current world-derived revealers.
        renderSettings.fogOfWar = IsFogOfWarPreferenceEnabled();
        renderSettings.colorGrading = IsColorGradingPreferenceEnabled();
        renderSettings.retroFilter = IsRetroFilterPreferenceEnabled();
        renderSettings.localLightBloom = IsLocalLightBloomPreferenceEnabled();
        renderSettings.rainOverlay = IsRainOverlayPreferenceEnabled();
        renderSettings.logisticsOverlay = IsLogisticsOverlayPreferenceEnabled();
        BeginTextureMode(worldComposite.fbo.Get());
        ClearBackground(BLANK);

        // Every render target uses canonical top-left screen space. The helper
        // below is the only code allowed to apply raylib's FBO Y correction.
        Rectangle layerDestination{0.0f, 0.0f, static_cast<float>(RENDER_WIDTH), static_cast<float>(RENDER_HEIGHT)};
        // Layer FBOs are cleared to transparent and populated with regular
        // alpha blending. Their stored RGB is therefore premultiplied by
        // alpha. Composing them a second time with BLEND_ALPHA would multiply
        // anti-aliased edges again, creating dark contours around minerals
        // and sprites. Consume RGB as premultiplied but retain conventional
        // source-over alpha so later world passes receive a valid composite.
        rlSetBlendFactorsSeparate(RL_ONE, RL_ONE_MINUS_SRC_ALPHA,
                                  RL_ONE, RL_ONE_MINUS_SRC_ALPHA,
                                  RL_FUNC_ADD, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM_SEPARATE);
        for (std::size_t index = 0; index < layers.size(); ++index)
            DrawRenderTarget(layers[index].fbo.Get().texture, layerDestination,
                             RenderTargetDestination::OffscreenPass);
        EndBlendMode();
        EndTextureMode();

        Texture2D presentedWorld = worldComposite.fbo.Get().texture;
        const Shader* lightingShader = shaderLibrary.Find(ShaderId::WorldLighting);
        const bool hasLightingPass = (renderSettings.dayNightCycle || renderSettings.dynamicLights) &&
                                     lightingShader != nullptr;
        if (hasLightingPass)
        {
            WorldLightingFrame lighting = GetCurrentWorldLightingFrame();
            if (!renderSettings.dayNightCycle)
            {
                lighting.ambientColor = {1.0f, 1.0f, 1.0f};
                lighting.ambientIntensity = 1.0f;
                lighting.exposure = 1.0f;
                lighting.saturation = 1.0f;
                lighting.contrast = 1.0f;
                lighting.localLightVisibility = 1.0f;
            }
            if (!renderSettings.dynamicLights)
                lighting.localLightVisibility = 0.0f;
            // The light map is diagnostic only. The final scene receives the
            // same emitter path directly below, so there is no second sampled
            // framebuffer whose registration could drift with the camera.
            if (renderSettings.debugView == RenderDebugView::LightMap)
                DrawLightMap(lighting, renderSettings.dynamicLights);
            BeginTextureMode(litWorld.fbo.Get());
            ClearBackground(BLANK);
            BeginShaderMode(*lightingShader);

            int ambientColorLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "ambientColor");
            int ambientIntensityLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "ambientIntensity");
            int exposureLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "exposure");
            int saturationLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "saturation");
            int contrastLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "contrast");
            if (ambientColorLocation >= 0)
                SetShaderValue(*lightingShader, ambientColorLocation, &lighting.ambientColor, SHADER_UNIFORM_VEC3);
            if (ambientIntensityLocation >= 0)
                SetShaderValue(*lightingShader, ambientIntensityLocation, &lighting.ambientIntensity, SHADER_UNIFORM_FLOAT);
            if (exposureLocation >= 0)
                SetShaderValue(*lightingShader, exposureLocation, &lighting.exposure, SHADER_UNIFORM_FLOAT);
            if (saturationLocation >= 0)
                SetShaderValue(*lightingShader, saturationLocation, &lighting.saturation, SHADER_UNIFORM_FLOAT);
            if (contrastLocation >= 0)
                SetShaderValue(*lightingShader, contrastLocation, &lighting.contrast, SHADER_UNIFORM_FLOAT);

            DrawRenderTarget(worldComposite.fbo.Get().texture, layerDestination,
                             RenderTargetDestination::OffscreenPass);
            EndShaderMode();
            if (renderSettings.dynamicLights)
                DrawDynamicLightsToActiveTarget(lighting, renderSettings.localLightBloom);
            EndTextureMode();
            presentedWorld = litWorld.fbo.Get().texture;
        }

        const Shader* postProcessShader =
            (renderSettings.colorGrading || renderSettings.retroFilter || renderSettings.rainOverlay)
                ? shaderLibrary.Find(ShaderId::WorldPostProcess)
                : nullptr;
        if (postProcessShader != nullptr)
        {
            BeginTextureMode(postProcessedWorld.fbo.Get());
            ClearBackground(BLANK);
            BeginShaderMode(*postProcessShader);

            const int colorGradingEnabled = renderSettings.colorGrading ? 1 : 0;
            const int retroFilterEnabled = renderSettings.retroFilter ? 1 : 0;
            const int rainOverlayEnabled = renderSettings.rainOverlay ? 1 : 0;
            const float animationTime = static_cast<float>(simulationTick) * 0.01f;
            const Vector2 resolution{static_cast<float>(RENDER_WIDTH),
                                     static_cast<float>(RENDER_HEIGHT)};
            int colorGradingLocation = shaderLibrary.GetLocation(ShaderId::WorldPostProcess, "colorGradingEnabled");
            int retroFilterLocation = shaderLibrary.GetLocation(ShaderId::WorldPostProcess, "retroFilterEnabled");
            int rainOverlayLocation = shaderLibrary.GetLocation(ShaderId::WorldPostProcess, "rainOverlayEnabled");
            int animationTimeLocation = shaderLibrary.GetLocation(ShaderId::WorldPostProcess, "animationTime");
            int resolutionLocation = shaderLibrary.GetLocation(ShaderId::WorldPostProcess, "resolution");
            if (colorGradingLocation >= 0)
                SetShaderValue(*postProcessShader, colorGradingLocation,
                               &colorGradingEnabled, SHADER_UNIFORM_INT);
            if (retroFilterLocation >= 0)
                SetShaderValue(*postProcessShader, retroFilterLocation,
                               &retroFilterEnabled, SHADER_UNIFORM_INT);
            if (rainOverlayLocation >= 0)
                SetShaderValue(*postProcessShader, rainOverlayLocation,
                               &rainOverlayEnabled, SHADER_UNIFORM_INT);
            if (animationTimeLocation >= 0)
                SetShaderValue(*postProcessShader, animationTimeLocation,
                               &animationTime, SHADER_UNIFORM_FLOAT);
            if (resolutionLocation >= 0)
                SetShaderValue(*postProcessShader, resolutionLocation,
                               &resolution, SHADER_UNIFORM_VEC2);

            DrawRenderTarget(presentedWorld, layerDestination,
                             RenderTargetDestination::OffscreenPass);
            EndShaderMode();
            EndTextureMode();
            presentedWorld = postProcessedWorld.fbo.Get().texture;
        }

        // Fog is the highest world layer. It is not a color-transform pass and
        // never samples the lit/postprocessed scene. First preserve the
        // finished world, then alpha-blend an independently generated dark
        // overlay from the visibility mask. UI is rendered later, above both.
        const Shader* fogShader = renderSettings.fogOfWar
            ? shaderLibrary.Find(ShaderId::FogOfWar)
            : nullptr;
        if (fogShader != nullptr)
        {
            DrawFogMask();
            BeginTextureMode(foggedWorld.fbo.Get());
            ClearBackground(BLANK);
            DrawRenderTarget(presentedWorld, layerDestination,
                             RenderTargetDestination::OffscreenPass);
            BeginShaderMode(*fogShader);
            DrawRenderTarget(fogMask.fbo.Get().texture, layerDestination,
                             RenderTargetDestination::OffscreenPass);
            EndShaderMode();
            EndTextureMode();
            presentedWorld = foggedWorld.fbo.Get().texture;
        }

        const char* debugLabel = nullptr;
        switch (renderSettings.debugView)
        {
            case RenderDebugView::Final:
                break;
            case RenderDebugView::WorldAlbedo:
                presentedWorld = worldComposite.fbo.Get().texture;
                debugLabel = "DEBUG: world albedo (F8: next view)";
                break;
            case RenderDebugView::LightMap:
                presentedWorld = lightMap.fbo.Get().texture;
                debugLabel = "DEBUG: light map (F8: next view)";
                break;
            case RenderDebugView::FogMask:
                presentedWorld = fogMask.fbo.Get().texture;
                debugLabel = "DEBUG: fog visibility mask (F8: next view)";
                break;
        }

        // The offscreen chain is already in the renderer's established final
        // orientation here. Applying the FBO-to-FBO correction once more on
        // the backbuffer flips the complete world vertically.
        DrawRenderTarget(presentedWorld, dest, RenderTargetDestination::Window);
        if (debugLabel != nullptr)
            UiText::Draw(debugLabel, offset.x + 18.0f, offset.y + 18.0f,
                         22, Color{255, 236, 152, 255});
    }

    for(auto ptr : ui)
    {
        ptr->Update(dt);
    }
}

// Presents the frame. EndDrawing performs the vsync / frame-cap wait, so any
// world lock the caller held for drawing must already be released here.
void Renderer::PresentFrame()
{
    DrawSceneTransitionOverlay();
    EndDrawing();
}

// Draws a standalone texture onto one render layer.
void Renderer::DrawOnLayer(WorldRenderLayer layer, Texture2D tex, Vec2i pos)
{
    if (!worldLayersInitialized)
        return;

    BeginLayer(layer);

    Rectangle src = {0,0,tex.width*1.0f, -tex.height*1.0f};
    Rectangle dest = {static_cast<float>(pos.x), static_cast<float>(RENDER_HEIGHT - tex.height - pos.y), tex.width*1.0f, tex.height*1.0f};
    DrawTexturePro(tex, src, dest, {0,0}, 0, WHITE);

    EndLayer();
}

// Draws one atlas tile directly onto a render layer.
void Renderer::DrawOnLayer(WorldRenderLayer layer, int atlas, int tex, Vec2f pos)
{
    if (!worldLayersInitialized)
        return;

    BeginLayer(layer);
    DrawAtlasTile(atlas, tex, pos);
    EndLayer();
}

// Initializes Renderer::BeginLayer.
void Renderer::BeginLayer(WorldRenderLayer layer)
{
    if (!worldLayersInitialized || layerActive)
        return;

    BeginTextureMode(layers[ToLayerIndex(layer)].fbo.Get());
    // Cache straight-alpha artwork as premultiplied RGB plus *linear* alpha.
    // Raylib's default glBlendFunc applies SRC_ALPHA to the alpha channel as
    // well, which stores A*A in a transparent FBO. That squared coverage was
    // then applied again during world composition and produced dark fringes.
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA,
                              RL_ONE, RL_ONE_MINUS_SRC_ALPHA,
                              RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    BeginMode2D(camera);
    layerActive = true;
}

// Initializes Renderer::EndLayer.
void Renderer::EndLayer()
{
    if (!layerActive)
        return;

    EndMode2D();
    EndBlendMode();
    EndTextureMode();
    layerActive = false;
}

// Clears this runtime state.
void Renderer::ClearLayer(WorldRenderLayer layer)
{
    if (!worldLayersInitialized)
        return;

    BeginTextureMode(layers[ToLayerIndex(layer)].fbo.Get());
    ClearBackground(BLANK);
    EndTextureMode();
}

// Draws one atlas tile at its native tile size.
void Renderer::DrawAtlasTile(int atlas, int tex, Vec2f pos)
{
    auto& at = atlasMap[atlas];
    DrawAtlasTile(atlas, tex, pos, {static_cast<float>(at.size.x), static_cast<float>(at.size.y)});
}

// Draws one atlas tile stretched to a target world size.
void Renderer::DrawAtlasTile(int atlas, int tex, Vec2f pos, Vec2f drawSize)
{
    auto& at = atlasMap[atlas];
    if (tex < 0 || tex >= at.dim.x * at.dim.y)
        tex = std::max(0, at.dim.x * at.dim.y - 1);

    Rectangle src = at.GetRectFromId(tex);
    src.height *= -1.0;

    Rectangle dest = {pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
    if (atlas == 0 && camera.zoom > 0.0f)
    {
        // Terrain uses shared render-pixel boundaries; resource overlays keep
        // their transparent atlas edges and therefore use the original quad.
        const RenderPixelBounds bounds = CalculateTerrainRenderPixelBounds(
            pos, drawSize, camera.zoom, static_cast<float>(RENDER_HEIGHT));
        const float worldPerRenderPixel = 1.0f / camera.zoom;
        dest = {bounds.left * worldPerRenderPixel,
                bounds.top * worldPerRenderPixel,
                (bounds.right - bounds.left) * worldPerRenderPixel,
                (bounds.bottom - bounds.top) * worldPerRenderPixel};
    }
    DrawTexturePro(at.tex.Get(), src, dest, {0,0}, 0, WHITE);
}

void Renderer::DrawResourceOverlay(int resourceOverlayTextureId, Vec2f pos)
{
    constexpr int ResourceOverlayAtlasId = 41;
    const MineralOverlayStyle* style = GetMineralOverlayStyle(resourceOverlayTextureId);
    const Shader* shader = shaderLibrary.Find(ShaderId::ResourceOverlay);
    auto atlasIt = atlasMap.find(ResourceOverlayAtlasId);
    if (style == nullptr || shader == nullptr || atlasIt == atlasMap.end() || !atlasIt->second.tex.IsValid())
    {
        DrawAtlasTile(ResourceOverlayAtlasId, resourceOverlayTextureId, pos);
        return;
    }

    TextureAtlas& atlas = atlasIt->second;
    if (resourceOverlayTextureId < 0 || resourceOverlayTextureId >= atlas.dim.x * atlas.dim.y)
        return;

    Rectangle source = atlas.GetRectFromId(resourceOverlayTextureId);
    const float textureWidth = static_cast<float>(atlas.tex.Get().width);
    const float textureHeight = static_cast<float>(atlas.tex.Get().height);
    float atlasTexelSize[2]{1.0f / textureWidth, 1.0f / textureHeight};
    // Clamp neighbourhood samples to this 64 px atlas cell so the coal edge
    // detector never reads alpha from a neighbouring resource variant.
    float sourceUvMin[2]{
        (source.x + 0.5f) / textureWidth,
        (source.y + 0.5f) / textureHeight};
    float sourceUvMax[2]{
        (source.x + source.width - 0.5f) / textureWidth,
        (source.y + source.height - 0.5f) / textureHeight};
    const auto toRgb = [](Color color, float (&out)[3])
    {
        out[0] = color.r / 255.0f;
        out[1] = color.g / 255.0f;
        out[2] = color.b / 255.0f;
    };
    float shadow[3];
    float base[3];
    float highlight[3];
    toRgb(style->shadow, shadow);
    toRgb(style->base, base);
    toRgb(style->highlight, highlight);

    const auto setVec2 = [&](const char* name, const float* value)
    {
        int location = shaderLibrary.GetLocation(ShaderId::ResourceOverlay, name);
        if (location >= 0) SetShaderValue(*shader, location, value, SHADER_UNIFORM_VEC2);
    };
    const auto setVec3 = [&](const char* name, const float* value)
    {
        int location = shaderLibrary.GetLocation(ShaderId::ResourceOverlay, name);
        if (location >= 0) SetShaderValue(*shader, location, value, SHADER_UNIFORM_VEC3);
    };

    BeginShaderMode(*shader);
    setVec2("atlasTexelSize", atlasTexelSize);
    setVec2("sourceUvMin", sourceUvMin);
    setVec2("sourceUvMax", sourceUvMax);
    setVec3("shadowColor", shadow);
    setVec3("baseColor", base);
    setVec3("highlightColor", highlight);
    int luminanceScaleLocation = shaderLibrary.GetLocation(ShaderId::ResourceOverlay, "luminanceScale");
    int luminanceBiasLocation = shaderLibrary.GetLocation(ShaderId::ResourceOverlay, "luminanceBias");
    int edgeHighlightStrengthLocation = shaderLibrary.GetLocation(ShaderId::ResourceOverlay, "edgeHighlightStrength");
    if (luminanceScaleLocation >= 0)
        SetShaderValue(*shader, luminanceScaleLocation, &style->luminanceScale, SHADER_UNIFORM_FLOAT);
    if (luminanceBiasLocation >= 0)
        SetShaderValue(*shader, luminanceBiasLocation, &style->luminanceBias, SHADER_UNIFORM_FLOAT);
    if (edgeHighlightStrengthLocation >= 0)
        SetShaderValue(*shader, edgeHighlightStrengthLocation,
                       &style->edgeHighlightStrength, SHADER_UNIFORM_FLOAT);

    source.height *= -1.0f;
    Rectangle destination{pos.x, RENDER_HEIGHT - TILE_SIZE - pos.y,
                          static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)};
    DrawTexturePro(atlas.tex.Get(), source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
    EndShaderMode();
}

void Renderer::DrawResourceGroundGlow(int resourceOverlayTextureId, Vec2f pos)
{
    const MineralOverlayStyle* style = GetMineralOverlayStyle(resourceOverlayTextureId);
    if (style == nullptr)
        return;

    // All of these glows are rendered before the resource sprites. Their
    // radii may therefore overlap into one field-shaped aura without later
    // tiles washing colour over rock silhouettes that were already drawn.
    Color inner = style->glow;
    // This halo is composited additively by both live-world and snapshot
    // render paths. Keep each tile subtle because neighbouring deposit cells
    // overlap; the old alpha-blend opacity would overexpose dense fields when
    // summed instead of mixed.
    inner.a = static_cast<unsigned char>(std::clamp(
        8.0f + style->glowStrength * 11.0f, 11.0f, 17.0f));
    Color outer = inner;
    outer.a = 0;
    DrawCircleGradient(
        static_cast<int>(std::round(pos.x + TILE_SIZE * 0.5f)),
        static_cast<int>(std::round(RENDER_HEIGHT - pos.y - TILE_SIZE * 0.5f)),
        TILE_SIZE * 1.08f,
        inner,
        outer);
}

void Renderer::DrawShipments(const std::vector<ShipmentRenderState>& shipments, Vec2i mapSize)
{
    constexpr int ResourceAtlasId = 1;
    constexpr float IconSize = 25.0f;
    constexpr float RightHandLaneOffset = 9.0f;
    constexpr float LateralJitter = 2.0f;
    constexpr float LongitudinalJitter = 4.0f;
    constexpr float WaitingProgress = 0.86f;

    const auto atlasIt = atlasMap.find(ResourceAtlasId);
    if (!layerActive || mapSize.x <= 0 || mapSize.y <= 0 ||
        atlasIt == atlasMap.end() || !atlasIt->second.tex.IsValid())
    {
        return;
    }

    const int tileCount = mapSize.x * mapSize.y;
    for (const ShipmentRenderState& shipment : shipments)
    {
        if (shipment.resourceType == ResourceType::Null ||
            shipment.fromTileId < 0 || shipment.toTileId < 0 ||
            shipment.fromTileId >= tileCount || shipment.toTileId >= tileCount)
        {
            continue;
        }

        const auto tileCenter = [mapSize](int tileId)
        {
            return Vec2f{
                static_cast<float>((tileId % mapSize.x) * TILE_SIZE) + TILE_SIZE * 0.5f,
                static_cast<float>((tileId / mapSize.x) * TILE_SIZE) + TILE_SIZE * 0.5f};
        };

        const Vec2f current = tileCenter(shipment.fromTileId);
        const Vec2f next = tileCenter(shipment.toTileId);
        const bool hasPrevious = shipment.previousTileId >= 0 && shipment.previousTileId < tileCount;
        const Vec2f previous = hasPrevious ? tileCenter(shipment.previousTileId) : current;

        // A shipment carried by a road tile enters at the midpoint of the
        // previous/current pair and exits at the midpoint of current/next.
        // This keeps the sprite inside the road tile instead of shifting it
        // half a tile ahead. At the source it waits on the source/road edge;
        // the first road traversal begins from exactly the same point.
        const Vec2f entry = hasPrevious
            ? Vec2f{(previous.x + current.x) * 0.5f, (previous.y + current.y) * 0.5f}
            : Vec2f{(current.x + next.x) * 0.5f, (current.y + next.y) * 0.5f};
        const Vec2f exit{(current.x + next.x) * 0.5f, (current.y + next.y) * 0.5f};

        float progress = std::clamp(shipment.progress, 0.0f, 1.0f);
        if (shipment.waitingForCapacity)
            progress = std::min(progress, WaitingProgress);

        const auto normalized = [](Vec2f direction)
        {
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            return length > 0.001f
                ? Vec2f{direction.x / length, direction.y / length}
                : Vec2f{};
        };
        const auto rightNormal = [](Vec2f direction)
        {
            // World Y grows upwards, so this is a clockwise rotation.
            return Vec2f{direction.y, -direction.x};
        };

        const Vec2f entryDirection = normalized({current.x - entry.x, current.y - entry.y});
        const Vec2f exitDirection = normalized({exit.x - current.x, exit.y - current.y});

        const std::uint64_t visualHash = shipment.shipmentId * 11400714819323198485ull ^
                                         static_cast<std::uint64_t>(shipment.ownerPlayerId + 1) * 1099511628211ull;
        const float laneOffset = RightHandLaneOffset +
            ((visualHash & 1ull) == 0ull ? -LateralJitter : LateralJitter);
        const int longitudinalSlot = static_cast<int>((visualHash >> 1u) % 3ull) - 1;

        const Vec2f entryRight = rightNormal(entryDirection);
        const Vec2f exitRight = rightNormal(exitDirection);
        const Vec2f entryLane{entry.x + entryRight.x * laneOffset,
                              entry.y + entryRight.y * laneOffset};
        const Vec2f exitLane{exit.x + exitRight.x * laneOffset,
                             exit.y + exitRight.y * laneOffset};
        const float directionDot = entryDirection.x * exitDirection.x + entryDirection.y * exitDirection.y;
        const Vec2f laneCorner = directionDot > 0.5f
            ? Vec2f{current.x + entryRight.x * laneOffset,
                    current.y + entryRight.y * laneOffset}
            : Vec2f{current.x + (entryRight.x + exitRight.x) * laneOffset,
                    current.y + (entryRight.y + exitRight.y) * laneOffset};

        Vec2f center{};
        Vec2f direction{};
        if (!hasPrevious)
        {
            direction = normalized({next.x - current.x, next.y - current.y});
            const Vec2f right = rightNormal(direction);
            center = {entry.x + right.x * laneOffset, entry.y + right.y * laneOffset};
        }
        else if (progress < 0.5f)
        {
            const float localProgress = progress * 2.0f;
            center = {entryLane.x + (laneCorner.x - entryLane.x) * localProgress,
                      entryLane.y + (laneCorner.y - entryLane.y) * localProgress};
            direction = normalized({laneCorner.x - entryLane.x, laneCorner.y - entryLane.y});
        }
        else
        {
            const float localProgress = (progress - 0.5f) * 2.0f;
            center = {laneCorner.x + (exitLane.x - laneCorner.x) * localProgress,
                      laneCorner.y + (exitLane.y - laneCorner.y) * localProgress};
            direction = normalized({exitLane.x - laneCorner.x, exitLane.y - laneCorner.y});
        }

        center.x += direction.x * longitudinalSlot * LongitudinalJitter;
        center.y += direction.y * longitudinalSlot * LongitudinalJitter;

        const Vec2f iconPosition{center.x - IconSize * 0.5f, center.y - IconSize * 0.5f};

        // Shadow, warm outline and dark inset give neighbouring cargo icons a
        // crisp silhouette even when several units leave in the same batch.
        const Vector2 screenCenter{center.x, RENDER_HEIGHT - center.y};
        DrawCircleV({screenCenter.x + 1.5f, screenCenter.y + 2.5f}, IconSize * 0.63f,
                    Color{0, 0, 0, 145});
        DrawCircleV(screenCenter, IconSize * 0.60f, Color{224, 202, 151, 235});
        DrawCircleV(screenCenter, IconSize * 0.51f, Color{18, 20, 24, 235});
        DrawAtlasTile(ResourceAtlasId, static_cast<int>(shipment.resourceType), iconPosition,
                      {IconSize, IconSize});
    }
}

// Draws one atlas tile, resolving the frame from an animation clip and elapsed time.
void Renderer::DrawAtlasTile(int atlas, int clipId, Vec2f pos, float elapsedTime)
{
    auto& at = atlasMap[atlas];
    DrawAtlasTile(atlas, at.GetFrameForAnimation(clipId, elapsedTime), pos);
}

// Same, stretched to a target world size.
void Renderer::DrawAtlasTile(int atlas, int clipId, Vec2f pos, Vec2f drawSize, float elapsedTime)
{
    auto& at = atlasMap[atlas];
    DrawAtlasTile(atlas, at.GetFrameForAnimation(clipId, elapsedTime), pos, drawSize);
}

// Loads the requested data into runtime state.
void Renderer::LoadBuildingTexture(BuildingType type, const std::string& path)
{
    if (!FileExists(path.c_str()))
        return;

    tvorin::ui::TextureHandle texture{LoadTexture(path.c_str())};
    if (texture.IsValid())
    {
        // Building sprites are sampled directly by the team-colour shader.
        // Point filtering is required for pixel art and prevents subpixel
        // camera movement from blending neighbouring pixels or animation
        // frames into a shimmering/floating silhouette.
        SetTextureFilter(texture.Get(), TEXTURE_FILTER_POINT);
        buildingTextures[type] = std::move(texture);
    }
}

Rectangle Renderer::GetBuildingTextureFirstFrameSource(BuildingType type) const
{
    if (IsRoadLike(type))
    {
        auto atlasIt = atlasMap.find(19);
        if (atlasIt != atlasMap.end())
        {
            const TextureAtlas& atlas = atlasIt->second;
            const int tileId = type == BuildingType::Bridge ? 16 : 0;
            const int clampedId = std::clamp(tileId, 0, std::max(0, atlas.dim.x * atlas.dim.y - 1));
            return {static_cast<float>((clampedId % atlas.dim.x) * atlas.size.x),
                    static_cast<float>((clampedId / atlas.dim.x) * atlas.size.y),
                    static_cast<float>(atlas.size.x), static_cast<float>(atlas.size.y)};
        }
    }

    auto textureIt = buildingTextures.find(type);
    if (textureIt == buildingTextures.end() || !textureIt->second.IsValid())
        return {};

    const Texture2D& texture = textureIt->second.Get();
    float frameWidth = static_cast<float>(texture.width);
    float frameHeight = static_cast<float>(texture.height);
    auto animationIt = buildingAnimations.find(type);
    if (animationIt != buildingAnimations.end() && animationIt->second.frameCount > 1)
    {
        frameWidth = texture.width / static_cast<float>(animationIt->second.frameCount);
    }
    return {0.0f, 0.0f, frameWidth, frameHeight};
}

void Renderer::RegisterBuildingAnimation(BuildingType type, const AnimationClip& clip)
{
    buildingAnimations[type] = clip;
}

bool Renderer::HasBuildingAnimation(BuildingType type) const
{
    // The Bakery is redrawn for its procedural chimney smoke even though its
    // base sprite is static; this prevents roof or wall flicker.
    if (type == BuildingType::Bakery)
        return true;

    const auto it = buildingAnimations.find(type);
    return it != buildingAnimations.end() && it->second.frameCount > 1;
}

// Draws a building texture sized to its footprint, animated by its lifetime
// (ETAP 5.4) if a clip is registered for its type — otherwise identical to
// the static overload.
void Renderer::DrawBuildingTexture(Building* building, Vec2f pos, Color tint)
{
    if (building == nullptr)
        return;

    Color ownerColor = building->owner != nullptr ? building->owner->color : WHITE;
    DrawBuildingTexture(building->buildingType, building->GetFootprint(), pos, tint,
                         static_cast<float>(building->GetLifetime()), ownerColor, building->owner != nullptr);
}

void Renderer::DrawBuildingSprite(Texture2D texture, Rectangle source, Rectangle destination, Color tint,
                                  Color ownerColor, bool applyTeamColor, BuildingType type)
{
    const Shader* teamColorShader = (applyTeamColor && renderSettings.teamColors)
        ? shaderLibrary.Find(ShaderId::TeamColor)
        : nullptr;
    if (teamColorShader == nullptr)
    {
        DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, tint);
        return;
    }

    float primary[4]{ownerColor.r / 255.0f, ownerColor.g / 255.0f, ownerColor.b / 255.0f, ownerColor.a / 255.0f};
    float secondary[4]{
        primary[0] + (1.0f - primary[0]) * 0.25f,
        primary[1] + (1.0f - primary[1]) * 0.25f,
        primary[2] + (1.0f - primary[2]) * 0.25f,
        primary[3]};
    int hasMaterialMask = 0;
    int useAlbedoBlueKey = 1;
    // The refreshed HQ uses a darker, more cyan-leaning blue than the legacy
    // building sprites. Keep its broader fallback isolated until authored
    // material masks replace the temporary albedo keying.
    int blueKeyProfile = type == BuildingType::Headquarters ? 1 : 0;

    BeginShaderMode(*teamColorShader);
    int primaryLocation = shaderLibrary.GetLocation(ShaderId::TeamColor, "playerPrimary");
    int secondaryLocation = shaderLibrary.GetLocation(ShaderId::TeamColor, "playerSecondary");
    int hasMaskLocation = shaderLibrary.GetLocation(ShaderId::TeamColor, "hasMaterialMask");
    int blueKeyLocation = shaderLibrary.GetLocation(ShaderId::TeamColor, "useAlbedoBlueKey");
    int blueKeyProfileLocation = shaderLibrary.GetLocation(ShaderId::TeamColor, "blueKeyProfile");
    if (primaryLocation >= 0)
        SetShaderValue(*teamColorShader, primaryLocation, primary, SHADER_UNIFORM_VEC4);
    if (secondaryLocation >= 0)
        SetShaderValue(*teamColorShader, secondaryLocation, secondary, SHADER_UNIFORM_VEC4);
    if (hasMaskLocation >= 0)
        SetShaderValue(*teamColorShader, hasMaskLocation, &hasMaterialMask, SHADER_UNIFORM_INT);
    if (blueKeyLocation >= 0)
        SetShaderValue(*teamColorShader, blueKeyLocation, &useAlbedoBlueKey, SHADER_UNIFORM_INT);
    if (blueKeyProfileLocation >= 0)
        SetShaderValue(*teamColorShader, blueKeyProfileLocation, &blueKeyProfile, SHADER_UNIFORM_INT);
    DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, tint);
    EndShaderMode();
}

// Draws a building snapshot with its standalone texture. `tint` modulates the
// sprite (WHITE = unchanged); a dim tint marks buildings still under construction.
void Renderer::DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint,
                                   Color ownerColor, bool applyTeamColor)
{
    Vec2f drawSize{
        static_cast<float>(footprint.x * TILE_SIZE),
        static_cast<float>(footprint.y * TILE_SIZE)};

    // Channel-wise multiply so the fallback shapes fade with the same tint the
    // GPU applies to textured sprites.
    auto modulate = [&](Color base)
    {
        return Color{
            static_cast<unsigned char>(base.r * tint.r / 255),
            static_cast<unsigned char>(base.g * tint.g / 255),
            static_cast<unsigned char>(base.b * tint.b / 255),
            static_cast<unsigned char>(base.a * tint.a / 255)};
    };

    auto textureIt = buildingTextures.find(type);
    if (textureIt != buildingTextures.end())
    {
        const Texture2D& texture = textureIt->second.Get();
        Rectangle src{0.0f, 0.0f, static_cast<float>(texture.width), -static_cast<float>(texture.height)};
        Rectangle dest{pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
        DrawBuildingSprite(texture, src, dest, tint, ownerColor, applyTeamColor, type);
        return;
    }

    Rectangle dest{pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
    DrawRectangleRounded(dest, 0.04f, 8, modulate(Color{96, 78, 56, 255}));
    DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f, modulate(Color{150, 108, 58, 255}));
}

// Same, but reads the frame from the type's registered animation clip
// (elapsedTime is normally the building's lifetime). Types with no clip, or
// a clip with frameCount==1, fall through to the static overload unchanged.
void Renderer::DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint, float elapsedTime,
                                   Color ownerColor, bool applyTeamColor)
{
    auto animIt = buildingAnimations.find(type);
    auto textureIt = buildingTextures.find(type);
    if (animIt == buildingAnimations.end() || animIt->second.frameCount <= 1 || textureIt == buildingTextures.end())
    {
        if (type == BuildingType::Bakery && tint.r == 255 && tint.g == 255 && tint.b == 255)
            DrawBakerySmoke(pos, footprint, elapsedTime);
        DrawBuildingTexture(type, footprint, pos, tint, ownerColor, applyTeamColor);
        return;
    }

    const AnimationClip& clip = animIt->second;
    const Texture2D& texture = textureIt->second.Get();
    int frame = ResolveAnimationFrame(clip, elapsedTime);
    float frameWidth = static_cast<float>(texture.width) / clip.frameCount;

    Vec2f drawSize{
        static_cast<float>(footprint.x * TILE_SIZE),
        static_cast<float>(footprint.y * TILE_SIZE)};

    Rectangle src{frameWidth * frame, 0.0f, frameWidth, -static_cast<float>(texture.height)};
    Rectangle dest{pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
    DrawBuildingSprite(texture, src, dest, tint, ownerColor, applyTeamColor, type);
}

// Draws terrain, territory and buildings from an immutable game snapshot.
void Renderer::DrawSnapshot(const GameSnapshot& snapshot)
{
    if (!worldLayersInitialized || !snapshot.IsValid())
        return;

    SetSimulationTick(snapshot.simulationTick);

    bool cameraChanged =
        cachedSnapshotCameraZoom != camera.zoom ||
        cachedSnapshotCameraTarget.x != camera.target.x ||
        cachedSnapshotCameraTarget.y != camera.target.y;
    bool snapshotChanged = cachedSnapshotTick != snapshot.simulationTick;
    if (!cameraChanged && !snapshotChanged)
        return;

    Vec2f worldA = RenderToWorld({0.0f, 0.0f});
    Vec2f worldB = RenderToWorld({static_cast<float>(RENDER_WIDTH), static_cast<float>(RENDER_HEIGHT)});
    float minWorldX = std::min(worldA.x, worldB.x);
    float maxWorldX = std::max(worldA.x, worldB.x);
    float minWorldY = std::min(worldA.y, worldB.y);
    float maxWorldY = std::max(worldA.y, worldB.y);

    int minTileX = std::clamp(static_cast<int>(std::floor(minWorldX / TILE_SIZE)) - 2, 0, snapshot.mapSize.x - 1);
    int maxTileX = std::clamp(static_cast<int>(std::ceil(maxWorldX / TILE_SIZE)) + 2, 0, snapshot.mapSize.x - 1);
    int minTileY = std::clamp(static_cast<int>(std::floor(minWorldY / TILE_SIZE)) - 2, 0, snapshot.mapSize.y - 1);
    int maxTileY = std::clamp(static_cast<int>(std::ceil(maxWorldY / TILE_SIZE)) + 2, 0, snapshot.mapSize.y - 1);

    ClearDynamicLights();
    ClearFogReveals();
    // Snapshot rendering has no Player building registry, so scan its anchor
    // tiles once and let radius-aware queue culling reject irrelevant sources.
    // This is independent of camera tile bounds and fixes fog changing shape
    // when an owning building scrolls just outside the view.
    for (int y = 0; y < snapshot.mapSize.y; y++)
    {
        for (int x = 0; x < snapshot.mapSize.x; x++)
        {
            size_t tileIndex = static_cast<size_t>(y * snapshot.mapSize.x + x);
            const auto& tile = snapshot.tiles[tileIndex];
            if (tile.hasBuilding)
            {
                QueueBuildingLight(tile.buildingType, tile.buildingFootprint,
                                   {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)},
                                   static_cast<int>(tileIndex), tile.isBuildingOperational);
                if (tile.buildingOwnerId == snapshot.localPlayerId)
                    QueueBuildingFogReveal(tile.buildingType, tile.buildingFootprint,
                                           {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)});
            }
        }
    }

    ClearLayer(WorldRenderLayer::Terrain);
    BeginLayer(WorldRenderLayer::Terrain);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            DrawAtlasTile(0, tile.terrainTextureId, pos);
        }
    }
    BeginBlendMode(BLEND_ADDITIVE);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            if (tile.resourceOverlayTextureId >= 0)
                DrawResourceGroundGlow(tile.resourceOverlayTextureId,
                                       {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)});
        }
    }
    EndBlendMode();
    EndLayer();

    ClearLayer(WorldRenderLayer::ResourceOverlays);
    BeginLayer(WorldRenderLayer::ResourceOverlays);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            if (tile.resourceOverlayTextureId < 0)
                continue;
            const Vec2f position{static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            QueueResourceLight(tile.resourceOverlayTextureId, position,
                               static_cast<int>(y * snapshot.mapSize.x + x));
            DrawResourceOverlay(tile.resourceOverlayTextureId, position);
        }
    }
    EndLayer();

    ClearLayer(WorldRenderLayer::MilitaryRoads);
    BeginLayer(WorldRenderLayer::MilitaryRoads);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            // Keep the track in the normal world layer order so static bridge
            // sprites render above it and dynamic objects (units/projectiles)
            // render above both. A Bridge replaces neither the track flag nor
            // the track texture; it only adds its sprite on StaticObjects.
            if (tile.isMilitaryRoad)
            {
                const int mask = RoadTopology::GetCardinalMask(x, y, snapshot.mapSize.x, snapshot.mapSize.y,
                    [&](int checkX, int checkY)
                    {
                        return snapshot.tiles[static_cast<size_t>(checkY * snapshot.mapSize.x + checkX)].isMilitaryRoad;
                    });
                DrawMilitaryRoadTexture(
                    {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)}, mask);
            }
        }
    }
    EndLayer();

    ClearLayer(WorldRenderLayer::WorldEffects);
    if (renderSettings.contactShadows)
    {
        const WorldLightingFrame lighting = GetCurrentWorldLightingFrame();
        const unsigned char shadowAlpha = static_cast<unsigned char>(std::clamp(
            32.0f + (1.0f - lighting.ambientIntensity) * 42.0f, 32.0f, 74.0f));
        BeginLayer(WorldRenderLayer::WorldEffects);
        for (int x = minTileX; x <= maxTileX; x++)
        {
            for (int y = minTileY; y <= maxTileY; y++)
            {
                const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
                if (!tile.hasBuilding || IsRoadLike(tile.buildingType))
                    continue;

                float width = tile.buildingFootprint.x * TILE_SIZE;
                float height = tile.buildingFootprint.y * TILE_SIZE;
                const float baseX = x * TILE_SIZE + width * 0.50f;
                const float baseY = RENDER_HEIGHT - y * TILE_SIZE - height * 0.84f;
                const float directionalLength = std::min(
                    lighting.shadowLength * 0.30f, std::max(width, height) * 1.10f);
                if (directionalLength > 0.5f)
                {
                    constexpr float samples[] = {0.32f, 0.62f, 0.90f};
                    constexpr float widths[] = {0.30f, 0.24f, 0.18f};
                    constexpr float alphas[] = {0.32f, 0.22f, 0.14f};
                    for (int i = 0; i < 3; ++i)
                    {
                        const float shadowX = baseX - lighting.sunDirection.x * directionalLength * samples[i];
                        const float shadowY = baseY + lighting.sunDirection.y * directionalLength * samples[i];
                        DrawEllipse(static_cast<int>(shadowX), static_cast<int>(shadowY),
                                    width * widths[i], std::max(2.0f, height * 0.075f),
                                    Color{0, 0, 0, static_cast<unsigned char>(shadowAlpha * alphas[i])});
                    }
                }
                DrawEllipse(static_cast<int>(baseX), static_cast<int>(baseY),
                            width * 0.32f,
                            std::max(2.0f, height * 0.075f),
                            Color{0, 0, 0, static_cast<unsigned char>(shadowAlpha * 0.70f)});
            }
        }
        EndLayer();
    }

    if (renderSettings.logisticsOverlay)
    {
        BeginLayer(WorldRenderLayer::WorldEffects);
        for (int x = minTileX; x <= maxTileX; x++)
        {
            for (int y = minTileY; y <= maxTileY; y++)
            {
                const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
                if (!tile.hasBuilding || !IsRoadLike(tile.buildingType))
                    continue;
                const int mask = RoadTopology::GetCardinalMask(x, y, snapshot.mapSize.x, snapshot.mapSize.y,
                    [&](int checkX, int checkY)
                    {
                        const auto& neighbor = snapshot.tiles[static_cast<size_t>(checkY * snapshot.mapSize.x + checkX)];
                        return neighbor.hasBuilding && IsRoadLike(neighbor.buildingType);
                    });
                DrawRoadUtilizationOverlay(
                    {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)},
                    tile.roadUtilization,
                    (mask & RoadTopology::West) != 0,
                    (mask & RoadTopology::East) != 0,
                    (mask & RoadTopology::North) != 0,
                    (mask & RoadTopology::South) != 0);
            }
        }
        EndLayer();
    }

    ClearLayer(WorldRenderLayer::StaticObjects);
    BeginLayer(WorldRenderLayer::StaticObjects);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            if (!tile.hasBuilding)
                continue;

            Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            const bool hasOwner = tile.buildingOwnerId >= 0;
            Color ownerColor = ResolveSnapshotPlayerColor(snapshot, tile.buildingOwnerId);
            const Color tint = tile.isBuildingOperational
                ? WHITE
                : Color{118, 122, 132, 215};
            // Snapshots do not retain per-building lifetimes, so visual-only
            // building clips share the deterministic simulation clock. Frame
            // selection still advances strictly left-to-right through the strip.
            constexpr float SimulationTickSeconds = 0.01f;
            const float elapsedTime = static_cast<float>(snapshot.simulationTick) * SimulationTickSeconds;
            if (IsRoadLike(tile.buildingType))
            {
                const int mask = RoadTopology::GetCardinalMask(x, y, snapshot.mapSize.x, snapshot.mapSize.y,
                    [&](int checkX, int checkY)
                    {
                        const auto& neighbour = snapshot.tiles[static_cast<size_t>(checkY * snapshot.mapSize.x + checkX)];
                        return neighbour.hasBuilding && IsRoadLike(neighbour.buildingType);
                    });
                DrawRoadTexture(tile.buildingType, pos, mask, tint);
            }
            else
            {
                DrawBuildingTexture(tile.buildingType, tile.buildingFootprint, pos, tint, elapsedTime,
                                    ownerColor, hasOwner);
            }
        }
    }
    EndLayer();

    ClearLayer(WorldRenderLayer::DynamicObjects);
    BeginLayer(WorldRenderLayer::DynamicObjects);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            if (!tile.hasBuilding)
                continue;
            const Vec2f position{static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            DrawDamageFlashOverlay(position, tile.buildingFootprint, tile.buildingDamageIndicator);
            if (renderSettings.logisticsOverlay && IsRoadLike(tile.buildingType))
                DrawRoadSaturationIndicator(position, tile.roadSaturated);
        }
    }
    EndLayer();

    cachedSnapshotTick = snapshot.simulationTick;
    cachedSnapshotCameraTarget = {camera.target.x, camera.target.y};
    cachedSnapshotCameraZoom = camera.zoom;
}

// Initializes Renderer::ScreenToRender.
Vec2f Renderer::ScreenToRender(Vector2 screen)
{
    float scale = std::min(GetScreenWidth() / static_cast<float>(RENDER_WIDTH),
                           GetScreenHeight() / static_cast<float>(RENDER_HEIGHT));
    float width = RENDER_WIDTH * scale;
    float height = RENDER_HEIGHT * scale;
    float offsetX = (GetScreenWidth() - width) * 0.5f;
    float offsetY = (GetScreenHeight() - height) * 0.5f;

    if (screen.x < offsetX || screen.x > offsetX + width ||
        screen.y < offsetY || screen.y > offsetY + height)
    {
        return Vec2f{-1.0f, -1.0f};
    }

    return Vec2f{
        (screen.x - offsetX) / scale,
        (screen.y - offsetY) / scale};
}

// Initializes Renderer::RenderToScreen.
Vec2f Renderer::RenderToScreen(Vec2f render)
{
    float scale = std::min(GetScreenWidth() / static_cast<float>(RENDER_WIDTH),
                           GetScreenHeight() / static_cast<float>(RENDER_HEIGHT));
    float width = RENDER_WIDTH * scale;
    float height = RENDER_HEIGHT * scale;
    float offsetX = (GetScreenWidth() - width) * 0.5f;
    float offsetY = (GetScreenHeight() - height) * 0.5f;

    return Vec2f{
        offsetX + render.x * scale,
        offsetY + render.y * scale};
}

// Initializes Renderer::RenderToWorld.
Vec2f Renderer::RenderToWorld(Vec2f render)
{
    return Vec2f{
        render.x / camera.zoom + camera.target.x,
        RENDER_HEIGHT - camera.target.y - (RENDER_HEIGHT - render.y) / camera.zoom};
}

// Initializes Renderer::WorldToRender.
Vec2f Renderer::WorldToRender(Vec2f world)
{
    return Vec2f{
        (world.x - camera.target.x) * camera.zoom,
        RENDER_HEIGHT - (RENDER_HEIGHT - world.y - camera.target.y) * camera.zoom};
}

// Initializes Renderer::ScreenToWorld.
Vec2f Renderer::ScreenToWorld(Vector2 screen)
{
    Vec2f render = ScreenToRender(screen);
    if (render.x < 0.0f || render.y < 0.0f)
        return {-1.0f, -1.0f};

    return RenderToWorld(render);
}

// Initializes Renderer::WorldToScreen.
Vec2f Renderer::WorldToScreen(Vec2f world)
{
    return RenderToScreen(WorldToRender(world));
}

// Adjusts camera or map-space geometry.
void Renderer::ClampCameraToMap(Vec2i mapSize)
{
    float mapW = static_cast<float>(mapSize.x * TILE_SIZE);
    float mapH = static_cast<float>(mapSize.y * TILE_SIZE);
    if (mapW <= 0.0f || mapH <= 0.0f)
        return;

    float topRenderPadding = ScreenTopPaddingToRender(topScreenPadding);
    float usableRenderHeight = std::max(1.0f, static_cast<float>(RENDER_HEIGHT) - topRenderPadding);
    const float minZoom = std::max(RENDER_WIDTH / mapW, usableRenderHeight / mapH);
    constexpr float MaxZoom = 2.5f;
    // Round the lower bound upward so snapping never reveals outside the map.
    const float tileAlignedMinZoom = std::ceil(minZoom * TILE_SIZE) / TILE_SIZE;
    const float effectiveMinZoom = std::min(tileAlignedMinZoom, MaxZoom);
    camera.zoom = std::clamp(SnapZoomToTileGrid(camera.zoom), effectiveMinZoom, MaxZoom);

    float visibleW = RENDER_WIDTH / camera.zoom;
    float visibleH = usableRenderHeight / camera.zoom;
    float maxX = std::max(0.0f, mapW - visibleW);
    float minY = RENDER_HEIGHT - mapH;
    float maxY = RENDER_HEIGHT - visibleH;

    camera.target.x = std::clamp(camera.target.x, 0.0f, maxX);
    camera.target.y = std::clamp(camera.target.y, minY, maxY);
    SnapCameraTargetToRenderPixels(camera);
    // Snapping can cross a clamped map edge by less than one screen pixel.
    camera.target.x = std::clamp(camera.target.x, 0.0f, maxX);
    camera.target.y = std::clamp(camera.target.y, minY, maxY);
}

void Renderer::DrawRoadTexture(BuildingType type, Vec2f pos, int connectionMask, Color tint)
{
    connectionMask &= 0x0F;
    int atlasId = 19;
    int tileId = (type == BuildingType::Bridge ? 16 : 0) + connectionMask;
    if (type == BuildingType::Road && atlasMap.contains(145))
    {
        const int tileX = static_cast<int>(std::floor(pos.x / TILE_SIZE));
        const int tileY = static_cast<int>(std::floor(pos.y / TILE_SIZE));
        const unsigned int hash =
            static_cast<unsigned int>(tileX) * 73856093u ^
            static_cast<unsigned int>(tileY) * 19349663u ^
            static_cast<unsigned int>(connectionMask) * 83492791u;
        atlasId = 145;
        tileId = static_cast<int>(hash % 3u) * 16 + connectionMask;
    }

    auto atlasIt = atlasMap.find(atlasId);
    if (atlasIt == atlasMap.end() || !atlasIt->second.tex.IsValid())
    {
        DrawRectangle(static_cast<int>(pos.x), RENDER_HEIGHT - TILE_SIZE - static_cast<int>(pos.y),
                      TILE_SIZE, TILE_SIZE, Color{112, 78, 48, tint.a});
        return;
    }

    TextureAtlas& atlas = atlasIt->second;
    Rectangle source = atlas.GetRectFromId(tileId);
    source.height *= -1.0f;
    Rectangle destination{pos.x, RENDER_HEIGHT - TILE_SIZE - pos.y,
                          static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)};
    DrawTexturePro(atlas.tex.Get(), source, destination, {0.0f, 0.0f}, 0.0f, tint);
}

void Renderer::DrawMilitaryRoadTexture(Vec2f pos, int connectionMask, Color tint)
{
    constexpr int MilitaryRoadAtlasId = 144;
    connectionMask &= 0x0F;
    int atlasId = MilitaryRoadAtlasId;
    int tileId = connectionMask;
    if (atlasMap.contains(146))
    {
        const int tileX = static_cast<int>(std::floor(pos.x / TILE_SIZE));
        const int tileY = static_cast<int>(std::floor(pos.y / TILE_SIZE));
        const unsigned int hash =
            static_cast<unsigned int>(tileX) * 73856093u ^
            static_cast<unsigned int>(tileY) * 19349663u ^
            static_cast<unsigned int>(connectionMask) * 83492791u;
        atlasId = 146;
        tileId = static_cast<int>(hash % 3u) * 16 + connectionMask;
    }

    auto atlasIt = atlasMap.find(atlasId);
    if (atlasIt == atlasMap.end() || !atlasIt->second.tex.IsValid())
    {
        DrawRectangle(static_cast<int>(pos.x), RENDER_HEIGHT - TILE_SIZE - static_cast<int>(pos.y),
                      TILE_SIZE, TILE_SIZE, Color{190, 178, 151, tint.a});
        return;
    }

    TextureAtlas& atlas = atlasIt->second;
    Rectangle source = atlas.GetRectFromId(tileId);
    source.height *= -1.0f;
    Rectangle destination{pos.x, RENDER_HEIGHT - TILE_SIZE - pos.y,
                          static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)};
    DrawTexturePro(atlas.tex.Get(), source, destination, {0.0f, 0.0f}, 0.0f, tint);
}

void Renderer::SetTopScreenPadding(float padding)
{
    topScreenPadding = std::max(0.0f, padding);
}

// Adjusts camera or map-space geometry.
void Renderer::CenterCameraOnWorld(Vec2f worldPoint, Vec2i mapSize)
{
    camera.target.x = worldPoint.x - (RENDER_WIDTH * 0.5f) / camera.zoom;
    camera.target.y = RENDER_HEIGHT - worldPoint.y - (RENDER_HEIGHT * 0.5f) / camera.zoom;
    ClampCameraToMap(mapSize);
}

// Adjusts camera or map-space geometry.
void Renderer::ZoomAtScreenPoint(Vector2 screen, float wheel, Vec2i mapSize)
{
    if (wheel == 0.0f)
        return;

    Vec2f render = ScreenToRender(screen);
    if (render.x < 0.0f || render.y < 0.0f)
        return;

    Vec2f worldBefore = RenderToWorld(render);
    camera.zoom += wheel * 0.12f;
    ClampCameraToMap(mapSize);

    camera.target.x = worldBefore.x - render.x / camera.zoom;
    camera.target.y = RENDER_HEIGHT - worldBefore.y - (RENDER_HEIGHT - render.y) / camera.zoom;
    ClampCameraToMap(mapSize);
}

// Clears this runtime state.
void Renderer::ClearLayers()
{
    if (!worldLayersInitialized)
        return;

    for(auto& l : layers)
    {
        BeginTextureMode(l.fbo.Get());
        ClearBackground(BLANK);
        EndTextureMode();
    }
}
