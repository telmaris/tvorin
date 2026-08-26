#include "scenes/Scenes.h"

// ─── MenuNavSystem ───────────────────────────────────────────────────────────

MenuNavSystem::MenuNavSystem(GuiController* con)
    : GuiSystem(con)
{
    menuScene = dynamic_cast<GameMenuScene*>(owner->scene);
    actionMap["esc"] = [this] { menuScene->OnBackPressed(); };
}

void MenuNavSystem::Update(double dt)
{
    owner->AddUiWidget(&menuScene->vbox);
}

void MenuNavSystem::UpdateUiWidgets(Vec2i size)
{
    menuScene->vbox.UpdateSize(size);
}

// ─── GameMenuScene ───────────────────────────────────────────────────────────

// Initializes GameMenuScene::GameMenuScene.
GameMenuScene::GameMenuScene()
{
    vbox.ChangeSizeAnchor(Vec2f{0.3f, 0.60f});
    vbox.ChangePositionAnchor(Vec2f{0.35f, 0.19f});

    auto saveButton = std::make_shared<UiButton>();
    saveButton->ChangeText("Save Game");
    saveButton->func = std::bind(&GameMenuScene::OnSaveGamePressed, this);
    vbox.AddChild(saveButton);

    auto loadGameButton = std::make_shared<UiButton>();
    loadGameButton->ChangeText("Load Game");
    loadGameButton->func = std::bind(&GameMenuScene::OnLoadGamePressed, this);
    vbox.AddChild(loadGameButton);

    auto optionsButton = std::make_shared<UiButton>();
    optionsButton->ChangeText("Options");
    optionsButton->func = std::bind(&GameMenuScene::OnOptionsPressed, this);
    vbox.AddChild(optionsButton);

    auto mainMenuButton = std::make_shared<UiButton>();
    mainMenuButton->ChangeText("Main Menu");
    mainMenuButton->func = std::bind(&GameMenuScene::OnMainMenuPressed, this);
    vbox.AddChild(mainMenuButton);

    auto quitButton = std::make_shared<UiButton>();
    quitButton->ChangeText("Quit Game");
    quitButton->func = std::bind(&GameMenuScene::OnQuitPressed, this);
    vbox.AddChild(quitButton);

    auto returnButton = std::make_shared<UiButton>();
    returnButton->ChangeText("Return");
    returnButton->func = std::bind(&GameMenuScene::OnBackPressed, this);
    vbox.AddChild(returnButton);

    vbox.UpdateSize({GetScreenWidth(), GetScreenHeight()});

    controller = std::make_unique<GuiController>();
    controller->Init(this);
    controller->AddSystem<MenuNavSystem>("default");
    controller->ChangeSystem("default");

    inputs.InitMenu(controller.get());
}

// Handles the UI action represented by OnBackPressed.
void GameMenuScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = gameplaySceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnOptionsPressed.
void GameMenuScene::OnOptionsPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "OptionsScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnMainMenuPressed.
void GameMenuScene::OnMainMenuPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "MainScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnSaveGamePressed.
void GameMenuScene::OnSaveGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "SaveGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnLoadGamePressed.
void GameMenuScene::OnLoadGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "LoadGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnQuitPressed.
void GameMenuScene::OnQuitPressed()
{
    auto msg = std::make_shared<QuitGameEvent>();
    msg->sender = this;
    broker->Broadcast(msg);
}

// Advances this object's state for one frame.
void GameMenuScene::Update(double dt)
{
    // Input first (gated by IGuiHandler — see GuiHandler.h), then ALWAYS
    // draw, even on the frame a transition fired. The old code returned early
    // after OnBackPressed(), skipping render.Draw and therefore EndDrawing's
    // PollInputEvents — which left raylib's IsKeyPressed(ESC) edge un-consumed
    // for the next scene's first frame. GameScene then re-read the SAME press
    // and bounced straight back to this menu (the reported ESC ping-pong).
    ProcessGuiInput(dt);
    controller->Update(dt);
    render.Draw(controller->GetUiWidgets(), dt);
}

// Handles the requested event or transfer.
void GameMenuScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto sceneChange = std::dynamic_pointer_cast<ChangeSceneEvent>(e);
    if (sceneChange != nullptr && sceneChange->sceneName == "GameMenuScene" &&
        (sceneChange->previousSceneName == "GameScene" || sceneChange->previousSceneName == "TutorialScene"))
        gameplaySceneName = sceneChange->previousSceneName;

    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        for (auto& [name, system] : controller->systems)
            system->UpdateUiWidgets(ptr->windowSize);
    }
}
