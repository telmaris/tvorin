#ifndef SCENES_H
#define SCENES_H

#include "scenes/GameWindow.h"
#include "core/GameSession.h"
#include "ui/Gui.h"
#include "ui/GuiHandler.h"
#include "ui/Input.h"
#include "ui/GuiController.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class TcpGameTransport;
class GameScene;

// Startup splash shown before the main menu with the studio logo and wordmark.
class StudioSplashScene : public Scene
{
public:
    StudioSplashScene();
    ~StudioSplashScene() override;

    void Update(double dt) override;
    void OnActivated() override;
    void OnDeactivated() override;

private:
    double elapsed{0.0};
    bool transitionRequested{false};
    tvorin::ui::TextureHandle studioLogo{};
};

// Strategy object for mode-specific gameplay update/render details.
class IGameRuntimeLoop
{
    public:
        virtual ~IGameRuntimeLoop() = default;
        virtual void Update(GameScene& scene, double dt) = 0;
        virtual std::uint64_t SubmitCommand(const GameCommand& command) = 0;
        virtual std::vector<GameCommandResult> ConsumeCommandResults() = 0;
        virtual bool IsConnectionClosed() const = 0;
        virtual std::string GetConnectionStatus() const = 0;
        virtual std::recursive_mutex* GetWorldMutex() = 0;
        virtual bool ShouldPauseWhenSceneInactive() const = 0;
        virtual void SetPaused(bool paused) = 0;
        virtual bool IsPaused() const = 0;
};

// Main menu scene with textured background and primary navigation buttons.
class MainMenuScene : public Scene, public IGuiHandler
{
    public:

    MainMenuScene();
    ~MainMenuScene() override;

    // Updates main menu widgets.
    void Update(double dt) override;
    // Starts the menu music theme.
    void OnActivated() override;
    // Main menu has no back destination — ESC does nothing here.
    void HandleGuiInput(double dt) override {}

    // Opens the new game scene.
    void OnNewGamePressed();
    // Opens the multiplayer scene.
    void OnMultiplayerPressed();
    // Opens the load game scene.
    void OnLoadGamePressed();
    // Opens the options scene.
    void OnOptionsPressed();
    // Opens the controls reference scene.
    void OnControlsPressed();
    // Requests application shutdown.
    void OnQuitPressed();

    // Handles resize and scene navigation events.
    void HandleEvent(std::shared_ptr<Event>) override;

    VBox buttonsColumn;
    UiImage menuGraphic;
    UiImage menuSkyBg;
    FuncWidget menuCloudsBgParallax;
    UiImage menuGrassBg;
    UiImage menuVillageBg;
    UiAnimation menuTvorinLogo;
    UiLabel statusLabel;
    double statusTimer{0.0};

private:
    tvorin::ui::TextureHandle menuCloudTexture{};
    double menuCloudScroll{0.0};
};

// Options scene for display and audio preferences.
class OptionsScene : public Scene, public IGuiHandler
{
    public:

        OptionsScene();
        // Updates options widgets.
        void Update(double dt) override;
        // Syncs slider to current audio volume.
        void OnActivated() override;
        // Handles resize and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;
        // ESC returns to the previous scene.
        void HandleGuiInput(double dt) override { HandleBackNavigation(); }
        void OnNavigateBack() override { OnBackPressed(); }

        // Returns to the previous scene.
        void OnBackPressed();

        UiButton backButton;
        UiParallaxBackground menuBackground;
        UiPanel menuPanel;
        CheckBox fullScreenCheckBox;
        SliderBar masterVolume;
        SliderBar musicVolume;
          SliderBar sfxVolume;
          CheckBox colorGradingCheckBox;
          CheckBox retroFilterCheckBox;
          CheckBox localLightBloomCheckBox;
          CheckBox rainOverlayCheckBox;
          CheckBox logisticsOverlayCheckBox;
  };

// New game form scene.
class NewGameScene : public Scene, public IGuiHandler
{
    public:

        NewGameScene();
        // Updates new game form widgets.
        void Update(double dt) override;
        // Handles resize and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;
        // ESC returns to the main menu.
        void HandleGuiInput(double dt) override { HandleBackNavigation(); }
        void OnNavigateBack() override { OnBackPressed(); }

        // Returns to the previous scene.
        void OnBackPressed();
        // Starts a new generated world.
        void OnStartPressed();
        // Starts the generated world in the scripted tutorial scene.
        void OnTutorialPressed();
        // Cycles map size preset.
        void OnSizePressed();
        // Refreshes option labels from current values.
        void RefreshOptionLabels();
        // Clears legacy AI settings from newly created single-player games.
        static void ApplySinglePlayerParameters(MapParameters& params);
        MapParameters BuildMapParameters();
        CampaignGenerationParameters BuildCampaignParameters();

        UiButton backButton;
        UiParallaxBackground menuBackground;
        UiPanel menuPanel;
        UiButton sizeButton;
        SliderBar resourceDensity;
        SliderBar resourceFieldSize;
        SliderBar resourceRichness;
        SliderBar globalProvinceCount;
        SliderBar globalProvinceSpacing;
        SliderBar globalBuildableWeight;
        SliderBar globalCityWeight;
        SliderBar globalBanditWeight;
        SliderBar globalEventWeight;
        SliderBar globalBuildableWealth;
        SliderBar globalCityWealth;
        SliderBar globalBanditStrength;
        SliderBar globalMapFogOfWar;
        CheckBox debugMode;
        UiButton startGame;
        UiButton startTutorial;
        FuncWidget tooltipWidget;
        MapSizePreset selectedSize{MapSizePreset::S};
    };

// LAN multiplayer entry scene.
class MultiplayerScene : public Scene, public IGuiHandler
{
    public:

        MultiplayerScene();
        // Updates multiplayer form widgets.
        void Update(double dt) override;
        // Handles resize and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;
        // ESC closes the settings sub-panel first, then returns to the menu.
        void HandleGuiInput(double dt) override { HandleBackNavigation(); }
        void OnNavigateBack() override
        {
            if (showGameSettings)
                OnCloseGameSettingsPressed();
            else
                OnBackPressed();
        }

        // Returns to the main menu.
        void OnBackPressed();
        // Starts a local host session.
        void OnHostPressed();
        // Joins a host by IPv4 address.
        void OnJoinPressed();
        // Starts the hosted lobby game.
        void OnStartPressed();
        // Opens host-only multiplayer world settings.
        void OnGameSettingsPressed();
        // Closes host-only multiplayer world settings.
        void OnCloseGameSettingsPressed();
        // Cycles hosted game size preset.
        void OnMultiplayerSizePressed();
        // Cycles host-controlled global campaign layout preset.
        void OnMultiplayerGlobalLayoutPressed();
        // Cycles host-controlled global province type weights.
        void OnMultiplayerGlobalTypeWeightsPressed();
        // Cycles host-controlled global province value scales.
        void OnMultiplayerGlobalValueScalesPressed();
        // Sends one lobby chat message.
        void OnSendChatPressed();
        void AddLobbyLine(const std::string& line, Color color = Color{190, 205, 224, 255});
        void ResetLobby();
        void UpdateLobbyMessages(double dt);
        void DrawConnectionDialog() const;
        void DrawLobbyLog() const;
        void DrawLobbyPlayerPanels() const;
        void DrawGameSettingsPanel() const;
        void RefreshMultiplayerLabels();
        void SaveMultiplayerSettings() const;
        CampaignGenerationParameters BuildLobbyMapParameters() const;
        void BroadcastLobbyState(const std::string& infoMessage = "");
        bool ApplyLobbyState(const std::string& payload);
        void MaybeBroadcastSettingsChange(const std::string& infoMessage = "");

        UiButton backButton;
        UiLabel nicknameLabel;
        TextBox nickname;
        UiLabel sessionNameLabel;
        TextBox sessionName;
        UiLabel addressLabel;
        TextBox address;
        UiLabel portLabel;
        TextBox port;
        UiButton hostButton;
        UiButton joinButton;
        UiButton startButton;
        UiButton gameSettingsButton;
        UiButton closeSettingsButton;
        UiButton multiplayerSizeButton;
        UiButton multiplayerGlobalLayoutButton;
        UiButton multiplayerGlobalTypeWeightsButton;
        UiButton multiplayerGlobalValueScalesButton;
        SliderBar multiplayerResourceDensity;
        SliderBar multiplayerResourceFieldSize;
        SliderBar multiplayerResourceRichness;
        CheckBox multiplayerDebugMode;
        TextBox chatInput;
        UiButton sendChatButton;
        std::shared_ptr<TcpGameTransport> lobbyTransport;
        std::vector<std::pair<std::string, Color>> lobbyLines;
        std::string lobbyNickname{"Player"};
        std::string remoteLobbyNickname;
        std::string lobbySessionName{"lan_test"};
        std::string lobbyAddress{"127.0.0.1"};
        unsigned short lobbyPort{27015};
        MapSizePreset lobbySizePreset{MapSizePreset::S};
        int lobbyGlobalLayoutPreset{1};
        int lobbyGlobalTypeWeightsPreset{0};
        int lobbyGlobalValueScalesPreset{0};
        bool showGameSettings{false};
        bool isLobbyHost{false};
        bool lobbyActive{false};
        bool connectingToLobby{false};
        bool announcedConnection{false};
        bool hasRemoteLobbyPlayer{false};
        int lobbyChatScroll{0};
        double connectionWaitTimer{0.0};
        double connectionMessageTimer{0.0};
        std::string connectionMessage;
        std::string lastBroadcastLobbyState;
};

// Save selection scene used to load existing games.
class LoadGameScene : public Scene, public IGuiHandler
{
    public:

        LoadGameScene();
        // Updates save-list widgets.
        void Update(double dt) override;
        // Handles resize, save-list and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;
        // ESC returns to the previous scene.
        void HandleGuiInput(double dt) override { HandleBackNavigation(); }
        void OnNavigateBack() override { OnBackPressed(); }

        // Returns to the previous scene.
        void OnBackPressed();
        // Rebuilds the visible save-file list.
        void LoadSaves();
        // Emits a load request for the selected save.
        void OnSavePressed(std::string);

        UiButton backButton;
        UiParallaxBackground menuBackground;
        VBox saveButtons;
};

// Save selection scene used to create or overwrite save files.
class SaveGameScene : public Scene, public IGuiHandler
{
    public:

        SaveGameScene();
        // Updates save form, save list and overwrite confirmation.
        void Update(double dt) override;
        // Handles resize, save-list and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;
        // ESC cancels a pending overwrite prompt first, then goes back.
        void HandleGuiInput(double dt) override { HandleBackNavigation(); }
        void OnNavigateBack() override
        {
            if (overwriteConfirmationVisible)
                OnCancelOverwrite();
            else
                OnBackPressed();
        }

        // Returns to the previous scene.
        void OnBackPressed();
        // Rebuilds the visible save-file list.
        void LoadSaves();
        // Selects an existing save for overwrite confirmation.
        void OnSavePressed(std::string);
        // Saves into the name typed in the text box.
        void OnNewSavePressed();
        // Confirms overwriting the selected save.
        void OnConfirmOverwrite();
        // Cancels overwrite confirmation.
        void OnCancelOverwrite();

        UiButton backButton;
        TextBox saveName;
        UiButton newSaveButton;
        VBox saveButtons;
        UiButton confirmOverwriteButton;
        UiButton cancelOverwriteButton;
        std::string pendingOverwriteSave;
        bool overwriteConfirmationVisible{false};
};

// Main-thread interstitial. HostSession generates on its simulation worker;
// ClientSession receives/restores the host snapshot on its session worker.
class LoadingScene : public Scene, public IGuiHandler
{
public:
    LoadingScene() = default;
    ~LoadingScene() override;

    void Update(double dt) override;
    void OnActivated() override;
    void OnDeactivated() override;
    void HandleEvent(std::shared_ptr<Event> event) override;
    void HandleGuiInput(double dt) override;

private:
    enum class Mode
    {
        SinglePlayer,
        MultiplayerHost,
        MultiplayerClient
    };

    struct LaunchRequest
    {
        Mode mode{Mode::SinglePlayer};
        std::string targetScene{"GameScene"};
        std::string worldName;
        CampaignGenerationParameters params;
        std::string address;
        unsigned short port{27015};
        std::shared_ptr<IGameTransport> transport;
    };

    void StartPendingSession();
    void CancelAndReturn(const std::string& reason);
    void DrawLoadingFrame(const GameSessionStartupStatus& status);

    std::optional<LaunchRequest> pendingRequest;
    std::unique_ptr<IGameSession> session;
    GameSessionStartupStatus displayedStatus;
    Mode activeMode{Mode::SinglePlayer};
    std::string targetScene{"GameScene"};
    std::string returnScene{"MainScene"};
    double visibleElapsed{0.0};
    float displayedProgress{0.01f};
    bool transitionRequested{false};
    bool cursorWasVisible{true};
    bool cursorHidden{false};
};

// Live gameplay scene observing the world owned by its active session.
class GameScene : public Scene, public IGuiHandler
{
    public:

        GameScene();
        ~GameScene() override;
        // Advances input, world simulation and rendering.
        void Update(double dt) override;
        void OnActivated() override;
        void OnDeactivated() override;
        void OnWindowFocusChanged(bool focused) override;
        // Handles game lifecycle and menu events.
        void HandleEvent(std::shared_ptr<Event>) override;
        // Routes gameplay input through this scene's InputProcessor into the
        // GuiController (which dispatches to the active interaction system —
        // ESC handling included, via BasicMapViewSystem::EscPressed etc.).
        void HandleGuiInput(double dt) override { inputs.HandleInputs(); }


        // Loads a save file into the gameplay scene.
        bool LoadGame(std::string);
        // Saves the current gameplay state.
        bool SaveGame(std::string saveName = "");
        // Sends a local player's intent to the active session authority.
        std::uint64_t SubmitLocalCommand(const GameCommand& command);
        // Returns command results received from the active session.
        std::vector<GameCommandResult> ConsumeCommandResults();
        // Clears the active runtime and closes owned network transports.
        void ShutdownActiveGame();
        // Repositions the small multiplayer diagnostics label.
        void UpdateNetworkStatusWidget(Vec2i windowSize);
        // Local visual-only shortcuts. They never submit a game command.
        virtual void HandleRenderDebugInput();
        virtual bool AreTutorialHudButtonsLocked() const { return false; }
        virtual bool AreTutorialDestroyLocked() const { return AreTutorialHudButtonsLocked(); }
        virtual bool AreTutorialDecisionsLocked() const { return AreTutorialHudButtonsLocked(); }
        virtual bool AreTutorialStatisticsLocked() const { return AreTutorialHudButtonsLocked(); }
        virtual void AppendGameplayWidgets(std::vector<UiWidget*>& widgets);
        virtual void PrepareGameplayRender() {}
        // Modal hooks used by gameplay-derived scenes without duplicating the
        // runtime/render loop.
        virtual bool HasBlockingPopup() const { return false; }
        virtual UiWidget* GetBlockingPopupWidget() { return nullptr; }

        GameWorld* game{nullptr};
        std::unique_ptr<IGameRuntimeLoop> runtimeLoop{nullptr};
        std::unique_ptr<GuiController> controller{nullptr};
        InputProcessor inputs;
        UiLabel networkStatusLabel;
        GameSnapshot latestSnapshot;
        std::vector<GameCommandResult> commandResults;
        OwnedProvinceListWidget ownedProvinceList;
        CampaignStatusWidget campaignStatus;
        CampaignSidebarWidget campaignSidebar;
        std::size_t prevUnlockedTechCount{0};
        std::size_t prevUnlockedFocusCount{0};

    private:
        void AdoptPreparedSession(std::unique_ptr<IGameSession> session,
                                  bool multiplayerClient,
                                  bool usedFallback);
};

// Scripted single-player scene. It reuses GameScene wholesale and drives a
// data-defined sequence of modal steps from generic tutorial triggers.
class TutorialScene : public GameScene
{
public:
    TutorialScene();

    void OnActivated() override;
    void Update(double dt) override;
    void HandleEvent(std::shared_ptr<Event> e) override;
    void HandleRenderDebugInput() override;
    void PrepareGameplayRender() override;
    bool AreTutorialHudButtonsLocked() const override { return tutorialHudLocked; }
    bool AreTutorialDestroyLocked() const override { return tutorialHudLocked || tutorialDecisionsUnlocked; }
    bool AreTutorialDecisionsLocked() const override { return !tutorialDecisionsUnlocked; }
    bool AreTutorialStatisticsLocked() const override { return tutorialHudLocked || tutorialDecisionsUnlocked; }
    void AppendGameplayWidgets(std::vector<UiWidget*>& widgets) override
    {
        GameScene::AppendGameplayWidgets(widgets);
        widgets.push_back(&tutorialTasks);
    }
    bool HasBlockingPopup() const override { return tutorialPopup.IsVisible(); }
    UiWidget* GetBlockingPopupWidget() override { return &tutorialPopup; }

private:
    void ShowPopupForTrigger(TutorialTriggerType trigger, BuildingType buildingType = BuildingType::Building);
    void OnPopupDismissed();
    void EmitTrigger(TutorialTriggerType trigger, BuildingType buildingType = BuildingType::Building);
    void OpenWoodcutterPanelAndHighlights();
    void ClearTutorialHighlights();
    void SetTutorialHudLock(bool locked);
    void UnlockTutorialDecisions();
    void UpdateTutorialTasks();
    void FinishPendingTutorialStart();

    PopupWindowWidget tutorialPopup;
    TutorialTriggerType activePopupTrigger{TutorialTriggerType::None};
    bool awaitingWoodcutter{false};
    bool woodcutterCompletionReported{false};
    bool awaitingRoadConnection{false};
    bool roadConnectionReported{false};
    bool awaitingBasicProduction{false};
    bool basicProductionReported{false};
    bool foodChainReported{false};
    bool decisionSelectedReported{false};
    bool awaitingRecruitment{false};
    bool recruitmentReported{false};
    struct PendingTutorialStart
    {
        std::string name;
        CampaignGenerationParameters params;
    };
    std::optional<PendingTutorialStart> pendingTutorialStart;
    bool tutorialHudLocked{false};
    bool tutorialDecisionsUnlocked{false};
    bool taskCameraBaselineSet{false};
    bool taskBuildModeSeen{false};
    Vec2f taskCameraStart{};
    TutorialTaskWidget tutorialTasks;
};

// Static controls reference scene shown from the main menu.
class ControlsScene : public Scene, public IGuiHandler
{
    public:

        ControlsScene();
        // Draws the controls reference card.
        void Update(double dt) override;
        // Handles resize and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;
        // ESC returns to the main menu.
        void HandleGuiInput(double dt) override { HandleBackNavigation(); }
        void OnNavigateBack() override { OnBackPressed(); }

        // Returns to the main menu.
        void OnBackPressed();

        UiButton backButton;
};

class GameMenuScene;

// Single-system interaction mode for GameMenuScene (A4 pilot,
// docs/work_plan_2026-07-13.md): establishes the GuiController pattern for a
// menu scene that only ever has one screen — nothing to switch between, so
// this system's whole job is to surface the owning scene's VBox and route
// "esc" to OnBackPressed, the same shape GameScene uses for its 8 modes.
class MenuNavSystem : public GuiSystem
{
public:
    explicit MenuNavSystem(GuiController* con);
    MenuNavSystem() = delete;

    void Update(double dt) override;
    void UpdateUiWidgets(Vec2i size) override;

    // Shadows GuiSystem::scene (Scene*) with the concrete scene this system
    // actually needs, same pattern as GameScene's interaction systems.
    GameMenuScene* menuScene{nullptr};
};

// In-game pause/menu scene.
class GameMenuScene : public Scene, public IGuiHandler
{
    public:

        GameMenuScene();
        // Updates menu widgets.
        void Update(double dt) override;
        // Handles resize and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;
        // Routes input through this scene's own GuiController + MenuNavSystem
        // (A4 pilot, docs/work_plan_2026-07-13.md) — same shape as GameScene,
        // even though there's only one system. The IGuiHandler input gate
        // (reset centrally on every scene activation) still wraps this call,
        // so the old ESC ping-pong class of bug stays fixed at the source.
        void HandleGuiInput(double dt) override { inputs.HandleInputs(); }

        // Returns to gameplay.
        void OnBackPressed();
        // Opens options.
        void OnOptionsPressed();
        // Returns to main menu.
        void OnMainMenuPressed();
        // Opens save game scene.
        void OnSaveGamePressed();
        // Opens load game scene.
        void OnLoadGamePressed();
        // Requests application shutdown.
        void OnQuitPressed();

        VBox vbox;
        std::unique_ptr<GuiController> controller{nullptr};
        InputProcessor inputs;
        std::string gameplaySceneName{"GameScene"};
};

#endif
