#include "scenes/Scenes.h"

#include "core/Log.h"
#include "multiplayer/TcpGameTransport.h"
#include "scenes/SceneUtils.h"
#include "ui/InputManager.h"
#include "ui/UiText.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr double MinimumLoadingSeconds = 1.0;

    const char* PhaseLabel(GameSessionStartupPhase phase)
    {
        switch (phase)
        {
            case GameSessionStartupPhase::Starting: return "STARTING SESSION";
            case GameSessionStartupPhase::GeneratingWorld: return "GENERATING WORLD";
            case GameSessionStartupPhase::WaitingForPeer: return "WAITING FOR PLAYERS";
            case GameSessionStartupPhase::SynchronizingWorld: return "SYNCHRONIZING WORLD";
            case GameSessionStartupPhase::Ready: return "READY";
            case GameSessionStartupPhase::Recovering: return "RECOVERING SESSION";
            case GameSessionStartupPhase::Failed: return "STARTUP FAILED";
            case GameSessionStartupPhase::Stopped: return "SESSION STOPPED";
        }
        return "LOADING";
    }
}

LoadingScene::~LoadingScene()
{
    session.reset();
    if (cursorHidden)
        Renderer::SetCustomCursorVisible(cursorWasVisible);
}

void LoadingScene::HandleEvent(std::shared_ptr<Event> event)
{
    if (auto newGame = std::dynamic_pointer_cast<NewGameEvent>(event))
    {
        pendingRequest = LaunchRequest{
            Mode::SinglePlayer,
            "GameScene",
            SanitizeSaveName(newGame->name),
            newGame->params};
        return;
    }

    if (auto tutorial = std::dynamic_pointer_cast<TutorialGameEvent>(event))
    {
        pendingRequest = LaunchRequest{
            Mode::SinglePlayer,
            "TutorialScene",
            SanitizeSaveName(tutorial->name),
            tutorial->params};
        return;
    }

    if (auto host = std::dynamic_pointer_cast<HostMultiplayerGameEvent>(event))
    {
        pendingRequest = LaunchRequest{
            Mode::MultiplayerHost,
            "GameScene",
            SanitizeSaveName(host->name),
            host->params,
            {},
            host->port,
            host->transport};
        return;
    }

    if (auto join = std::dynamic_pointer_cast<JoinMultiplayerGameEvent>(event))
    {
        pendingRequest = LaunchRequest{
            Mode::MultiplayerClient,
            "GameScene",
            SanitizeSaveName(join->name),
            join->params,
            join->address,
            join->port,
            join->transport};
    }
}

void LoadingScene::OnActivated()
{
    InputManager::SetInputEnabled(true);
    visibleElapsed = 0.0;
    displayedProgress = 0.01f;
    displayedStatus = {};
    transitionRequested = false;
    // Startup cancellation/failure invalidates lobby transports and the menu
    // music was already faded out. MainScene is the only universally safe
    // recovery target and restores the menu presentation from a clean state.
    returnScene = "MainScene";

    if (auto* window = dynamic_cast<GameWindow*>(broker))
        window->DiscardPreparedGameSession();

    cursorWasVisible = Renderer::IsCustomCursorVisible();
    Renderer::SetCustomCursorVisible(false);
    cursorHidden = true;
    StartPendingSession();
}

void LoadingScene::OnDeactivated()
{
    // An unexpected scene switch is also a cancellation path. Destruction of
    // the session joins its worker before any transport/world is released.
    session.reset();
    if (cursorHidden)
    {
        Renderer::SetCustomCursorVisible(cursorWasVisible);
        cursorHidden = false;
    }
}

void LoadingScene::StartPendingSession()
{
    if (!pendingRequest.has_value())
    {
        displayedStatus = {
            GameSessionStartupPhase::Failed,
            1.0f,
            "No game launch request",
            "LoadingScene was activated without launch data",
            false};
        return;
    }

    LaunchRequest request = std::move(*pendingRequest);
    pendingRequest.reset();
    activeMode = request.mode;
    targetScene = std::move(request.targetScene);
    try
    {
        if (activeMode == Mode::SinglePlayer)
        {
            session = std::make_unique<HostSession>(HostSessionStartRequest{
                std::move(request.worldName), std::move(request.params),
                HostWorldMode::SinglePlayer});
        }
        else if (activeMode == Mode::MultiplayerHost)
        {
            if (request.transport == nullptr)
                request.transport = TcpGameTransport::CreateHost(request.port);
            if (request.transport == nullptr)
                throw std::runtime_error("Could not create host transport");
            session = std::make_unique<HostSession>(
                HostSessionStartRequest{
                    std::move(request.worldName), std::move(request.params),
                    HostWorldMode::MultiplayerHost},
                std::move(request.transport), 1, true);
        }
        else
        {
            if (request.transport == nullptr)
                request.transport = TcpGameTransport::CreateClient(
                    request.address, request.port);
            if (request.transport == nullptr)
                throw std::runtime_error("Could not create client transport");
            session = std::make_unique<ClientSession>(
                std::make_unique<GameWorld>(), std::move(request.transport), 1);
        }
    }
    catch (const std::exception& exception)
    {
        displayedStatus = {
            GameSessionStartupPhase::Failed,
            1.0f,
            "Could not start game session",
            exception.what(),
            false};
        Log::Error("LoadingScene", "Session construction failed: ",
                   exception.what());
    }
    catch (...)
    {
        displayedStatus = {
            GameSessionStartupPhase::Failed,
            1.0f,
            "Could not start game session",
            "Unknown session construction exception",
            false};
        Log::Error("LoadingScene", "Session construction failed");
    }
}

void LoadingScene::HandleGuiInput(double dt)
{
    (void)dt;
    if (InputManager::IsKeyPressed(KEY_ESCAPE))
        CancelAndReturn("Game startup cancelled");
}

void LoadingScene::Update(double dt)
{
    visibleElapsed += std::clamp(dt, 0.0, 0.1);
    ProcessGuiInput(dt);

    if (session != nullptr)
    {
        session->Update(dt);
        displayedStatus = session->GetStartupStatus();
    }

    const float reportedTarget = displayedStatus.phase == GameSessionStartupPhase::Ready
        ? 1.0f
        : std::clamp(displayedStatus.progress, 0.01f, 0.98f);
    const float targetProgress = std::max(displayedProgress, reportedTarget);
    const float blend = 1.0f - std::exp(
        -4.5f * static_cast<float>(std::clamp(dt, 0.0, 0.1)));
    displayedProgress += (targetProgress - displayedProgress) * blend;

    DrawLoadingFrame(displayedStatus);

    auto* window = dynamic_cast<GameWindow*>(broker);
    if (transitionRequested || visibleElapsed < MinimumLoadingSeconds ||
        window == nullptr || !window->IsSceneTransitionIdle())
        return;

    if (displayedStatus.phase == GameSessionStartupPhase::Ready &&
        session != nullptr)
    {
        transitionRequested = true;
        window->PublishPreparedGameSession(
            std::move(session), targetScene,
            activeMode == Mode::MultiplayerClient,
            displayedStatus.usedFallback);

        auto change = std::make_shared<ChangeSceneEvent>();
        change->sender = this;
        change->sceneName = targetScene;
        change->previousSceneName = name;
        broker->Broadcast(change);
        return;
    }

    if (displayedStatus.phase == GameSessionStartupPhase::Failed ||
        displayedStatus.phase == GameSessionStartupPhase::Stopped)
    {
        const std::string detail = displayedStatus.error.empty()
            ? displayedStatus.message
            : displayedStatus.error;
        CancelAndReturn("Game startup failed: " + detail);
    }
}

void LoadingScene::CancelAndReturn(const std::string& reason)
{
    if (transitionRequested)
        return;
    transitionRequested = true;
    session.reset();

    if (cursorHidden)
    {
        Renderer::SetCustomCursorVisible(cursorWasVisible);
        cursorHidden = false;
    }

    if (broker != nullptr)
    {
        auto status = std::make_shared<NetworkStatusEvent>();
        status->sender = this;
        status->message = reason;
        broker->Broadcast(status);

        auto change = std::make_shared<ChangeSceneEvent>();
        change->sender = this;
        change->sceneName = returnScene.empty() ? "MainScene" : returnScene;
        change->previousSceneName = name;
        broker->Broadcast(change);
    }
}

void LoadingScene::DrawLoadingFrame(const GameSessionStartupStatus& status)
{
    BeginDrawing();
    ClearBackground(Color{18, 12, 9, 255});

    const float screenWidth = static_cast<float>(GetScreenWidth());
    const float screenHeight = static_cast<float>(GetScreenHeight());
    const float time = static_cast<float>(GetTime());
    const float stripeSpacing = std::max(90.0f, screenWidth / 12.0f);
    const float stripeTravel = std::fmod(time * 34.0f, stripeSpacing);
    for (float x = -screenHeight - stripeSpacing + stripeTravel;
         x < screenWidth + stripeSpacing; x += stripeSpacing)
    {
        DrawLineEx({x, 0.0f}, {x + screenHeight * 0.42f, screenHeight},
                   2.0f, Fade(UiTheme::Bronze, 0.055f));
    }

    const Vector2 center{screenWidth * 0.5f, screenHeight * 0.27f};
    const float pulse = 0.5f + 0.5f * std::sin(time * 2.4f);
    DrawRing(center, 42.0f + pulse * 3.0f, 44.0f + pulse * 3.0f,
             0.0f, 360.0f, 72, Fade(UiTheme::Bronze, 0.48f));
    DrawRing(center, 28.0f, 30.0f, 0.0f, 360.0f, 64,
             Fade(UiTheme::Sage, 0.48f));
    for (int marker = 0; marker < 3; ++marker)
    {
        const float angle = time * (0.9f + marker * 0.13f) +
                            marker * 2.0943951f;
        const float radius = 52.0f + marker * 7.0f;
        DrawCircleV({center.x + std::cos(angle) * radius,
                     center.y + std::sin(angle) * radius},
                    4.0f + marker, UiTheme::Sage);
    }

    const std::string phase = PhaseLabel(status.phase);
    UiText::Draw(phase,
                 (screenWidth - UiText::Measure(phase, 22)) * 0.5f,
                 screenHeight * 0.43f, 22, UiTheme::AmberBright);

    const std::string message = status.message.empty()
        ? "Preparing session"
        : status.message;
    UiText::Draw(message,
                 (screenWidth - UiText::Measure(message, 28)) * 0.5f,
                 screenHeight * 0.49f, 28, UiTheme::Parchment);

    Rectangle bar{screenWidth * 0.22f, screenHeight * 0.59f,
                  screenWidth * 0.56f, 34.0f};
    DrawRectangleRec(bar, Color{10, 9, 8, 235});
    DrawRectangleRec({bar.x + 3.0f, bar.y + 3.0f,
                      (bar.width - 6.0f) *
                          std::clamp(displayedProgress, 0.0f, 1.0f),
                      bar.height - 6.0f},
                     UiTheme::Sage);
    DrawRectangleLinesEx(bar, 2.0f, UiTheme::Bronze);

    const std::string percent = std::to_string(static_cast<int>(
        std::round(std::clamp(displayedProgress, 0.0f, 1.0f) * 100.0f))) + "%";
    UiText::Draw(percent,
                 bar.x + (bar.width - UiText::Measure(percent, 20)) * 0.5f,
                 bar.y + 7.0f, 20, UiTheme::Parchment);

    for (int pip = 0; pip < 5; ++pip)
    {
        const float pipPulse = 0.5f + 0.5f * std::sin(
            time * 5.0f - static_cast<float>(pip) * 0.72f);
        DrawCircleV({screenWidth * 0.5f + (pip - 2) * 22.0f,
                     bar.y + bar.height + 28.0f},
                    3.0f + pipPulse * 2.0f,
                    Fade(UiTheme::AmberBright, 0.32f + pipPulse * 0.62f));
    }

    UiText::Draw("Esc - cancel",
                 (screenWidth - UiText::Measure("Esc - cancel", 18)) * 0.5f,
                 screenHeight * 0.73f, 18,
                 Fade(UiTheme::Parchment, 0.65f));
    render.PresentFrame();
}
