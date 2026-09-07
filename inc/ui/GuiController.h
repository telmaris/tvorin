#ifndef GUI_CONTROLLER_H
#define GUI_CONTROLLER_H

#include "ui/Gui.h"
#include "ui/BuildInteractionState.h"
#include "ui/UpgradeGestureTargets.h"
#include "ui/GameplayClock.h"
#include "ui/ShaderLibrary.h"
#include "ui/UiTheme.h"
#include "economy/BuildingConfig.h"
#include "economy/BuildingSalvage.h"
#include "world/GlobalMap.h"
#include "warfare/TaskGroupIds.h"
#include "raylib.h"

#include <cstdint>
#include <array>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class Scene;
class GameScene;
class CampaignStatusWidget;
struct JourneyStatusView;

class GameplayClockWidget : public UiWidget
{
public:
    void UpdateSize(Vec2i windowSize) override;
    void Update(double dt) override;

    GameScene* scene{nullptr};
};
class GuiController;

// Implementations are split by feature across src/ui/Gui*.cpp.

// Mutable camera drag state shared by map interaction systems.
struct CameraMovement
{
    bool isMoving = false;
    // RMB both pans and assigns logistics targets; displacement distinguishes
    // a click from a drag.
    Vec2i rmbPressScreenPos{-1, -1};
    bool rmbDragged = false;
};

// Stabilizes a road-paint gesture to one axis. A short deliberate pause arms
// a new corner; ordinary sideways hand jitter never bends the active segment.
struct RoadDragStabilizer
{
    enum class Axis { None, Horizontal, Vertical };

    void Begin(Vec2i tile, Vector2 mousePosition);
    void End();
    Vec2i Constrain(Vec2i rawTile, Vector2 mousePosition, double dt, Vec2i lastPlacedTile);
    bool IsActive() const { return active; }
    Axis GetAxis() const { return axis; }

private:
    static constexpr float DirectionLockPixels = 5.0f;
    static constexpr float StationaryPixelTolerance = 1.5f;
    static constexpr double TurnPauseSeconds = 0.18;

    bool active{false};
    Axis axis{Axis::None};
    Vec2i segmentAnchor{-9999, -9999};
    Vector2 gestureOriginMouse{};
    Vector2 lastMousePosition{};
    double stationaryTime{0.0};
};

// One selectable item in the build panel.
struct BuildOption
{
    std::string name;
    std::string costText;
    std::vector<ResourceAmountDefinition> buildCosts;
    std::vector<std::string> lockReasons;
    BuildingType buildingType{BuildingType::Building};
    Vec2i footprint{1, 1};
    double buildTime{0.0};
    std::string category{"OTHER"};
    std::function<std::unique_ptr<Building>()> previewFactory;
    std::function<void(Vec2i)> buildAt;
};

// Interaction mode owned by GuiController.
class GuiSystem
{
public:
    virtual ~GuiSystem() = default;
    explicit GuiSystem(GuiController* con) : owner(con) {}
    GuiSystem() = delete;

    // Advances this interaction mode by one frame.
    virtual void Update(double dt) = 0;

    // Rebuilds widgets owned by this interaction mode after layout changes.
    virtual void UpdateUiWidgets(Vec2i) = 0;

    // Globally registered input subscribers must be enabled and disabled here.
    virtual void OnActivate() {}
    virtual void OnDeactivate() {}

    // Feature-specific transition preconditions belong to the target system.
    virtual bool CanActivate() { return true; }

    GuiController* owner{nullptr};
    // Generic owner used by shared rendering and action wiring. Gameplay
    // systems also keep a typed GameScene pointer.
    Scene* scene{nullptr};
    std::map<std::string, std::function<void()>> actionMap;
};

// Routes input actions to the active GUI interaction system.
class GuiController
{
public:
    // Creates interaction systems and attaches the controller to a scene.
    void Init(Scene *);
    // Updates the active system.
    void Update(double);
    // Executes an action in the active system when it is registered.
    void MakeAction(std::string);

    // Registers an interaction system by name.
    template <typename T> void AddSystem(std::string name)
    {
        static_assert(std::is_base_of<GuiSystem, T>::value);

        systems[name] = std::make_shared<T>(this);
    }

    // Switches active interaction system (refuses when the target system's
    // own CanActivate() precondition isn't met, e.g. TechGuiSystem).
    void ChangeSystem(std::string name);
    // Checks the current interaction layer without exposing pointer identity
    // to scene/render orchestration.
    bool IsSystemActive(const std::string& name) const;
    // Returns widgets that should be drawn by the renderer.
    inline std::vector<UiWidget*> GetUiWidgets() { return ui; }
    // Adds a widget to the current draw list.
    inline void AddUiWidget(UiWidget* ptr) { ui.push_back(ptr); }

    GameplayClockWidget* GetGameplayClockWidget() { return &gameplayClockWidget; }

    std::map<std::string, std::shared_ptr<GuiSystem>> systems;
    std::shared_ptr<GuiSystem> activeSystem;

    std::vector<UiWidget*> ui;
    Scene *scene{nullptr};
    GameplayClockWidget gameplayClockWidget;
};


// ─── Map overlay widgets ─────────────────────────────────────────────────────

// Draws the ghost preview for the currently selected build option.
class BuildGhostWidget : public UiWidget
{
public:
    BuildGhostWidget() = default;
    // Draws build preview and validity tint under the cursor.
    void Update(double dt) override;

    GameScene* scene{nullptr};
    const BuildOption* selectedOption{nullptr};
    bool canBuild{false};
    Vec2i tilePos{0, 0};
};

// Draws selection highlight for the currently selected building.
class SelectedBuildingWidget : public UiWidget
{
public:
    // Draws selected building footprint highlight.
    void Update(double dt) override;

    GameScene* scene{nullptr};
    Building* building{nullptr};
};

class DemolitionTargetWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
    Building* building{nullptr};
    bool actionable{false};
};

class DemolitionTooltipWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
    Building* building{nullptr};
    DemolitionPreview preview;
};

// Draws warning highlights over production buildings that cannot currently work.
class ProductionWarningWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
};

// Delayed hover tooltip for buildings in the default map view.
class BuildingHoverTooltipWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
    int hoveredBuildingId{-1};
    double hoverDuration{0.0};
    int connectivityBuildingId{-1};
    double connectivityAge{0.0};
    bool connectivityKnown{false};
    bool roadDisconnected{false};
};

// Screen-space marker for an upgrade target or an already upgrading building.
class UpgradeTargetWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
    Building* building{nullptr};
    bool actionable{false};
};

// ─── HUD and full-screen panels ──────────────────────────────────────────────

// Top-screen strategic resource summary for the local player.
class StrategicResourceHudWidget : public UiWidget
{
public:
    void UpdateSize(Vec2i windowSize) override;
    void Update(double dt) override;
    void SetTutorialHighlights(bool manpower, bool food)
    {
        highlightManpower = manpower;
        highlightFood = food;
    }
    void SetTutorialLockedButtons(bool locked)
    {
        tutorialDisableDestroy = locked;
        tutorialDisableDecisions = locked;
        tutorialDisableStatistics = locked;
    }
    void SetTutorialDecisionsLocked(bool locked) { tutorialDisableDecisions = locked; }

    GameScene* scene{nullptr};
    bool highlightManpower{false};
    bool highlightFood{false};
    bool tutorialDisableDestroy{false};
    bool tutorialDisableDecisions{false};
    bool tutorialDisableStatistics{false};
};

// Geometry-only hit testing for the pointer-free global-map presentation
// snapshot. Screen coordinates are kept outside the campaign model.
class GlobalMapNodeHitTester
{
public:
    static std::optional<ProvinceId> HitTest(const GlobalMapView& view, Vector2 point,
                                             Vector2 origin, float scale,
                                             float radius = 18.0f);
};

// Geometry-only tract hit testing. A scaled tolerance is supplied by the
// canvas, and equal-distance candidates are resolved by stable connection ID.
class GlobalMapEdgeHitTester
{
public:
    static std::optional<ProvinceConnectionId> HitTest(const GlobalMapView& view,
                                                        Vector2 point, Vector2 origin,
                                                        float scale,
                                                        float tolerance = 9.0f);
};

class GlobalMapPanelWidget : public UiWidget
{
public:
    GlobalMapPanelWidget();
    ~GlobalMapPanelWidget() override;
    void UpdateSize(Vec2i windowSize) override;
    void Update(double dt) override;
    void DrawOverlay(double dt) override;
    // The global-map interaction system delegates the same camera gestures
    // used by the decision tree to these methods. Keeping them on the widget
    // makes drawing and hit testing share one transform.
    void AdjustMapZoom(Vec2i point, float wheel);
    void BeginPanning();
    void EndPanning();
    void HandleLeftClick();

    GameScene* scene{nullptr};
    ProvinceId selectedProvinceId{InvalidProvinceId};
    ProvinceConnectionId selectedConnectionId{InvalidProvinceConnectionId};

private:
    enum class OperationDialogKind : std::uint8_t
    {
        None,
        Trade,
        Attack,
        Colonize,
        ResourceTransfer,
        ArmyTransfer
    };

    enum class ProvinceAction : std::uint8_t
    {
        Scout,
        Trade,
        Attack,
        Colonize,
        ResourceTransfer,
        ArmyTransfer
    };

    Rectangle CanvasRect() const;
    Vector2 MapOrigin() const;
    float MapScale() const;
    Rectangle ProvinceTooltipRect(const GlobalMapNodeView& selected, Vector2 origin,
                                  float scale) const;
    Rectangle ProvinceActionRect(Rectangle tooltip, std::size_t actionIndex) const;
    Rectangle ScoutCountButtonRect(Rectangle tooltip, bool increment) const;
    Rectangle ScoutFoodButtonRect(Rectangle tooltip, bool increment) const;
    Rectangle ScoutConfirmButtonRect(Rectangle tooltip) const;
    Rectangle ScoutCancelButtonRect(Rectangle tooltip) const;
    Rectangle RouteTooltipRect(const GlobalMapView& view, const ProvinceEdgeView& selected,
                               Vector2 origin, float scale) const;
    Rectangle OperationDialogRect() const;
    Rectangle OperationCloseButtonRect() const;
    Rectangle OperationConfirmButtonRect() const;
    std::vector<ProvinceAction> AvailableProvinceActions(const GlobalMapNodeView& selected) const;
    std::vector<int> AvailableScoutIds(ProvinceId sourceProvinceId) const;
    const JourneyStatusView* FindLatestScoutJourney(ProvinceId targetProvinceId,
                                                     bool activeOnly) const;
    const JourneyStatusView* FindLatestActiveJourney(ProvinceId targetProvinceId) const;
    bool IsNodeAnchorVisible(const GlobalMapNodeView& node, Vector2 origin,
                             float scale) const;
    void DrawParchmentBackground(Rectangle bounds, Vector2 anchor,
                                 float textureScale = 1.0f) const;
    void EnsureFogResources(Rectangle canvas);
    void DrawFogOfWar(const GlobalMapView& view, Vector2 origin, float scale,
                      Rectangle canvas);
    void DrawCanvas(const GlobalMapView& view, Vector2 origin, float scale);
    void DrawProvinceTooltip(const GlobalMapNodeView* selected, Vector2 origin,
                             float scale) const;
    void DrawRouteTooltip(const GlobalMapView& view, const ProvinceEdgeView* selectedEdge,
                          Vector2 origin, float scale) const;
    void DrawOperationDialog(const GlobalMapView& view);
    void HandleInput(const GlobalMapView& view, Vector2 origin, float scale,
                     const GlobalMapNodeView* selected,
                     const ProvinceEdgeView* selectedEdge);

    std::array<tvorin::ui::TextureHandle, 6> provinceTextures{};
    std::array<tvorin::ui::TextureHandle, 6> provinceHoverTextures{};
    std::array<tvorin::ui::TextureHandle, 4> globalMapBackgrounds{};
    tvorin::ui::TextureHandle globalMapFogRevealTexture{};
    tvorin::ui::RenderTextureHandle globalMapFogMask{};
    ShaderLibrary globalMapShaders{};
    Vec2i fogMaskSize{};

    Vec2f mapPanOffset{0.0f, 0.0f};
    float mapZoom{1.0f};
    bool panning{false};
    Vec2f lastPanMouse{0.0f, 0.0f};

    OperationDialogKind operationDialog{OperationDialogKind::None};
    ProvinceId operationTargetProvinceId{InvalidProvinceId};
    ProvinceId operationOriginProvinceId{InvalidProvinceId};
    ProvinceId scoutSetupProvinceId{InvalidProvinceId};
    ProvinceId scoutPendingProvinceId{InvalidProvinceId};
    std::uint64_t scoutPendingCommandId{0};
    std::string scoutPendingFailureReason;
    int selectedScoutCount{1};
    int scoutFoodDraft{0};
    mutable int scoutMinimumFoodDraft{1};
    std::vector<TaskGroupId> selectedOperationTaskGroups;
    std::map<ResourceType, int> resourceTransferDraft;
    int destinationBarracksBuildingId{0};
    int attackFoodDraft{0};
    int attackSwordDraft{0};
};

// Compact, collapsible navigation for players who own more than one local
// province. The widget only requests a presentation switch; it never changes
// province simulation state or issues gameplay commands.
class OwnedProvinceListWidget : public UiWidget
{
public:
    void UpdateSize(Vec2i windowSize) override;
    void Update(double dt) override;
    bool CapturesPointer(Vector2 point) const;

    GameScene* scene{nullptr};
    CampaignStatusWidget* journal{nullptr};
    bool expanded{false};
    float scrollOffset{0.0f};
    float maxScrollOffset{0.0f};
    std::map<ProvinceId, WorldEventInstanceId> alertAcknowledgedEventIds;
};

// Read-only campaign alert/status card. It consumes only the pointer-free
// presentation snapshot, so displaying an event or battle can never mutate
// authority state or pause a multiplayer simulation.
class CampaignStatusWidget : public UiWidget
{
public:
    void UpdateSize(Vec2i windowSize) override;
    void Update(double dt) override;
    bool CapturesPointer(Vector2 point) const;

    GameScene* scene{nullptr};
    bool journalExpanded{false};
    WorldEventInstanceId newestSeenEventId{InvalidWorldEventInstanceId};
    double headlineVisibleUntil{0.0};
    float scrollOffset{0.0f};
    float maxScrollOffset{0.0f};
    std::set<WorldEventInstanceId> expandedEventIds;
};

// Owns the two campaign cards so their dynamic heights and the bottom inset
// are coordinated in one layout pass. The cards remain separate widgets for
// their content and input logic, but the sidebar is the only gameplay widget
// registered with the renderer.
class CampaignSidebarWidget : public UiWidget
{
public:
    void UpdateSize(Vec2i windowSize) override;
    void Update(double dt) override;
    bool CapturesPointer(Vector2 point) const;

    OwnedProvinceListWidget* provinces{nullptr};
    CampaignStatusWidget* journal{nullptr};
    Vec2i windowSize{};
};

class GlobalMapGuiSystem : public GuiSystem
{
public:
    explicit GlobalMapGuiSystem(GuiController* con);
    GlobalMapGuiSystem() = delete;

    void Update(double dt) override;
    void UpdateUiWidgets(Vec2i size) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void StockpilePressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();
    bool CanActivate() override;

    GameScene* scene{nullptr};
    GlobalMapPanelWidget panel;

private:
    CameraMovement cameraMovement;
};

// Full-screen economy and strategic statistics overview.
class StatsPanelWidget : public UiWidget
{
public:
    void Update(double dt) override;
    bool HandleClick(Vec2i point);

    GameScene* scene{nullptr};
    int selectedWindowIndex{0};
    int selectedFlowMode{0};

private:
    // Layout rectangles shared by Update (drawing) and HandleClick (hit tests)
    // so both always agree on where the controls are.
    Rectangle GetWindowSpinnerRect() const;
    Rectangle GetFlowModeToggleRect() const;
    Rectangle GetChartRect() const;
    Rectangle GetFilterButtonRect(Rectangle chart, int index) const;
    Rectangle GetAllFilterButtonRect(Rectangle chart) const;

    std::set<ResourceType> selectedResources;
    std::vector<ResourceType> filterResources;
};

// Aggregated warehouse-network stockpile with per-warehouse hover details.
class StockpilePanelWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
    float scrollOffset{0.0f};
    float maxScrollOffset{0.0f};
    bool scrollbarDragging{false};
    float scrollbarDragOffset{0.0f};
    void Scroll(float wheel);

private:
    // Grid geometry shared by drawing and hover hit-testing.
    Rectangle GetGridRect() const;
};

// Which research tree a ResearchTreePanelWidget renders.
enum class ResearchTreeKind
{
    Focus,
    Technology,
};

// Full-screen research tree (shared by political focuses and technologies).
// The two trees differ only in data source, the focus-only state side panel and
// what happens when an available node is clicked.
class ResearchTreePanelWidget : public UiWidget
{
public:
    explicit ResearchTreePanelWidget(ResearchTreeKind kind) : kind(kind) {}
    void Update(double dt) override;
    void AdjustTreeZoom(Vec2i point, float wheel);

    GameScene* scene{nullptr};
    ResearchTreeKind kind{ResearchTreeKind::Technology};
    float scrollOffset{0.0f};
    float maxScrollOffset{0.0f};
    Vec2f panOffset{0.0f, 0.0f};
    float zoom{0.90f};
    bool panning{false};
    Vec2f lastPanMouse{0.0f, 0.0f};
    std::string selectedTagFilter;

private:
    // Scrollable tree viewport; the focus tree reserves space for the state panel.
    Rectangle GetTreeArea(Rectangle bounds) const;
};

// Right-side build panel with selectable building cards.
class BuildPanelWidget : public UiWidget
{
public:
    // Draws available build options and handles hover visuals.
    void Update(double dt) override;
    // Rebuilds the signed tab list from the data-driven option categories.
    void RefreshCategoryTabs();
    // Selects a tab by its visible order.
    void SelectCategory(size_t index);
    // Returns the tab index under a screen point, or -1 when none is hit.
    int GetTabAt(Vec2i point) const;
    // Scrolls the build option list.
    void Scroll(float wheel);
    // Applies a direct scrollbar offset and persists it for the active tab.
    void SetScrollOffset(float offset);
    // Returns option index under a point, or -1 when none is hit.
    int GetOptionAt(Vec2i point) const;

    GameScene* scene{nullptr};
    std::vector<BuildOption>* options{nullptr};
    size_t selectedIndex{std::numeric_limits<size_t>::max()};
    Vec2i hoveredTile{-1, -1};
    std::string title{"Build"};
    float scrollOffset{0.0f};
    float maxScrollOffset{0.0f};
    std::vector<std::string> categoryTabs;
    std::map<std::string, float> categoryScrollOffsets;
    std::string activeCategory;
    bool dragging{false};
    Vec2i dragOffset{0, 0};
    bool scrollbarDragging{false};
    float scrollbarDragOffset{0.0f};
};

// Roster panel for recruited units. Expedition controls are handled by the
// global map layer and are intentionally absent from the local-map panel.
class RosterPanelWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
    TaskGroupId selectedTaskGroupId{InvalidTaskGroupId};
    int selectedBarracksId{0};
};

// ─── Interaction systems ─────────────────────────────────────────────────────

// Default map interaction mode for selection, camera and logistics assignment.
class BasicMapViewSystem : public GuiSystem
{
public:
    explicit BasicMapViewSystem(GuiController* con);
    BasicMapViewSystem() = delete;

    // Rebuilds map-view widget list.
    void UpdateUiWidgets(Vec2i) override;

    // Opens or closes the in-game menu.
    void EscPressed();
    // Enters building placement mode.
    void BuildPressed();
    // Enters road placement mode.
    void RoadBuildPressed();
    void DestroyPressed();
    void StockpilePressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void CenterOnHeadquartersPressed();

    // Handles map selection and panel interactions.
    void SelectBuilding(Building* building);
    void LmbPressed();
    // Stops camera drag initiated by left mouse button when relevant.
    void LmbReleased();
    // Starts camera drag or assigns selected building receiver.
    void RmbPressed();
    // Stops camera drag.
    void RmbReleased();
    // Zooms camera around cursor.
    void Scroll();

    // Updates camera drag and visible widgets.
    void Update(double dt) override;

    // Clears building selection so the info/research panels (and any
    // InputEventSubscriber they own, e.g. GuiPanel::escClose) stop reacting
    // the moment another GuiSystem becomes active — see GuiSystem::OnDeactivate.
    void OnDeactivate() override;

    GameScene* scene{nullptr};
    CameraMovement cameraMovement;

    BuildingInfoPanel buildingInfoPanel;
    ResearchPanel researchPanel;
    SelectedBuildingWidget selectedBuildingWidget;
    ProductionWarningWidget productionWarningWidget;
    BuildingHoverTooltipWidget buildingHoverTooltipWidget;
    StrategicResourceHudWidget strategicHudWidget;

    bool isBuildingSelected{false};

private:
    // Returns the research panel when it holds a building, else the info panel.
    GuiPanel* ActivePanel();
    // Clears building selection and both side panels.
    void ClearBuildingSelection();
};

// Build interaction mode for placeable buildings.
class BuildGuiSystem : public GuiSystem
{
public:
    explicit BuildGuiSystem(GuiController* con);
    BuildGuiSystem() = delete;

    GameScene* scene{nullptr};

    // Rebuilds build-mode widget list.
    void UpdateUiWidgets(Vec2i) override;
    void OnActivate() override;
    void OnDeactivate() override;
    // Updates camera drag, build panel and ghost preview.
    void Update(double dt) override;

    // Cancels build mode and returns to map view.
    virtual void EscPressed();
    // Toggles build mode.
    virtual void BuildPressed();
    // Toggles road build mode.
    virtual void RoadBuildPressed();
    virtual void DestroyPressed();
    virtual void StockpilePressed();
    virtual void StatsPressed();
    virtual void FocusPressed();
    virtual void TechPressed();
    virtual void RosterPressed();
    // Selects build option or places selected building.
    virtual void LmbPressed();
    // Stops left-button action.
    virtual void LmbReleased();
    // Starts camera drag.
    virtual void RmbPressed();
    // Stops camera drag.
    virtual void RmbReleased();
    // Zooms camera around cursor.
    virtual void Scroll();

protected:
    // Switches controller back to default map view.
    void ReturnToMapView();
    // Returns map tile currently under cursor.
    Vec2i GetHoveredTile() const;
    // Returns true when selected building can be placed at tile.
    bool CanPlaceSelected(Vec2i tilePos) const;
    // Selects a build option by index and refreshes preview.
    void SelectOption(size_t index);
    // Rebuilds ghost preview for the selected option.
    void RefreshGhost();
    // Places selected option under cursor when placement is valid.
    bool TryPlaceSelectedAtHovered();
    void ResetBuildInteraction();

    CameraMovement cameraMovement;
    BuildPanelWidget buildPanel;
    StrategicResourceHudWidget strategicHudWidget;
    std::vector<BuildOption> options;
    BuildInteractionState interactionState{BuildInteractionState::Browse};
    size_t selectedIndex{std::numeric_limits<size_t>::max()};
    std::unique_ptr<Building> selectedPreview;
    BuildGhostWidget ghostWidget;
};

// Specialized build mode that only places roads.
class RoadBuildSystem : public BuildGuiSystem
{
public:
    explicit RoadBuildSystem(GuiController* con);
    RoadBuildSystem() = delete;

    // Updates camera drag, road panel, ghost preview and drag placement.
    void Update(double dt) override;

    // Switches back to building placement mode.
    void BuildPressed() override;
    // Cancels road placement mode.
    void RoadBuildPressed() override;
    // Selects road option or starts placing roads.
    void LmbPressed() override;
    // Ends road drag placement.
    void LmbReleased() override;
    void Scroll() override;

private:
    bool TryPlaceRoadTowards(Vec2i tilePos);
    bool TryPlaceRoadAt(Vec2i tilePos);
    // Road mode has no visible option panel (Update never draws buildPanel),
    // so it keeps the sole road option selected while dragging.
    void SyncSelectionToTile(Vec2i tilePos);
    Vec2i GetRoadPlacementTile(double dt);

    Vec2i lastRoadDragTile{-9999, -9999};
    RoadDragStabilizer dragStabilizer;
};

class DestroyGuiSystem : public GuiSystem
{
public:
    explicit DestroyGuiSystem(GuiController* con);
    DestroyGuiSystem() = delete;

    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void StockpilePressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    void ReturnToMapView();
    // Drops the hover target before leaving destroy mode.
    void ClearHoverTarget();

    CameraMovement cameraMovement;
    DemolitionTargetWidget destroyTargetWidget;
    DemolitionTooltipWidget destroyTooltipWidget;
    Building* hoveredBuilding{nullptr};
    DemolitionPreview demolitionPreview;
    StrategicResourceHudWidget strategicHudWidget;
};

// Mass-upgrade interaction mode. It submits one existing upgrade command per
// visited building, preserving deterministic gesture order.
class UpgradeGuiSystem : public GuiSystem
{
public:
    explicit UpgradeGuiSystem(GuiController* con);
    UpgradeGuiSystem() = delete;

    void UpdateUiWidgets(Vec2i) override;
    void OnActivate() override;
    void OnDeactivate() override;
    void Update(double dt) override;
    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void StockpilePressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void UpgradePressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

    GameScene* gameScene{nullptr};
    GameScene* scene{nullptr};

private:
    Building* GetHoveredBuilding() const;
    bool CanUpgrade(Building* building) const;
    void SubmitUpgrade(Building* building);
    void SubmitUpgradeLine(Vec2i tile);
    void ReturnToMapView();

    CameraMovement cameraMovement;
    StrategicResourceHudWidget strategicHudWidget;
    UpgradeTargetWidget targetWidget;
    Building* hoveredBuilding{nullptr};
    UpgradeGestureTargets visitedBuildingIds;
    Vec2i lastGestureTile{-9999, -9999};
    bool gestureActive{false};
};

class StatsGuiSystem : public GuiSystem
{
public:
    explicit StatsGuiSystem(GuiController* con);
    StatsGuiSystem() = delete;

    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void StockpilePressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    CameraMovement cameraMovement;
    StatsPanelWidget statsPanel;
    StrategicResourceHudWidget strategicHudWidget;
};

class FocusGuiSystem : public GuiSystem
{
public:
    explicit FocusGuiSystem(GuiController* con);
    FocusGuiSystem() = delete;

    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void StockpilePressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    CameraMovement cameraMovement;
    ResearchTreePanelWidget focusPanel{ResearchTreeKind::Focus};
    StrategicResourceHudWidget strategicHudWidget;
};

class TechGuiSystem : public GuiSystem
{
public:
    explicit TechGuiSystem(GuiController* con);
    TechGuiSystem() = delete;

    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    // The tech tree only opens once the local player has a completed
    // University — domain gate moved here from GuiController::ChangeSystem.
    bool CanActivate() override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void StockpilePressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    CameraMovement cameraMovement;
    ResearchTreePanelWidget techPanel{ResearchTreeKind::Technology};
    StrategicResourceHudWidget strategicHudWidget;
};

// Roster full-screen mode — opened via the strategic HUD's Roster button or
// the U key, following the same recipe as FocusGuiSystem/TechGuiSystem.
class RosterGuiSystem : public GuiSystem
{
public:
    explicit RosterGuiSystem(GuiController* con);
    RosterGuiSystem() = delete;

    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void StockpilePressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();
    bool CanActivate() override;

private:
    CameraMovement cameraMovement;
    RosterPanelWidget rosterPanel;
    StrategicResourceHudWidget strategicHudWidget;
};

// Stockpile full-screen mode — opened with E, following the same recipe as
// StatsGuiSystem/RosterGuiSystem.
class StockpileGuiSystem : public GuiSystem
{
public:
    explicit StockpileGuiSystem(GuiController* con);
    StockpileGuiSystem() = delete;

    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void StockpilePressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    CameraMovement cameraMovement;
    StockpilePanelWidget stockpilePanel;
    StrategicResourceHudWidget strategicHudWidget;
};

#endif
