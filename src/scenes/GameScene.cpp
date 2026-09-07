#include "scenes/Scenes.h"
#include "scenes/SceneUtils.h"
#include "core/Log.h"
#include "data/TextureConfig.h"
#include "ui/GuiController.h"
#include "ui/Renderer.h"
#include "ui/UiText.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

namespace
{
    constexpr const char* TextureConfigPath = "assets/data/textures.rtsdata";

    struct GameNotificationSnapshot
    {
        int localPlayerId{-1};
        std::size_t unlockedTechnologyCount{0};
        std::size_t unlockedFocusCount{0};
    };

    void LoadWorldAtlases(Renderer& renderer)
    {
        std::string error;
        const TextureConfig textureConfig = LoadTextureConfig(TextureConfigPath, &error);
        if (error.empty())
        {
            for (const TextureAtlasDefinition& atlas : textureConfig.atlases)
            {
                // These atlases are tile-sized and used by the world pass;
                // standalone building art stays on its dedicated renderer path.
                if ((atlas.id != 0 && atlas.id != 1 && atlas.id != 19 && atlas.id != 41 && atlas.id != 144 && atlas.id != 145 && atlas.id != 146) || atlas.path.empty() ||
                    !FileExists(atlas.path.c_str()))
                    continue;
                renderer.atlasMap[atlas.id] = TextureAtlas{};
                renderer.atlasMap[atlas.id].LoadTextureAtlas(
                    atlas.path.c_str(), {atlas.cellWidth, atlas.cellHeight});
            }
        }

        // Keep a safe terrain fallback when an editor save is incomplete.
        if (!renderer.atlasMap.contains(0))
        {
            renderer.atlasMap[0] = TextureAtlas{};
            renderer.atlasMap[0].LoadTextureAtlas("assets/textures/terrain/terrain_tileset.png");
        }
    }

    // Building artwork is authored in textures.rtsdata so the game and the
    // texture editor consume the same atlas path and animation definition.
    // Legacy texturePath values remain a safe fallback for incomplete configs.
    void LoadBuildingArtwork(Renderer& renderer)
    {
        const auto& buildingDefinitions = GetBuildingDefinitions();
        std::string error;
        const TextureConfig textureConfig = LoadTextureConfig(TextureConfigPath, &error);
        std::set<BuildingType> configuredTypes;

        if (!error.empty())
        {
            Log::Msg("[Textures]", "Could not load ", TextureConfigPath, ": ", error,
                     ". Using legacy building texture paths.");
        }
        else
        {
            for (const BuildingTextureDefinition& textureDefinition : textureConfig.buildings)
            {
                const auto definitionIt = std::find_if(
                    buildingDefinitions.begin(), buildingDefinitions.end(),
                    [&](const BuildingDefinition& definition)
                    {
                        return definition.tag == "[" + textureDefinition.buildingType + "]";
                    });
                const TextureAtlasDefinition* atlas = textureConfig.FindAtlas(textureDefinition.sprite.atlasId);

                // The current standalone-building renderer supports strips
                // beginning at cell zero. The editor can still author richer
                // atlases; those stay on the legacy fallback until that path is
                // rendered natively.
                if (definitionIt == buildingDefinitions.end() || atlas == nullptr ||
                    atlas->path.empty() || !FileExists(atlas->path.c_str()) ||
                    textureDefinition.sprite.textureId != 0)
                    continue;

                renderer.LoadBuildingTexture(definitionIt->type, atlas->path);
                configuredTypes.insert(definitionIt->type);

                if (textureDefinition.animation.enabled && textureDefinition.animation.frames > 1 &&
                    textureDefinition.animation.frameTime > 0.0)
                {
                    renderer.RegisterBuildingAnimation(
                        definitionIt->type,
                        AnimationClip{0, textureDefinition.animation.frames,
                                      static_cast<float>(textureDefinition.animation.frameTime),
                                      textureDefinition.animation.looping});
                }
            }
        }

        for (const BuildingDefinition& definition : buildingDefinitions)
        {
            // Base artwork may come from textures.rtsdata or the legacy path,
            // while completed upgrade visuals are always authored directly in
            // the building definition and loaded independently below.
            if (!configuredTypes.contains(definition.type) && !definition.texturePath.empty())
                renderer.LoadBuildingTexture(definition.type, definition.texturePath);

            for (const BuildingUpgradeLevelDefinition& level : definition.upgradeLevels)
            {
                if (!level.visualTexturePath.empty())
                    renderer.LoadBuildingUpgradeTexture(definition.type, level.level,
                                                        level.visualTexturePath);
            }
        }
    }
}

// InputProcessor's polling methods live here since GameScene is its only
// owner (InputProcessor inputs; is a GameScene member).
bool InputProcessor::IsActionPressed(int action)
{
    bool result = false;

    if (action > ACTION_NULL && action < MAX_ACTION)
    {
        auto input = actionInputs[action];
        result = ((input.key >= 0) && InputManager::IsKeyPressed(input.key)) ||
                 ((input.button >= 0) && InputManager::IsMouseButtonPressed(input.button));
    }

    return result;
}

bool InputProcessor::IsActionReleased(int action)
{
    bool result = false;

    if (action > ACTION_NULL && action < MAX_ACTION)
    {
        auto input = actionInputs[action];
        result = ((input.key >= 0) && InputManager::IsKeyReleased(input.key)) ||
                 ((input.button >= 0) && InputManager::IsMouseButtonReleased(input.button));
    }

    return result;
}

bool InputProcessor::IsActionDown(int action)
{
    bool result = false;

    if (action > ACTION_NULL && action < MAX_ACTION)
    {
        auto input = actionInputs[action];
        result = ((input.key >= 0) && InputManager::IsKeyDown(input.key)) ||
                 ((input.button >= 0) && InputManager::IsMouseButtonDown(input.button));
    }

    return result;
}

GameScene::GameScene()
{
    render.InitializeWorldLayers();
    LoadWorldAtlases(render);

    LoadBuildingArtwork(render);

    GuiPanel::LoadResourceAtlas("assets/textures/resources/official/resources_rework_atlas.png", {64, 64});

    controller = std::make_unique<GuiController>();
    controller->Init(this);
    controller->AddSystem<BasicMapViewSystem>("default");
    controller->AddSystem<BuildGuiSystem>("build");
    controller->AddSystem<RoadBuildSystem>("road_build");
    controller->AddSystem<DestroyGuiSystem>("destroy");
    controller->AddSystem<UpgradeGuiSystem>("upgrade");
    controller->AddSystem<StatsGuiSystem>("stats");
    controller->AddSystem<FocusGuiSystem>("focus");
    controller->AddSystem<TechGuiSystem>("tech");
    controller->AddSystem<RosterGuiSystem>("roster");
    controller->AddSystem<StockpileGuiSystem>("stockpile");
    controller->AddSystem<GlobalMapGuiSystem>("global_map");
    controller->ChangeSystem("default");

    inputs.Init(controller.get());

    networkStatusLabel.ChangeText("");
    networkStatusLabel.ChangeSize(180, 28);
    networkStatusLabel.fontSize = 18;
    networkStatusLabel.color = Color{188, 226, 255, 255};
    UpdateNetworkStatusWidget({GetScreenWidth(), GetScreenHeight()});
    ownedProvinceList.scene = this;
    ownedProvinceList.journal = &campaignStatus;
    ownedProvinceList.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    campaignStatus.scene = this;
    campaignStatus.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    campaignSidebar.provinces = &ownedProvinceList;
    campaignSidebar.journal = &campaignStatus;
    campaignSidebar.UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

void GameScene::AppendGameplayWidgets(std::vector<UiWidget*>& widgets)
{
    ownedProvinceList.scene = this;
    ownedProvinceList.journal = &campaignStatus;
    campaignStatus.scene = this;
    campaignSidebar.provinces = &ownedProvinceList;
    campaignSidebar.journal = &campaignStatus;
    widgets.push_back(&campaignSidebar);
}

GameScene::~GameScene()
{
    runtimeLoop.reset();
    game = nullptr;
}

void GameScene::OnActivated()
{
    if (auto* window = dynamic_cast<GameWindow*>(broker))
    {
        bool multiplayerClient = false;
        bool usedFallback = false;
        if (auto prepared = window->TakePreparedGameSession(
                name, multiplayerClient, usedFallback))
        {
            AdoptPreparedSession(std::move(prepared), multiplayerClient,
                                 usedFallback);
        }
    }

    // A modal tutorial may have disabled the process-wide input query gate;
    // entering a regular gameplay scene always restores normal controls.
    InputManager::SetInputEnabled(true);

    if (runtimeLoop != nullptr && runtimeLoop->ShouldPauseWhenSceneInactive())
        runtimeLoop->SetPaused(HasBlockingPopup());
}

void GameScene::OnDeactivated()
{
    if (runtimeLoop != nullptr && runtimeLoop->ShouldPauseWhenSceneInactive())
        runtimeLoop->SetPaused(true);
}

void GameScene::OnWindowFocusChanged(bool focused)
{
    // Losing native-window focus is not a gameplay pause. GameWindow gates
    // local input, while fixed-tick simulation and networking keep advancing.
    // Explicit pause sources (blocking popup/game menu) remain authoritative.
    (void)focused;
}

namespace
{

    void DrawRuntimeLoadingScreen(Renderer& renderer, const std::string& message)
    {
        BeginDrawing();
        ClearBackground(Color{20, 14, 10, 255});
        int fontSize = 28;
        int width = UiText::Measure(message, fontSize);
        UiText::Draw(message,
                     (GetScreenWidth() - width) * 0.5f,
                     GetScreenHeight() * 0.5f,
                     fontSize,
                     Color{210, 224, 242, 255});
        // Loading frames use the same application-wide present path as every
        // scene so cursor, transition, telemetry and Ctrl+F9 capture stay in
        // the final backbuffer in the same order.
        renderer.PresentFrame();
    }

    class GameRuntimeLoopBase : public IGameRuntimeLoop
    {
    public:
        explicit GameRuntimeLoopBase(std::unique_ptr<IGameSession> session)
        : session(std::move(session))
        {
        }

        std::uint64_t SubmitCommand(const GameCommand& command) override
        {
            return session != nullptr ? session->SubmitCommand(command) : 0;
        }

        std::vector<GameCommandResult> ConsumeCommandResults() override
        {
            return session != nullptr ? session->ConsumeCommandResults() : std::vector<GameCommandResult>{};
        }

        bool IsConnectionClosed() const override
        {
            return session != nullptr && session->IsConnectionClosed();
        }

        std::string GetConnectionStatus() const override
        {
            return session != nullptr ? session->GetConnectionStatus() : std::string{};
        }

        std::recursive_mutex* GetWorldMutex() override
        {
            return session != nullptr ? session->GetWorldMutex() : nullptr;
        }

        bool ShouldPauseWhenSceneInactive() const override
        {
            return session != nullptr && session->ShouldPauseWhenSceneInactive();
        }

        void SetPaused(bool paused) override
        {
            if (session != nullptr)
                session->SetPaused(paused);
        }

        bool IsPaused() const override
        {
            return session != nullptr && session->IsPaused();
        }

    protected:
        void UpdateSessionAndResults(GameScene& scene, double dt)
        {
            if (session == nullptr)
                return;

            session->Update(dt);
            auto results = session->ConsumeCommandResults();
            scene.commandResults.insert(scene.commandResults.end(), results.begin(), results.end());

            GameSnapshot incomingSnapshot;
            if (session->ConsumeLatestSnapshot(incomingSnapshot))
                scene.latestSnapshot = std::move(incomingSnapshot);
        }

        void DrawReadyGameplay(GameScene& scene, double dt, bool lockWorld)
        {
            std::unique_lock<std::recursive_mutex> worldLock;
            if (lockWorld)
                if (auto* mutex = GetWorldMutex())
                    worldLock = std::unique_lock<std::recursive_mutex>(*mutex);

            GameWorld* renderWorld = session != nullptr ? session->GetWorld() : nullptr;
            // Gated through IGuiHandler (GameScene::HandleGuiInput forwards to
            // inputs.HandleInputs()) so the first frame after a scene switch
            // never re-consumes the key edge that caused the switch.
            const bool hasBlockingPopup = scene.HasBlockingPopup();
            std::vector<UiWidget*> widgets;
            if (!hasBlockingPopup)
            {
                scene.ProcessGuiInput(dt);
                scene.HandleRenderDebugInput();
                scene.controller->Update(dt);
                const auto controllerWidgets = scene.controller->GetUiWidgets();
                widgets.insert(widgets.end(), controllerWidgets.begin(), controllerWidgets.end());
                // Persistent campaign overlays belong above full-screen
                // controller panels (especially the global map), otherwise
                // its opaque parchment/fog layer hides the province list and
                // event journal completely.
                scene.AppendGameplayWidgets(widgets);
                AppendDiagnostics(scene, widgets);
            }
            else
            {
                // Keep the HUD and selected building panel visible behind the
                // modal. InputManager is disabled by the popup callback, so
                // these widgets remain display-only while the action button is
                // temporarily enabled by PopupWindowWidget::Update().
                scene.controller->Update(dt);
                const auto controllerWidgets = scene.controller->GetUiWidgets();
                widgets.insert(widgets.end(), controllerWidgets.begin(), controllerWidgets.end());
                scene.AppendGameplayWidgets(widgets);
                AppendDiagnostics(scene, widgets);
                if (UiWidget* popup = scene.GetBlockingPopupWidget())
                    widgets.push_back(popup);
            }

            // Camera input must settle before both the cached world layers and
            // the light/fog maps are rebuilt. Previously DrawMap ran first and
            // controller->Update moved the camera afterwards; DrawContent then
            // composited an old-camera world with a new-camera light map. That
            // made building lights drift or disappear while panning/zooming,
            // especially near the left edge of the render target.
            const bool globalMapActive = scene.controller != nullptr &&
                                         scene.controller->IsSystemActive("global_map");
            if (!globalMapActive)
            {
                if (renderWorld != nullptr)
                    renderWorld->DrawMap();
                else if (scene.latestSnapshot.IsValid())
                    scene.render.DrawSnapshot(scene.latestSnapshot);
                scene.PrepareGameplayRender();
            }

            // Keep the world lock held through widget rendering: every widget's
            // Update() (called from render.DrawContent) reads live simulation state,
            // so releasing the lock before drawing races the simulation thread
            // mutating those objects.
            //
            // But we must NOT hold it across the present: EndDrawing() blocks on
            // the frame cap (SetTargetFPS), and holding
            // worldMutex across that ~frame-long wait starves the 100 Hz background
            // sim thread — the whole simulation crawls (build/production/transport
            // stall). So issue all draw calls under the lock via DrawContent(),
            // then release the lock, then PresentFrame() unlocked.
            scene.render.DrawContent(widgets, dt, !globalMapActive);

            // Release the world lock BEFORE presenting so the sim thread runs
            // freely during the frame-cap wait inside EndDrawing().
            if (worldLock.owns_lock())
                worldLock.unlock();

            scene.render.PresentFrame();
        }

        void AppendDiagnostics(GameScene& scene, std::vector<UiWidget*>& widgets)
        {
            if (session == nullptr)
                return;

            int pingMs = session->GetPingMs();
            std::string connectionStatus = session->GetConnectionStatus();
            if (pingMs >= 0)
            {
                scene.networkStatusLabel.ChangeText("Ping " + std::to_string(pingMs) + " ms");
                widgets.push_back(&scene.networkStatusLabel);
            }
            else if (!connectionStatus.empty() && connectionStatus != "Connected" &&
                     connectionStatus != "Single player")
            {
                scene.networkStatusLabel.ChangeText(connectionStatus);
                widgets.push_back(&scene.networkStatusLabel);
            }
        }

        std::unique_ptr<IGameSession> session;
    };

    // Unified host session loop (SP + MP host both use HostSession with background thread)
    class HostRuntimeLoop : public GameRuntimeLoopBase
    {
    public:
        using GameRuntimeLoopBase::GameRuntimeLoopBase;

        void Update(GameScene& scene, double dt) override
        {
            UpdateSessionAndResults(scene, dt);
            if (session != nullptr && !session->IsReadyForGameplay())
            {
                std::string status = session->GetConnectionStatus();
                DrawRuntimeLoadingScreen(scene.render, status.empty() ? "Waiting for client map sync" : status);
                return;
            }

            DrawReadyGameplay(scene, dt, true);
        }
    };

    class MultiplayerClientRuntimeLoop : public GameRuntimeLoopBase
    {
    public:
        using GameRuntimeLoopBase::GameRuntimeLoopBase;

        void Update(GameScene& scene, double dt) override
        {
            UpdateSessionAndResults(scene, dt);
            if (session != nullptr && !session->IsReadyForGameplay())
            {
                std::string status = session->GetConnectionStatus();
                DrawRuntimeLoadingScreen(scene.render, status.empty() ? "Syncing map" : status);
                return;
            }

            DrawReadyGameplay(scene, dt, true);
        }
    };
}

void GameScene::HandleRenderDebugInput()
{
    const KeyBindingMap& bindings = GetDefaultKeyBindings();
    auto pressed = [&](GameAction action)
    {
        const int key = bindings.GetKeyForAction(action);
        return key != 0 && InputManager::IsKeyPressed(key);
    };

    if (pressed(GameAction::ToggleNightPreview))
    {
        render.ToggleNightPreview();
        Log::Msg("[Renderer] Night preview: ", render.IsNightPreviewEnabled() ? "on" : "off");
    }
    if (pressed(GameAction::ToggleDayNightCycle))
    {
        render.SetDayNightCycleEnabled(!render.IsDayNightCycleEnabled());
        Log::Msg("[Renderer] Day/night cycle: ", render.IsDayNightCycleEnabled() ? "on" : "off");
    }
    if (pressed(GameAction::ToggleDynamicLights))
    {
        render.SetDynamicLightsEnabled(!render.AreDynamicLightsEnabled());
        Log::Msg("[Renderer] Dynamic lights: ", render.AreDynamicLightsEnabled() ? "on" : "off");
    }
    if (pressed(GameAction::CycleRendererDebugView))
    {
        render.CycleDebugView();
        Log::Msg("[Renderer] Debug view changed.");
    }
    if (pressed(GameAction::ToggleLogisticsOverlay))
    {
        const bool enabled = !IsLogisticsOverlayPreferenceEnabled();
        SetLogisticsOverlayPreferenceEnabled(enabled);
        render.SetLogisticsOverlayEnabled(enabled);
        Log::Msg("[Renderer] Logistics overlay: ", enabled ? "on" : "off");
    }
}

void GameScene::Update(double dt)
{
    if (game == nullptr || runtimeLoop == nullptr)
        return;

    if (runtimeLoop->IsConnectionClosed())
    {
        std::string status = runtimeLoop->GetConnectionStatus();
        if (status.empty())
            status = "Server closed the connection";
        Log::Msg("GameScene", "Network session closed: ", status);
        ShutdownActiveGame();

        auto statusEvent = std::make_shared<NetworkStatusEvent>();
        statusEvent->sender = this;
        statusEvent->message = "Multiplayer disconnected: " + status;
        broker->Broadcast(statusEvent);

        auto sceneEvent = std::make_shared<ChangeSceneEvent>();
        sceneEvent->sender = this;
        sceneEvent->sceneName = "MainScene";
        sceneEvent->previousSceneName = name;
        broker->Broadcast(sceneEvent);
        return;
    }

    runtimeLoop->Update(*this, dt);

    if (audioSystem != nullptr && game != nullptr)
    {
        GameNotificationSnapshot notificationSnapshot;
        std::unique_lock<std::recursive_mutex> worldLock;
        if (auto* mutex = runtimeLoop->GetWorldMutex())
            worldLock = std::unique_lock<std::recursive_mutex>(*mutex);

        notificationSnapshot.localPlayerId = game->GetLocalPlayerId();
        const auto& players = game->GetPlayerHandler().players;
        auto pit = players.find(notificationSnapshot.localPlayerId);
        if (pit != players.end() && pit->second != nullptr)
        {
            const Player* player = pit->second.get();
            notificationSnapshot.unlockedTechnologyCount = player->technologies.GetUnlocked().size();
            notificationSnapshot.unlockedFocusCount = player->focuses.GetUnlocked().size();
        }

        // Do not touch the live world after this point. Audio playback and UI
        // bookkeeping may run without the simulation mutex.
        if (worldLock.owns_lock())
            worldLock.unlock();

        const int localId = notificationSnapshot.localPlayerId;
        for (const auto& result : commandResults)
        {
            if (result.playerId != localId)
                continue;
            if (!result.accepted)
            {
                audioSystem->PlaySound("error");
                continue;
            }

            switch (result.type)
            {
                case GameCommandType::BuildBuilding:
                    audioSystem->PlaySound("build");
                    break;
                case GameCommandType::StartFocus:
                case GameCommandType::StartTechnologyResearch:
                    audioSystem->PlaySound("research");
                    break;
                default:
                    break;
            }
        }

        if (notificationSnapshot.unlockedTechnologyCount > prevUnlockedTechCount ||
            notificationSnapshot.unlockedFocusCount > prevUnlockedFocusCount)
            audioSystem->PlaySound("notification");
        prevUnlockedTechCount = notificationSnapshot.unlockedTechnologyCount;
        prevUnlockedFocusCount = notificationSnapshot.unlockedFocusCount;

    }

    commandResults.clear();
}


void GameScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        for(auto& [name, system] : controller->systems)
        {
            system->UpdateUiWidgets(ptr->windowSize);
        }
        UpdateNetworkStatusWidget(ptr->windowSize);
        campaignSidebar.UpdateSize(ptr->windowSize);
    }

    auto sceneChange = std::dynamic_pointer_cast<ChangeSceneEvent>(e);
    if (sceneChange != nullptr && sceneChange->sceneName == "MainScene" && runtimeLoop != nullptr)
        ShutdownActiveGame();

    auto ptr3 = std::dynamic_pointer_cast<LoadGameEvent>(e);
    if (ptr3 != nullptr)
    {
        if (!LoadGame(ptr3->name))
            return;

        auto msg = std::make_shared<ChangeSceneEvent>();
        msg->sender = this;
        msg->sceneName = "GameScene";
        msg->previousSceneName = name;
        broker->Broadcast(msg);
    }

    auto saveEvent = std::dynamic_pointer_cast<SaveGameEvent>(e);
    if (saveEvent != nullptr)
    {
        if (SaveGame(saveEvent->name))
        {
            auto msg = std::make_shared<SaveListChangedEvent>();
            msg->sender = this;
            broker->Broadcast(msg);
        }
    }

}

void GameScene::AdoptPreparedSession(std::unique_ptr<IGameSession> session,
                                     bool multiplayerClient,
                                     bool usedFallback)
{
    if (session == nullptr || !session->IsReadyForGameplay())
        return;

    runtimeLoop.reset();
    game = nullptr;
    render.ClearLayers();

    std::unique_lock<std::recursive_mutex> worldLock;
    if (auto* mutex = session->GetWorldMutex())
        worldLock = std::unique_lock<std::recursive_mutex>(*mutex);
    game = session->GetWorld();
    if (game == nullptr || !game->IsInitialized())
    {
        game = nullptr;
        return;
    }
    game->AttachPresentation(&render);
    if (worldLock.owns_lock())
        worldLock.unlock();

    session->ActivateGameplay();
    if (multiplayerClient)
        runtimeLoop = std::make_unique<MultiplayerClientRuntimeLoop>(
            std::move(session));
    else
        runtimeLoop = std::make_unique<HostRuntimeLoop>(std::move(session));

    prevUnlockedTechCount = 0;
    prevUnlockedFocusCount = 0;
    latestSnapshot = GameSnapshot{};
    commandResults.clear();

    if (usedFallback && broker != nullptr)
    {
        auto status = std::make_shared<NetworkStatusEvent>();
        status->sender = this;
        status->message = "World created using a safe fallback layout";
        broker->Broadcast(status);
    }
    if (audioSystem != nullptr)
        audioSystem->PlayMusicRotation("gameplay_rotation", "gameplay",
                                       DefaultMusicCrossfadeSeconds);
}

bool GameScene::LoadGame(std::string name)
{
    std::string saveName = SanitizeSaveName(name);
    std::string filename{"saves/" + saveName + ".save"};
    auto loadedGame = std::make_unique<GameWorld>();
    if (loadedGame->LoadFromFile(filename, &render))
    {
        runtimeLoop.reset();
        game = nullptr;
        render.ClearLayers();
        auto hostSession = std::make_unique<HostSession>(std::move(loadedGame));
        game = hostSession->GetWorld();
        hostSession->ActivateGameplay();
        runtimeLoop = std::make_unique<HostRuntimeLoop>(std::move(hostSession));
        {
            auto pit = game->GetPlayerHandler().players.find(game->GetLocalPlayerId());
            if (pit != game->GetPlayerHandler().players.end())
            {
                prevUnlockedTechCount  = pit->second->technologies.GetUnlocked().size();
                prevUnlockedFocusCount = pit->second->focuses.GetUnlocked().size();
            }
        }
        Log::Msg("GameScene", "Save ", saveName, " loaded!");
        if (audioSystem != nullptr)
            audioSystem->PlayMusicRotation("gameplay_rotation", "gameplay",
                                           DefaultMusicCrossfadeSeconds);
        return true;
    }
    else
    {
        Log::Msg("GameScene", "Failed to load save ", saveName);
        return false;
    }
}

bool GameScene::SaveGame(std::string saveName)
{
    if (game == nullptr)
        return false;

    std::unique_lock<std::recursive_mutex> worldLock;
    if (runtimeLoop != nullptr)
        if (auto* mutex = runtimeLoop->GetWorldMutex())
            worldLock = std::unique_lock<std::recursive_mutex>(*mutex);

    saveName = saveName.empty() ? game->worldName : saveName;
    saveName = SanitizeSaveName(saveName);
    const std::string previousWorldName = game->worldName;
    game->worldName = saveName;

    std::string filename{"saves/" + saveName + ".save"};
    std::filesystem::create_directories("saves");
    if (!game->SaveToFile(filename))
    {
        game->worldName = previousWorldName;
        Log::Msg("GameScene", "Failed to save ", filename);
        return false;
    }

    Log::Msg("GameScene", "Saved ", filename);
    return true;
}

// Sends a local player's intent to the active session authority.
std::uint64_t GameScene::SubmitLocalCommand(const GameCommand& command)
{
    if (runtimeLoop != nullptr)
        return runtimeLoop->SubmitCommand(command);
    return 0;
}

// Returns command results received from the active session.
std::vector<GameCommandResult> GameScene::ConsumeCommandResults()
{
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

// Tears down the active runtime and closes any owned network transport.
void GameScene::ShutdownActiveGame()
{
    runtimeLoop.reset();
    game = nullptr;
    latestSnapshot = GameSnapshot{};
    commandResults.clear();
    render.ClearLayers();
    Log::Msg("GameScene", "Active game session shut down");
}

// Keeps the multiplayer diagnostics label pinned to the top-right corner.
void GameScene::UpdateNetworkStatusWidget(Vec2i windowSize)
{
    networkStatusLabel.ChangeSize(190, 28);
    networkStatusLabel.ChangePosition(std::max(8, windowSize.x - 205), 10);
}
