#include "scenes/Scenes.h"
#include "core/Log.h"
#include "ui/ControlIcons.h"
#include "ui/InputManager.h"

#include "raylib.h"

namespace
{
    void ApplyBorderlessMonitorWindow()
    {
        if (!IsWindowReady())
            return;

        const int monitor = GetCurrentMonitor();
        const int monitorWidth = GetMonitorWidth(monitor);
        const int monitorHeight = GetMonitorHeight(monitor);
        const Vector2 monitorPosition = GetMonitorPosition(monitor);

        // Maximized windows use the Windows work area and therefore leave the
        // taskbar visible. A borderless window sized to the monitor bounds is
        // the actual fullscreen-windowed presentation we want here.
        ClearWindowState(FLAG_FULLSCREEN_MODE | FLAG_WINDOW_MAXIMIZED);
        SetWindowState(FLAG_WINDOW_UNDECORATED | FLAG_BORDERLESS_WINDOWED_MODE |
                       FLAG_WINDOW_ALWAYS_RUN);
        SetWindowPosition(static_cast<int>(monitorPosition.x),
                          static_cast<int>(monitorPosition.y));
        SetWindowSize(monitorWidth, monitorHeight);
    }

    float SmoothStep(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }
}

// Initializes GameWindow::LaunchGame.
void GameWindow::LaunchGame()
{
    // Keep presentation driven by the explicit frame cap in MainLoop. VSync
    // adds a second limiter and makes frame pacing depend on the compositor.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNDECORATED |
                   FLAG_BORDERLESS_WINDOWED_MODE | FLAG_WINDOW_ALWAYS_RUN |
                   FLAG_WINDOW_HIDDEN);
    InitWindow(1920, 1080, "Tvorin");
    ApplyBorderlessMonitorWindow();
    SetWindowMinSize(1280, 720);
    Renderer::InitializeCustomCursor();
    // Pilot: use Departure Mono consistently for both shared UI roles. Keeping
    // both roles loaded preserves the existing title/body role switching while
    // making panels, title bars, buttons, tooltips and dense labels identical.
    GuiPanel::LoadUiFont("assets/fonts/DepartureMono-Regular.otf");
    GuiPanel::LoadUiPlainFont("assets/fonts/DepartureMono-Regular.otf");
    UiControlIcons::Load();

    InitAudioDevice();
    audio.Init();

    AudioConfig audioConfig = LoadAudioConfig();
    audio.SetMasterVolume(audioConfig.masterVolume);
    audio.SetMusicVolume(audioConfig.musicVolume);
    audio.SetSfxVolume(audioConfig.sfxVolume);

    // Music themes — add supported audio files to assets/music/ to activate them.
    audio.RegisterMusic("menu",      "assets/music/menu_theme.wav");
    audio.RegisterMusic("gameplay",  "assets/music/game_theme_ambient.wav");
    audio.RegisterMusic("gameplay_ambient_1", "assets/music/game_ambient_1.wav");
    audio.RegisterMusic("gameplay_ambient_2", "assets/music/game_ambient_2.ogg");
    audio.RegisterMusic("battle",    "assets/music/battle_theme.ogg");
    audio.RegisterMusicRotation("gameplay_rotation",
                                {"gameplay", "gameplay_ambient_1", "gameplay_ambient_2"});

    // Both tracks participate in the first menu/game transition. Preloading
    // them while the window is still on its startup black frame avoids a
    // direction-dependent hitch when gameplay is selected from the menu.
    audio.PreloadMusic("menu");
    audio.PreloadMusic("gameplay");
    audio.PreloadMusic("gameplay_ambient_1");
    audio.PreloadMusic("gameplay_ambient_2");

    // Sound effects — add .wav/.ogg files to assets/audio/sfx/ to activate them.
    audio.RegisterSound("click",        "assets/sfx/mouse_click.wav");
    audio.RegisterSound("build",        "assets/sfx/button_clicked.mp3");
    audio.RegisterSound("notification", "assets/sfx/notification.mp3");
    audio.RegisterSound("error",        "assets/sfx/error.wav");
    audio.RegisterSound("research",     "assets/sfx/research_ready.mp3");
    audio.RegisterSound("destroy",      "assets/sfx/destroy.wav");
    audio.RegisterSound("recruit",      "assets/sfx/recruit.wav");

    AddScene<StudioSplashScene>("StudioSplashScene");
    AddScene<MainMenuScene>("MainScene");
    AddScene<OptionsScene>("OptionsScene");
    AddScene<LoadingScene>("LoadingScene");
    AddScene<GameScene>("GameScene");
    AddScene<TutorialScene>("TutorialScene");
    AddScene<NewGameScene>("NewGameScene");
    AddScene<MultiplayerScene>("MultiplayerScene");
    AddScene<LoadGameScene>("LoadGameScene");
    AddScene<SaveGameScene>("SaveGameScene");
    AddScene<GameMenuScene>("GameMenuScene");
    AddScene<ControlsScene>("ControlsScene");

    // Start underneath an opaque overlay so the first visible frame is a
    // deliberate studio splash rather than a partially initialized menu.
    transitionAlpha = 1.0f;
    transitionPhase = SceneTransitionPhase::FadeIn;
    ActivateScene("StudioSplashScene", "");

    // Present one intentional black frame while the native window is still
    // hidden. This prevents DWM from exposing the provisional 1920x1080
    // window/background during scene and asset initialization.
    BeginDrawing();
    ClearBackground(BLACK);
    EndDrawing();
    ClearWindowState(FLAG_WINDOW_HIDDEN);

    MainLoop();

    DiscardPreparedGameSession();
    ShutdownRenderers();
    // Destroy scene-owned widget handles while the native window is still
    // alive. This keeps move-only UI assets from attempting a late raylib
    // unload during GameWindow destruction.
    activeScene.reset();
    scenes.clear();
    uiAssets.Close();
    GuiPanel::UnloadResourceAtlas();
    UiTextFont::Unload();
    UiControlIcons::Unload();
    Renderer::ShutdownCustomCursor();
    audio.Cleanup();
    CloseAudioDevice();
    ShowCursor();
    CloseWindow();
}

void GameWindow::ShutdownRenderers()
{
    for (auto& [name, scene] : scenes)
    {
        if (scene != nullptr)
            scene->render.Shutdown();
    }
}

// Handles the requested event or transfer.
void GameWindow::HandleEvent(std::shared_ptr<Event> e)
{
    Log::Msg(tag, e->msgName, " received!");
    auto ptr = std::dynamic_pointer_cast<QuitGameEvent>(e);
    if (ptr != nullptr)
    {
        isRunning = false;
    }

    auto newGame = std::dynamic_pointer_cast<NewGameEvent>(e);
    if (newGame != nullptr)
    {
        RequestSceneChange("LoadingScene", activeScene != nullptr ? activeScene->name : "", true);
    }

    auto tutorialGame = std::dynamic_pointer_cast<TutorialGameEvent>(e);
    if (tutorialGame != nullptr)
    {
        RequestSceneChange("LoadingScene", activeScene != nullptr ? activeScene->name : "", true);
    }

    if (std::dynamic_pointer_cast<HostMultiplayerGameEvent>(e) != nullptr ||
        std::dynamic_pointer_cast<JoinMultiplayerGameEvent>(e) != nullptr)
    {
        RequestSceneChange("LoadingScene", activeScene != nullptr ? activeScene->name : "", true);
    }

    auto ptr2 = std::dynamic_pointer_cast<ChangeSceneEvent>(e);
    if (ptr2 != nullptr)
    {
        RequestSceneChange(ptr2->sceneName, ptr2->previousSceneName, false);
    }

    auto ptr3 = std::dynamic_pointer_cast<ToggleFullscreenEvent>(e);
    if (ptr3 != nullptr)
        ApplyBorderlessMonitorWindow();
}

// Initializes GameWindow::MainLoop.
void GameWindow::MainLoop()
{
    SetTargetFPS(150);
    bool focusSuspended = false;
    bool inputEnabledBeforeFocusLoss = true;
    while (isRunning)
    {
        const bool windowFocused = IsWindowFocused();
        if (!windowFocused && !focusSuspended)
        {
            focusSuspended = true;
            inputEnabledBeforeFocusLoss = InputManager::IsInputEnabled();
            InputManager::SetInputEnabled(false);
            if (activeScene != nullptr)
                activeScene->OnWindowFocusChanged(false);
        }

        const bool firstFrameAfterFocusReturn = windowFocused && focusSuspended;
        if (firstFrameAfterFocusReturn)
            focusSuspended = false;
        UpdateWindowSize();
        const KeyBindingMap& bindings = GetDefaultKeyBindings();
        const int captureKey = bindings.GetKeyForAction(GameAction::CaptureFinalFrame);
        if (captureKey != 0 &&
            (InputManager::IsKeyDown(KEY_LEFT_CONTROL) || InputManager::IsKeyDown(KEY_RIGHT_CONTROL)) &&
            InputManager::IsKeyPressed(captureKey))
            Renderer::RequestFinalFrameCapture();
        const float dt = GetFrameTime();
        UpdateSceneTransition(dt);
        audio.Update(dt);
        Update(dt);

        // Keep local input suppressed through the first complete frame after
        // focus returns so stale mouse/key edges cannot affect gameplay. The
        // simulation and networking continue on every frame while unfocused.
        if (firstFrameAfterFocusReturn)
        {
            InputManager::SetInputEnabled(inputEnabledBeforeFocusLoss);
            if (activeScene != nullptr)
                activeScene->OnWindowFocusChanged(true);
        }
    }
}

bool GameWindow::IsSceneTransitionIdle() const
{
    return transitionPhase == SceneTransitionPhase::Idle;
}

void GameWindow::PublishPreparedGameSession(
    std::unique_ptr<IGameSession> session,
    std::string targetScene,
    bool multiplayerClient,
    bool usedFallback)
{
    DiscardPreparedGameSession();
    preparedGameSession = std::move(session);
    preparedGameTarget = std::move(targetScene);
    preparedGameIsClient = multiplayerClient;
    preparedGameUsedFallback = usedFallback;
}

std::unique_ptr<IGameSession> GameWindow::TakePreparedGameSession(
    const std::string& targetScene,
    bool& multiplayerClient,
    bool& usedFallback)
{
    if (preparedGameSession == nullptr || preparedGameTarget != targetScene)
        return nullptr;

    multiplayerClient = preparedGameIsClient;
    usedFallback = preparedGameUsedFallback;
    preparedGameTarget.clear();
    preparedGameIsClient = false;
    preparedGameUsedFallback = false;
    return std::move(preparedGameSession);
}

void GameWindow::DiscardPreparedGameSession()
{
    preparedGameSession.reset();
    preparedGameTarget.clear();
    preparedGameIsClient = false;
    preparedGameUsedFallback = false;
}

void GameWindow::ActivateScene(const std::string& name, const std::string& previousSceneName)
{
    auto sceneIt = scenes.find(name);
    if (sceneIt == scenes.end() || sceneIt->second == nullptr)
        return;

    if (activeScene != nullptr)
        activeScene->OnDeactivated();

    activeScene = sceneIt->second;
    activeScene->previousSceneName = previousSceneName;

    // Central input-gate reset (see IGuiHandler): every activated scene
    // starts with its GUI input gated until it has presented a frame and
    // ESC is released. This remains centralized despite delayed transitions.
    if (auto* guiHandler = dynamic_cast<IGuiHandler*>(activeScene.get()))
        guiHandler->ResetGuiInputGate();
    activeScene->OnActivated();
}

void GameWindow::RequestSceneChange(std::string name, std::string previousSceneName,
                                    bool stopMusicBeforeGameplayStart)
{
    if (scenes.find(name) == scenes.end())
        return;

    // Ignore duplicate requests while the current transition is already in
    // flight. This prevents a held key or a second event from replacing the
    // destination half-way through a fade.
    if (transitionPhase != SceneTransitionPhase::Idle)
        return;

    pendingSceneName = std::move(name);
    pendingPreviousSceneName = std::move(previousSceneName);
    transitionPhase = SceneTransitionPhase::FadeOut;
    transitionElapsed = 0.0f;
    transitionHoldRemaining = HoldSeconds;
    InputManager::SetInputEnabled(false);

    // For a newly generated world, silence the menu before entering the
    // opaque/loading part of the transition. The gameplay track starts only
    // after the session has actually been created.
    if (stopMusicBeforeGameplayStart)
        audio.StopMusic(0.22f);
}

void GameWindow::UpdateSceneTransition(float dt)
{
    const float safeDt = std::clamp(dt, 0.0f, 0.1f);

    switch (transitionPhase)
    {
        case SceneTransitionPhase::Idle:
            transitionAlpha = 0.0f;
            break;

        case SceneTransitionPhase::FadeOut:
            transitionElapsed += safeDt;
            transitionAlpha = SmoothStep(transitionElapsed / FadeOutSeconds);
            if (transitionAlpha >= 1.0f)
            {
                transitionPhase = SceneTransitionPhase::Hold;
                transitionElapsed = 0.0f;
                ActivateScene(pendingSceneName, pendingPreviousSceneName);
                // OnActivated() may restore input for gameplay scenes; keep
                // it blocked until the new scene is fully visible.
                InputManager::SetInputEnabled(false);
            }
            break;

        case SceneTransitionPhase::Hold:
            transitionHoldRemaining = std::max(0.0f, transitionHoldRemaining - safeDt);
            if (transitionHoldRemaining <= 0.0f)
            {
                transitionPhase = SceneTransitionPhase::FadeIn;
                transitionElapsed = 0.0f;
            }
            break;

        case SceneTransitionPhase::FadeIn:
            transitionElapsed += safeDt;
            transitionAlpha = 1.0f - SmoothStep(transitionElapsed / FadeInSeconds);
            if (transitionAlpha <= 0.0f)
            {
                transitionPhase = SceneTransitionPhase::Idle;
                pendingSceneName.clear();
                pendingPreviousSceneName.clear();
                InputManager::SetInputEnabled(true);
            }
            break;
    }

    SetSceneTransitionOverlayAlpha(transitionAlpha);
}

// Advances UpdateWindowSize for one frame or simulation tick.
void GameWindow::UpdateWindowSize()
{
    // UI anchors and mouse coordinates are expressed in logical screen
    // pixels. Framebuffer/render dimensions are telemetry only and must not
    // leak into layout events on a scaled display.
    Vec2i currentSize{GetScreenWidth(), GetScreenHeight()};
    if(currentSize != lastWindowSize)
    {
        auto e = std::make_shared<WindowSizeChangedEvent>();
        e->sender = nullptr;
        e->windowSize = currentSize;
        Broadcast(e);
    }
    lastWindowSize = currentSize;
}
