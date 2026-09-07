#include "scenes/Scenes.h"

#include <algorithm>
#include <cmath>

namespace
{
    void DrawMenuClouds(Texture2D texture, Rectangle bounds, float scroll)
    {
        if (texture.id == 0 || bounds.width <= 0.0f || bounds.height <= 0.0f)
            return;

        const float imageRatio = texture.width / static_cast<float>(texture.height);
        const float drawHeight = std::max(bounds.height, bounds.width / imageRatio);
        const float drawWidth = drawHeight * imageRatio;
        const float drawY = bounds.y + (bounds.height - drawHeight) * 0.5f;
        const float offset = std::fmod(scroll, drawWidth);
        const float firstX = bounds.x - offset - drawWidth;

        for (float x = firstX; x < bounds.x + bounds.width; x += drawWidth)
        {
            DrawTexturePro(texture,
                           {0.0f, 0.0f, static_cast<float>(texture.width),
                            static_cast<float>(texture.height)},
                           {x, drawY, drawWidth, drawHeight},
                           {0.0f, 0.0f}, 0.0f, WHITE);
        }
    }
}

MainMenuScene::MainMenuScene()
{
    buttonsColumn.ChangeSizeAnchor(Vec2f{0.4f, 0.3f});
    buttonsColumn.ChangePositionAnchor(Vec2f{0.3f, 0.4f});

    const Vec2f fullScreen{1.0f, 1.0f};
    menuSkyBg.ChangeSizeAnchor(fullScreen);
    menuSkyBg.ChangePositionAnchor({0.0f, 0.0f});
    menuSkyBg.cover = true;
    menuSkyBg.LoadTextureFromFile("assets/ui/menu/layer_1.png");

    menuCloudsBgParallax.ChangeSizeAnchor(fullScreen);
    menuCloudsBgParallax.ChangePositionAnchor({0.0f, 0.0f});
    menuCloudTexture = tvorin::ui::TextureHandle{LoadTexture("assets/ui/menu/layer_2.png")};
    if (menuCloudTexture.IsValid())
        SetTextureFilter(menuCloudTexture.Get(), TEXTURE_FILTER_POINT);
    menuCloudsBgParallax.func = [this](double dt)
    {
        menuCloudScroll += std::max(0.0, dt) * 3.2;
        DrawMenuClouds(menuCloudTexture.Get(),
                       {static_cast<float>(menuCloudsBgParallax.pos.x),
                        static_cast<float>(menuCloudsBgParallax.pos.y),
                        static_cast<float>(menuCloudsBgParallax.size.x),
                        static_cast<float>(menuCloudsBgParallax.size.y)},
                       static_cast<float>(menuCloudScroll));
    };

    menuGrassBg.ChangeSizeAnchor(fullScreen);
    menuGrassBg.ChangePositionAnchor({0.0f, 0.0f});
    menuGrassBg.cover = true;
    menuGrassBg.LoadTextureFromFile("assets/ui/menu/layer_3.png");

    menuVillageBg.ChangeSizeAnchor(fullScreen);
    menuVillageBg.ChangePositionAnchor({0.0f, 0.0f});
    menuVillageBg.cover = true;
    menuVillageBg.LoadTextureFromFile("assets/ui/menu/layer_4.png");

    menuTvorinLogo.ChangePositionAnchor({0.24f, 0.15f});
    menuTvorinLogo.ChangeSizeAnchor({0.52f, 0.20f});
    menuTvorinLogo.SetFloating(5.0f, 4.8f, 0.35f);
    menuTvorinLogo.SetFrameDuration(0.18f);
    menuTvorinLogo.AddFrameFromFile("assets/ui/menu/tvorin_logo.png");

    statusLabel.ChangePositionAnchor(Vec2f{0.26f, 0.82f});
    statusLabel.ChangeSizeAnchor(Vec2f{0.48f, 0.05f});
    statusLabel.fontSize = 22;
    statusLabel.color = Color{238, 184, 84, 255};

    auto newGameButton = std::make_shared<UiButton>();
    newGameButton->ChangeText("New Game");
    newGameButton->func = std::bind(&MainMenuScene::OnNewGamePressed, this);
    buttonsColumn.AddChild(newGameButton);

    auto loadGameButton = std::make_shared<UiButton>();
    loadGameButton->ChangeText("Load Game");
    loadGameButton->func = std::bind(&MainMenuScene::OnLoadGamePressed, this);
    buttonsColumn.AddChild(loadGameButton);

    auto multiplayerButton = std::make_shared<UiButton>();
    multiplayerButton->ChangeText("Multiplayer");
    multiplayerButton->func = std::bind(&MainMenuScene::OnMultiplayerPressed, this);
    buttonsColumn.AddChild(multiplayerButton);

    auto optionsButton = std::make_shared<UiButton>();
    optionsButton->ChangeText("Options");
    optionsButton->func = std::bind(&MainMenuScene::OnOptionsPressed, this);
    buttonsColumn.AddChild(optionsButton);

    auto controlsButton = std::make_shared<UiButton>();
    controlsButton->ChangeText("Controls");
    controlsButton->func = std::bind(&MainMenuScene::OnControlsPressed, this);
    buttonsColumn.AddChild(controlsButton);

    auto quitButton = std::make_shared<UiButton>();
    quitButton->ChangeText("Quit");
    quitButton->func = std::bind(&MainMenuScene::OnQuitPressed, this);
    buttonsColumn.AddChild(quitButton);
}

MainMenuScene::~MainMenuScene()
{
    menuCloudTexture.Reset();
}

// Starts the menu music theme.
void MainMenuScene::OnActivated()
{
    if (audioSystem != nullptr)
        audioSystem->PlayMusic("menu", DefaultMusicCrossfadeSeconds);
}

void MainMenuScene::Update(double dt)
{
    ProcessGuiInput(dt);
    if (statusTimer > 0.0)
        statusTimer = std::max(0.0, statusTimer - dt);

    std::vector<UiWidget*> widgets{
        &menuSkyBg,
        &menuCloudsBgParallax,
        &menuGrassBg,
        &menuVillageBg,
        &menuTvorinLogo,
        &buttonsColumn};
    if (statusTimer > 0.0)
        widgets.push_back(&statusLabel);
    render.Draw(widgets, dt);
}

void MainMenuScene::OnNewGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "NewGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void MainMenuScene::OnLoadGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "LoadGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void MainMenuScene::OnMultiplayerPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "MultiplayerScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void MainMenuScene::OnOptionsPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "OptionsScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void MainMenuScene::OnControlsPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "ControlsScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void MainMenuScene::OnQuitPressed()
{
    auto msg = std::make_shared<QuitGameEvent>();
    msg->sender = this;
    broker->Broadcast(msg);
}

void MainMenuScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        buttonsColumn.UpdateSize(ptr->windowSize);
        menuSkyBg.UpdateSize(ptr->windowSize);
        menuCloudsBgParallax.UpdateSize(ptr->windowSize);
        menuGrassBg.UpdateSize(ptr->windowSize);
        menuVillageBg.UpdateSize(ptr->windowSize);
        menuTvorinLogo.UpdateSize(ptr->windowSize);
        statusLabel.UpdateSize(ptr->windowSize);
    }

    auto networkStatus = std::dynamic_pointer_cast<NetworkStatusEvent>(e);
    if (networkStatus != nullptr)
    {
        statusLabel.ChangeText(networkStatus->message);
        statusTimer = 8.0;
    }
}
