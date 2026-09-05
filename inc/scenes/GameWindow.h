#ifndef GAMEWINDOW_H
#define GAMEWINDOW_H

#include "ui/AudioSystem.h"
#include "ui/GuiHandler.h"
#include "ui/UiAssetContext.h"
#include "core/Events.h"
#include "core/GameSession.h"

#include <utility>

enum class SceneTransitionPhase
{
    Idle,
    FadeOut,
    Hold,
    FadeIn
};

// Base scene that owns a renderer and receives events from the window broker.
class Scene : public EventClient
{
    public:
    // Updates and draws the scene for one frame.
    virtual void Update(double dt) = 0;
    // Called by GameWindow each time this scene becomes the active scene.
    virtual void OnActivated() {}
    // Called immediately before GameWindow switches away from this scene.
    virtual void OnDeactivated() {}
    // Called when the native window gains or loses focus. Implementations may
    // adjust presentation state, but focus alone must not pause simulation.
    virtual void OnWindowFocusChanged(bool focused) { (void)focused; }

    std::string   name;
    std::string   previousSceneName;
    Renderer      render;
    AudioSystem*  audioSystem{nullptr};  // points to GameWindow::audio; set by AddScene<T>()
};

// Raylib application shell that owns scenes, window state and the main loop.
class GameWindow : public EventBroker
{
    public:

    // Handles global events such as quit, fullscreen and scene switching.
    void HandleEvent(std::shared_ptr<Event>) override;

    // Creates a scene, registers it in the scene map and subscribes it to events.
    template <typename T> void AddScene(std::string name)
    {
        static_assert(std::is_base_of<Scene, T>::value);

        auto scene = std::make_shared<T>();
        scene->broker      = this;
        scene->name        = name;
        scene->audioSystem = &audio;
        scenes.insert({name, scene});

        AddClient(name, scene.get());
    }

    // Updates the currently active scene.
    inline void Update(double dt)
    {
        activeScene->Update(dt);
    }

    // Broadcasts a resize event when the logical UI screen size changes.
    void UpdateWindowSize();

    // Initializes Raylib, creates scenes and starts the game loop.
    void LaunchGame();

    // Runs the frame loop until a quit event is received.
    void MainLoop();
    // True when no fade/hold transition is in flight. LoadingScene uses this
    // before publishing success, cancellation or startup failure transitions.
    bool IsSceneTransitionIdle() const;

    // LoadingScene publishes a fully initialized/synchronized session here so
    // it survives the scene transition. The target gameplay scene consumes it
    // on the main thread from OnActivated().
    void PublishPreparedGameSession(std::unique_ptr<IGameSession> session,
                                    std::string targetScene,
                                    bool multiplayerClient,
                                    bool usedFallback);
    std::unique_ptr<IGameSession> TakePreparedGameSession(
        const std::string& targetScene,
        bool& multiplayerClient,
        bool& usedFallback);
    void DiscardPreparedGameSession();

    // Releases scene renderers while the OpenGL context is still alive.
    void ShutdownRenderers();

    // Activates a registered scene and records where navigation came from.
    inline void ChangeScene(std::string name, std::string previousSceneName)
    {
        RequestSceneChange(std::move(name), std::move(previousSceneName), false);
    }

    // Returns true while the transition has reached a fully opaque frame.
    // Gameplay setup uses this moment so expensive world creation happens
    // behind the black transition instead of freezing a visible menu.
    bool IsSceneTransitionOpaque() const
    {
        return transitionPhase == SceneTransitionPhase::Hold &&
               transitionAlpha >= 1.0f;
    }

    AudioSystem audio;
    tvorin::ui::UiAssetContext uiAssets;

    bool isRunning{true};
    const std::string tag{"GameWindow"};
    Vec2i lastWindowSize{};

private:
    void ActivateScene(const std::string& name, const std::string& previousSceneName);
    void RequestSceneChange(std::string name, std::string previousSceneName,
                            bool stopMusicBeforeGameplayStart);
    void UpdateSceneTransition(float dt);

public:
    std::map<std::string, std::shared_ptr<Scene>> scenes;
    std::shared_ptr<Scene> activeScene;

private:
    SceneTransitionPhase transitionPhase{SceneTransitionPhase::Idle};
    std::string pendingSceneName;
    std::string pendingPreviousSceneName;
    float transitionAlpha{0.0f};
    float transitionElapsed{0.0f};
    float transitionHoldRemaining{0.0f};
    static constexpr float FadeOutSeconds = 0.24f;
    static constexpr float FadeInSeconds = 0.38f;
    static constexpr float HoldSeconds = 0.06f;
    std::unique_ptr<IGameSession> preparedGameSession;
    std::string preparedGameTarget;
    bool preparedGameIsClient{false};
    bool preparedGameUsedFallback{false};
};
#endif
