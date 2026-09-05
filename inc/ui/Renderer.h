#ifndef RENDERER_H
#define RENDERER_H

#include "core/Types.h"
#include "core/GameSnapshot.h"
#include "raylib.h"
#include "simulation/ShipmentRenderState.h"
#include "ui/Gui.h"
#include "ui/ShaderLibrary.h"
#include "ui/WorldLighting.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

constexpr int RENDER_WIDTH = 1920;
constexpr int RENDER_HEIGHT = 1080;
inline constexpr float CameraZoomWheelStep = 0.12f;
inline constexpr float CameraMaxZoom = 2.5f;

struct CameraVisibleWorldRect
{
    float minX{0.0f};
    float maxX{0.0f};
    float minY{0.0f};
    float maxY{0.0f};
};

// Returns the world-space rectangle visible between the top HUD padding and
// the bottom of the fixed render surface. `topRenderPadding` is expressed in
// fixed render pixels, making this helper independent of the OS window size.
CameraVisibleWorldRect ComputeCameraVisibleWorldRect(const Camera2D& camera,
                                                     float topRenderPadding);

class Building;
enum class BuildingType : int;

// Ordered world layers. Tactical UI is intentionally not part of this list:
// it will be composed after world lighting in a later rendering pass.
enum class WorldRenderLayer : std::size_t
{
    Terrain,
    ResourceOverlays,
    WorldEffects,
    StaticObjects,
    DynamicObjects,
    Count
};

constexpr std::size_t ToLayerIndex(WorldRenderLayer layer)
{
    return static_cast<std::size_t>(layer);
}

// Debug views intentionally expose intermediate render targets without
// affecting the simulation. They are useful for validating FBO orientation
// and local-light placement at different camera zoom levels.
enum class RenderDebugView : std::uint8_t
{
    Final,
    WorldAlbedo,
    LightMap,
    FogMask
};

struct RenderSettings
{
    bool teamColors{true};
    bool dayNightCycle{true};
    bool dynamicLights{true};
    bool contactShadows{true};
    // Local province fog was removed; global-map fog is rendered by the
    // strategic map panel from the campaign presentation view.
    bool fogOfWar{false};
    bool colorGrading{true};
    bool retroFilter{true};
    // A low-cost second, wider additive emitter pass. It never touches UI or
    // bright terrain/albedo, unlike a bloom pass over the final frame.
    bool localLightBloom{true};
    // Procedural rain is a presentation-only postprocess. It is deliberately
    // disabled by default and has no weather/gameplay simulation behind it.
    bool rainOverlay{false};
    bool logisticsOverlay{false};
    RenderDebugView debugView{RenderDebugView::Final};
};

// Global presentation preference shared between the options scene and the
// active gameplay renderer. It is intentionally not a simulation setting and
// is therefore never serialized into saves, commands, or multiplayer state.
bool IsFogOfWarPreferenceEnabled();
void SetFogOfWarPreferenceEnabled(bool enabled);
bool IsColorGradingPreferenceEnabled();
void SetColorGradingPreferenceEnabled(bool enabled);
bool IsRetroFilterPreferenceEnabled();
void SetRetroFilterPreferenceEnabled(bool enabled);
bool IsLocalLightBloomPreferenceEnabled();
void SetLocalLightBloomPreferenceEnabled(bool enabled);
bool IsRainOverlayPreferenceEnabled();
void SetRainOverlayPreferenceEnabled(bool enabled);
bool IsLogisticsOverlayPreferenceEnabled();
void SetLogisticsOverlayPreferenceEnabled(bool enabled);

// Global presentation overlay used by GameWindow for scene transitions.
// The alpha is intentionally render-only and never touches simulation state.
void SetSceneTransitionOverlayAlpha(float alpha);
void DrawSceneTransitionOverlay();

struct FogRevealView
{
    Vector2 worldPosition{};
    float radiusWorld{96.0f};
};

// Animation clip: sequence of frames from an atlas
struct AnimationClip
{
    int startFrameId = 0;
    int frameCount = 1;
    float frameTime = 0.1f;
    bool looping = true;
};

// Resolves which 0-based frame index of a clip should show at an elapsed
// time. Shared by TextureAtlas (atlas-tile animation) and Renderer's
// standalone building-texture animation so the timing math lives in one place.
int ResolveAnimationFrame(const AnimationClip& clip, float elapsedTime);

// Fixed-resolution render layer backed by a Raylib render texture.
struct CanvasLayer
{
    // GPU allocation is explicit so menu scenes do not allocate world-sized FBOs.
    void Initialize(int width = RENDER_WIDTH, int height = RENDER_HEIGHT);
    // Must normally be called while the raylib window/context is still alive.
    void Shutdown();
    bool IsInitialized() const { return fbo.IsValid(); }

    tvorin::ui::RenderTextureHandle fbo{};
};

// All render targets use canonical screen space (origin in the top-left).
// Raylib's backbuffer and offscreen passes require opposite source-rectangle
// conventions, so the destination boundary must be stated explicitly.
enum class RenderTargetDestination : std::uint8_t
{
    OffscreenPass,
    Window
};

// This is the only place that defines the raylib FBO sampling correction.
// This is intentionally GPU-free so the FBO orientation contract can be unit
// tested without opening the game window.
Rectangle RenderTargetSourceRect(Texture2D texture, RenderTargetDestination destination);

// Tile atlas loader and id-to-source-rectangle mapper.
struct TextureAtlas
{
    // Loads atlas texture and derives grid dimensions from tile size.
    void LoadTextureAtlas(const char* path, Vec2i tileSize = {TILE_SIZE, TILE_SIZE});
    // Returns source rectangle for a tile id, clamped to atlas bounds.
    Rectangle GetRectFromId(int id);
    // Registers an animation clip under an id (ETAP 5.2).
    void RegisterAnimation(int clipId, const AnimationClip& clip);
    // Returns the clip for an id, or a default single-frame clip if unregistered.
    AnimationClip GetAnimation(int clipId) const;
    // Resolves which atlas frame a clip should show at a given elapsed time.
    int GetFrameForAnimation(int clipId, float elapsedTime) const;

    tvorin::ui::TextureHandle tex;
    Vec2i size;
    Vec2i dim;
    std::map<int, AnimationClip> animations;
};

// Draws the world through a camera and composites UI over render layers.
class Renderer
{
    public:

    Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Allocates the world render layers. Only GameScene should call this;
    // UI-only scenes can keep their Renderer uninitialized.
    bool InitializeWorldLayers();
    // Explicitly frees all GPU resources owned by this renderer. Call before
    // CloseWindow(); this method is intentionally safe to call more than once.
    void Shutdown();
    bool HasWorldLayers() const { return worldLayersInitialized; }
    // The render clock is derived from the deterministic simulation tick, not
    // from wall-clock time. Both direct and snapshot render paths set it.
    void SetSimulationTick(std::uint64_t tick)
    {
        simulationTick = tick;
        snapshotTick = std::numeric_limits<std::uint64_t>::max();
    }
    void SetRenderSettings(const RenderSettings& settings) { renderSettings = settings; }
    const RenderSettings& GetRenderSettings() const { return renderSettings; }
    void SetDayNightCycleEnabled(bool enabled) { renderSettings.dayNightCycle = enabled; }
    bool IsDayNightCycleEnabled() const { return renderSettings.dayNightCycle; }
    // Visual-only testing shortcut. When enabled, lighting stays at phase 0
    // (night) without changing the deterministic simulation clock.
    void ToggleNightPreview();
    bool IsNightPreviewEnabled() const { return nightPreviewEnabled; }
    WorldLightingFrame GetCurrentWorldLightingFrame() const;
    void SetDynamicLightsEnabled(bool enabled) { renderSettings.dynamicLights = enabled; }
    bool AreDynamicLightsEnabled() const { return renderSettings.dynamicLights; }
    void SetTeamColorsEnabled(bool enabled) { renderSettings.teamColors = enabled; }
    bool AreTeamColorsEnabled() const { return renderSettings.teamColors; }
    void SetContactShadowsEnabled(bool enabled) { renderSettings.contactShadows = enabled; }
    bool AreContactShadowsEnabled() const { return renderSettings.contactShadows; }
    bool IsFogOfWarEnabled() const { return renderSettings.fogOfWar; }
    void SetColorGradingEnabled(bool enabled) { renderSettings.colorGrading = enabled; }
    bool IsColorGradingEnabled() const { return renderSettings.colorGrading; }
    void SetRetroFilterEnabled(bool enabled) { renderSettings.retroFilter = enabled; }
    bool IsRetroFilterEnabled() const { return renderSettings.retroFilter; }
    void SetLocalLightBloomEnabled(bool enabled) { renderSettings.localLightBloom = enabled; }
    bool IsLocalLightBloomEnabled() const { return renderSettings.localLightBloom; }
    void SetRainOverlayEnabled(bool enabled) { renderSettings.rainOverlay = enabled; }
    bool IsRainOverlayEnabled() const { return renderSettings.rainOverlay; }
    void SetLogisticsOverlayEnabled(bool enabled) { renderSettings.logisticsOverlay = enabled; }
    bool IsLogisticsOverlayEnabled() const { return renderSettings.logisticsOverlay; }
    void CycleDebugView();
    // Lights are rebuilt from world sources; their data does not enter
    // simulation state, saves, or gameplay checksums. Target clipping is left
    // to the GPU so camera pan/zoom cannot remove a still-relevant emitter.
    void ClearDynamicLights();
    void QueueDynamicLight(const LightEmitterView& light);
    void QueueBuildingLight(BuildingType type, Vec2i footprint, Vec2f pos, int stableId, bool isOperational);
    // Adds a small mineral-coloured light for resource overlay atlas cells
    // 0..47. Non-mineral overlays (for example WOOD) are ignored.
    void QueueResourceLight(int resourceOverlayTextureId, Vec2f pos, int stableId);
    // Fog revealers are rebuilt from all owned world sources by
    // GameWorld::DrawMap/DrawSnapshot each frame. No state is retained, so
    // toggling fog cannot show stale visibility.
    void ClearFogReveals();
    void QueueFogReveal(const FogRevealView& reveal);
    void QueueBuildingFogReveal(BuildingType type, Vec2i footprint, Vec2f pos);

    // Draws all render layers and UI widgets, then presents the frame.
    // Convenience wrapper = DrawContent + PresentFrame.
    void Draw(std::vector<UiWidget*> ui = {}, double dt = 0);
    // Begins the frame and issues all draw calls (layers + widgets) but does NOT
    // present. Lets a caller draw under a lock and release it before the
    // frame-cap-blocking present. Must be paired with PresentFrame().
    void DrawContent(std::vector<UiWidget*> ui = {}, double dt = 0,
                     bool drawWorld = true);
    // Presents the frame (EndDrawing). This is where frame-cap blocking happens,
    // so callers holding a lock should release it before calling this.
    void PresentFrame(bool drawTransitionOverlay = true);
    // Window-wide cursor presentation drawn after UI and transition overlays.
    static void InitializeCustomCursor();
    static void ShutdownCustomCursor();
    static void SetCustomCursorVisible(bool visible);
    static bool IsCustomCursorVisible();
    // Requests a one-shot capture of the completed backbuffer. The request is
    // fulfilled in PresentFrame(), after UI, transition overlay and cursor.
    static void RequestFinalFrameCapture();
    // Draws a full texture on a render layer at tile coordinates.
    void DrawOnLayer(WorldRenderLayer, Texture2D, Vec2i);
    // Draws one atlas tile on a render layer at tile coordinates.
    void DrawOnLayer(WorldRenderLayer, int, int, Vec2f);
    // Begins drawing to a render layer.
    void BeginLayer(WorldRenderLayer);
    // Ends drawing to the current render layer.
    void EndLayer();
    // Clears one render layer without changing camera state.
    void ClearLayer(WorldRenderLayer);
    // Draws one atlas tile in world space.
    void DrawAtlasTile(int, int, Vec2f);
    // Draws one atlas tile in world space with scale.
    void DrawAtlasTile(int, int, Vec2f, Vec2f);
    // Draws atlas 41 mineral cells with a material-specific colour grade while
    // preserving the atlas silhouette exactly. Environmental tint/glow is
    // handled by the dedicated ground-glow and local-emitter paths.
    void DrawResourceOverlay(int resourceOverlayTextureId, Vec2f pos);
    // Draws the broad deposit glow below all resource sprites. Call while the
    // terrain layer and additive blending are active so neighbouring halos
    // merge behind the rocks without darkening the terrain albedo.
    void DrawResourceGroundGlow(int resourceOverlayTextureId, Vec2f pos);
    // Draws one atlas tile, resolving the frame from an animation clip and elapsed time (ETAP 5.3).
    void DrawAtlasTile(int atlas, int clipId, Vec2f pos, float elapsedTime);
    // Same, stretched to a target world size.
    void DrawAtlasTile(int atlas, int clipId, Vec2f pos, Vec2f drawSize, float elapsedTime);
    // Loads a standalone building texture for a building type.
    void LoadBuildingTexture(BuildingType, const std::string&);
    // Returns the first source frame for an animated texture, or the full
    // texture for a static building sprite.
    Rectangle GetBuildingTextureFirstFrameSource(BuildingType) const;
    // Registers an animation clip for a standalone building texture (ETAP 5.4).
    // Frames are read as a horizontal strip inside the loaded texture (frame
    // width = texture width / frameCount). Types with no registered clip (or
    // frameCount==1) keep drawing the full texture — fully backward compatible.
    void RegisterBuildingAnimation(BuildingType type, const AnimationClip& clip);
    // True when a type has a multi-frame strip registered. This lets callers
    // redraw otherwise cached building layers while the clip advances.
    bool HasBuildingAnimation(BuildingType type) const;
    // Draws a building with its standalone texture.
    void DrawBuildingTexture(Building*, Vec2f, Color tint = WHITE);
    // Draws a building snapshot with its standalone texture.
    void DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint = WHITE,
                             Color ownerColor = WHITE, bool applyTeamColor = false);
    // Draws one road-like tile using the four-neighbour connection mask.
    // Mask bits are West=1, East=2, North=4 and South=8. Road cells occupy
    // the first sixteen entries of atlas 19.
    // Road-only cells can use atlas 145, which contains three 16-cell variants
    // stacked in rows; the selected variant is derived from the tile position.
    void DrawRoadTexture(BuildingType type, Vec2f pos, int connectionMask, Color tint = WHITE);
    // Draws pointer-free in-flight resource views on the currently active
    // dynamic layer. The caller owns BeginLayer/EndLayer.
    void DrawShipments(const std::vector<ShipmentRenderState>& shipments, Vec2i mapSize);
    // Same, picking the frame from the type's registered animation clip and elapsed time.
    void DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint, float elapsedTime,
                             Color ownerColor = WHITE, bool applyTeamColor = false);
    // Draws terrain, territory and buildings from an immutable game snapshot.
    void DrawSnapshot(const GameSnapshot& snapshot);
    // Converts OS screen coordinates to fixed render coordinates.
    Vec2f ScreenToRender(Vector2);
    // Converts fixed render coordinates to OS screen coordinates.
    Vec2f RenderToScreen(Vec2f);
    // Converts fixed render coordinates to world coordinates.
    Vec2f RenderToWorld(Vec2f);
    // Converts world coordinates to fixed render coordinates.
    Vec2f WorldToRender(Vec2f);
    // Converts OS screen coordinates to world coordinates.
    Vec2f ScreenToWorld(Vector2);
    // Converts world coordinates to OS screen coordinates.
    Vec2f WorldToScreen(Vec2f);
    // Keeps camera view within map bounds when possible.
    void ClampCameraToMap(Vec2i mapSize);
    // Centers the camera on a world-space point and clamps it to map bounds.
    void CenterCameraOnWorld(Vec2f worldPoint, Vec2i mapSize);
    // Applies cursor-centered zoom and clamps camera afterwards.
    void ZoomAtScreenPoint(Vector2 screen, float wheel, Vec2i mapSize);
    // Clears all render layers.
    void ClearLayers();


    std::array<CanvasLayer, ToLayerIndex(WorldRenderLayer::Count)> layers{};
    // Composite target for every world layer. UI is deliberately drawn after it.
    CanvasLayer worldComposite;
    CanvasLayer litWorld;
    CanvasLayer lightMap;
    CanvasLayer fogMask;
    CanvasLayer foggedWorld;
    // Final world-only grading/retro target. UI is drawn afterwards and never
    // receives palette quantization, scanlines, or contrast correction.
    CanvasLayer postProcessedWorld;
    std::map<int, TextureAtlas> atlasMap;
    std::map<BuildingType, tvorin::ui::TextureHandle> buildingTextures;
    std::map<BuildingType, AnimationClip> buildingAnimations;
    // White radial alpha mask shared by additive lights and soft fog revealers.
    tvorin::ui::TextureHandle radialLightMask{};
    ShaderLibrary shaderLibrary;

    Camera2D camera;
    bool worldLayersInitialized{false};
    bool layerActive{false};
    RenderSettings renderSettings{};
    std::uint64_t simulationTick{0};
    std::uint64_t snapshotTick{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t worldCompositeTick{0};
    std::uint64_t uiDrawTick{0};
    DayNightConfig dayNightConfig{};
    bool nightPreviewEnabled{false};
    std::vector<LightEmitterView> dynamicLights;
    std::vector<FogRevealView> fogReveals;
    std::uint64_t cachedSnapshotTick{std::numeric_limits<std::uint64_t>::max()};
    ProvinceId cachedSnapshotProvinceId{InvalidProvinceId};
    Vec2f cachedSnapshotCameraTarget{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float cachedSnapshotCameraZoom{-1.0f};

private:
    void DrawBuildingSprite(Texture2D texture, Rectangle source, Rectangle destination, Color tint,
                            Color ownerColor, bool applyTeamColor, BuildingType type);
    // Draws local emitters into whichever render target is currently active.
    // Their world placement uses the same Camera2D matrix as sprite layers.
    void DrawDynamicLightsToActiveTarget(const WorldLightingFrame& lighting, bool bloomEnabled);
    void DrawLightMap(const WorldLightingFrame& lighting, bool localLightsEnabled);
    void DrawFogMask();
};



#endif
