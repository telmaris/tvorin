#include "scenes/Scenes.h"
#include "scenes/SceneUtils.h"

#include <algorithm>
#include <chrono>

// Initializes NewGameScene::NewGameScene.
NewGameScene::NewGameScene()
{
    menuBackground.ChangePositionAnchor({0.0f, 0.0f});
    menuBackground.ChangeSizeAnchor({1.0f, 1.0f});
    menuBackground.LoadFromDirectory("assets/ui/menu/newgame");

    menuPanel.ChangePositionAnchor(Vec2f{0.16f, 0.07f});
    menuPanel.ChangeSizeAnchor(Vec2f{0.68f, 0.86f});

    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.63f, 0.84f});
    backButton.ChangeSizeAnchor(Vec2f{0.18f, 0.065f});
    backButton.func = std::bind(&NewGameScene::OnBackPressed, this);

    gameName.ChangePositionAnchor(Vec2f{0.30f, 0.12f});
    gameName.ChangeSizeAnchor(Vec2f{0.40f, 0.07f});

    sizeButton.ChangePositionAnchor(Vec2f{0.30f, 0.22f});
    sizeButton.ChangeSizeAnchor(Vec2f{0.40f, 0.055f});
    sizeButton.func = std::bind(&NewGameScene::OnSizePressed, this);

    difficultyButton.ChangePositionAnchor(Vec2f{0.30f, 0.29f});
    difficultyButton.ChangeSizeAnchor(Vec2f{0.40f, 0.055f});
    difficultyButton.func = std::bind(&NewGameScene::OnDifficultyPressed, this);

    resourceDensity.ChangePositionAnchor(Vec2f{0.30f, 0.38f});
    resourceDensity.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    resourceDensity.currentValue = 0.5f;
    resourceFieldSize.ChangePositionAnchor(Vec2f{0.30f, 0.47f});
    resourceFieldSize.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    resourceFieldSize.currentValue = 0.5f;
    resourceRichness.ChangePositionAnchor(Vec2f{0.30f, 0.56f});
    resourceRichness.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    resourceRichness.currentValue = 0.5f;
    aiOpponents.ChangePositionAnchor(Vec2f{0.30f, 0.65f});
    aiOpponents.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    aiOpponents.currentValue = 0.2f;

    debugMode.ChangeText("Debug mode");
    debugMode.ChangePositionAnchor(Vec2f{0.30f, 0.73f});
    debugMode.ChangeSizeAnchor(Vec2f{0.20f, 0.045f});

    startGame.ChangeText("Start game");
    startGame.ChangePositionAnchor(Vec2f{0.19f, 0.84f});
    startGame.ChangeSizeAnchor(Vec2f{0.18f, 0.065f});
    startGame.func = std::bind(&NewGameScene::OnStartPressed, this);

    startTutorial.ChangeText("Tutorial");
    startTutorial.ChangePositionAnchor(Vec2f{0.41f, 0.84f});
    startTutorial.ChangeSizeAnchor(Vec2f{0.18f, 0.065f});
    startTutorial.func = std::bind(&NewGameScene::OnTutorialPressed, this);

    tooltipWidget.func = [this](double)
    {
        Vector2 mouse = GetMousePosition();
        Vec2i mp{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
        if (gameName.ContainsPoint(mp))
            Tooltip::Draw("World name", {"Name of the save file for this world.", "Used as the filename — special characters will be replaced."});
        else if (sizeButton.ContainsPoint(mp))
            Tooltip::Draw("Map size", {
                "Sets the tile dimensions of the generated world.",
                "{separator}",
                "S 301x301 — fast to generate, good for testing",
                "M 501x501 — standard game",
                "L 701x701 — large, longer game",
                "XL 1001x1001 — very large, expect long generation"});
        else if (difficultyButton.ContainsPoint(mp))
            Tooltip::Draw("AI difficulty", {
                "Controls how aggressively and intelligently the AI plays.",
                "{separator}",
                "Primitive — barely reacts, no expansion",
                "Easy — slow expansion, weak army",
                "Normal — balanced AI opponent",
                "Hard — fast expansion, strong military pressure"});
        else if (resourceDensity.ContainsPoint(mp))
            Tooltip::Draw("Resource density", {"How frequently resource deposits are scattered across the map.", "{bonus}Higher values = more deposits, easier economy.", "{penalty}Lower values = sparse resources, tighter logistics required."});
        else if (resourceFieldSize.ContainsPoint(mp))
            Tooltip::Draw("Resource field size", {"How large each individual resource deposit is.", "{bonus}Higher values = bigger fields, longer before depletion.", "{penalty}Lower values = small pockets scattered around."});
        else if (resourceRichness.ContainsPoint(mp))
            Tooltip::Draw("Resource richness", {"Total amount of resources in each deposit.", "{bonus}Higher values = more total resources per field.", "{penalty}Lower values = deposits run out faster."});
        else if (aiOpponents.ContainsPoint(mp))
            Tooltip::Draw("AI opponents", {"Number of AI-controlled enemy players (0 to 5).", "Each opponent starts at a random location on the map."});
        else if (debugMode.ContainsPoint(mp))
            Tooltip::Draw("Debug mode", {"Forces a tiny 101x101 map with 1 AI opponent and preset resource values.", "Useful for rapid testing — skips the generation wait."});
        else if (startGame.ContainsPoint(mp))
            Tooltip::Draw("Start game", {"Generate the world and begin playing.", "Map generation may take several seconds for larger sizes."});
        else if (startTutorial.ContainsPoint(mp))
            Tooltip::Draw("Tutorial", {"Generate the world and begin the guided introduction.", "The tutorial pauses whenever an instruction window is open."});
        else if (backButton.ContainsPoint(mp))
            Tooltip::Draw("Back", {"Return to the main menu without starting a game."});
    };

    RefreshOptionLabels();

    Vec2i windowSize{GetScreenWidth(), GetScreenHeight()};
    backButton.UpdateSize(windowSize);
    gameName.UpdateSize(windowSize);
    startGame.UpdateSize(windowSize);
    startTutorial.UpdateSize(windowSize);
    sizeButton.UpdateSize(windowSize);
    difficultyButton.UpdateSize(windowSize);
    resourceDensity.UpdateSize(windowSize);
    resourceFieldSize.UpdateSize(windowSize);
    resourceRichness.UpdateSize(windowSize);
    aiOpponents.UpdateSize(windowSize);
    debugMode.UpdateSize(windowSize);
}

// Advances this object's state for one frame.
void NewGameScene::Update(double dt)
{
    ProcessGuiInput(dt);
    RefreshOptionLabels();

    render.Draw({&menuBackground,
                 &menuPanel,
                 &gameName,
                 &sizeButton,
                 &difficultyButton,
                 &resourceDensity,
                 &resourceFieldSize,
                 &resourceRichness,
                 &aiOpponents,
                 &debugMode,
                 &startGame,
                 &startTutorial,
                 &backButton,
                 &tooltipWidget}, dt);
}

// Handles the UI action represented by OnBackPressed.
void NewGameScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnStartPressed.
void NewGameScene::OnStartPressed()
{
    auto msg = std::make_shared<NewGameEvent>();
    msg->sender = this;
    msg->name = gameName.GetText();
    msg->params = BuildMapParameters();
    broker->Broadcast(msg);
}

void NewGameScene::OnTutorialPressed()
{
    auto msg = std::make_shared<TutorialGameEvent>();
    msg->sender = this;
    msg->name = gameName.GetText();
    msg->params = BuildMapParameters();
    // The guided defence/counterattack stages require a scripted opponent.
    msg->params.aiOpponentCount = std::max(1, msg->params.aiOpponentCount);
    // The tutorial may intentionally inherit Debug mode from the New Game
    // panel so its scripted steps can be tested quickly with the compact map
    // and debug resource shortcuts.
    broker->Broadcast(msg);
}

MapParameters NewGameScene::BuildMapParameters()
{
    MapParameters params;
    params.sizePreset = selectedSize;
    params.sizeX = MapGenerator::SizeFromPreset(params.sizePreset);
    params.sizeY = params.sizeX;
    params.seed = static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count());
    params.resourceDensity = resourceDensity.GetValue();
    params.resourceFieldSize = resourceFieldSize.GetValue();
    params.resourceRichness = SliderToInt(resourceRichness.GetValue(), 30, 250);
    params.aiOpponentCount = SliderToInt(aiOpponents.GetValue(), 0, 5);
    params.aiDifficulty = selectedDifficulty;
    params.debugMode = debugMode.IsActive();
    if (params.debugMode)
    {
        params.sizePreset = MapSizePreset::S;
        params.sizeX = 101;
        params.sizeY = 101;
        params.aiOpponentCount = 1;
        params.resourceDensity = 0.65f;
        params.resourceFieldSize = 0.45f;
        params.resourceRichness = 120;
    }

    return params;
}

// Handles the UI action represented by OnSizePressed.
void NewGameScene::OnSizePressed()
{
    int next = (static_cast<int>(selectedSize) + 1) % 4;
    selectedSize = static_cast<MapSizePreset>(next);
    RefreshOptionLabels();
}

// Handles the UI action represented by OnDifficultyPressed.
void NewGameScene::OnDifficultyPressed()
{
    selectedDifficulty = (selectedDifficulty + 1) % 4;
    RefreshOptionLabels();
}

// Initializes NewGameScene::RefreshOptionLabels.
void NewGameScene::RefreshOptionLabels()
{
    sizeButton.ChangeText("Map size: " + MapSizeName(selectedSize));
    difficultyButton.ChangeText("AI difficulty: " + DifficultyName(selectedDifficulty));
    resourceDensity.ChangeText("Resource density " + std::to_string(SliderToInt(resourceDensity.GetValue(), 50, 225)) + "%");
    resourceFieldSize.ChangeText("Field size " + std::to_string(SliderToInt(resourceFieldSize.GetValue(), 65, 200)) + "%");
    resourceRichness.ChangeText("Richness " + std::to_string(SliderToInt(resourceRichness.GetValue(), 30, 250)));
    aiOpponents.ChangeText("AI opponents " + std::to_string(SliderToInt(aiOpponents.GetValue(), 0, 5)));
    if (debugMode.IsActive())
        sizeButton.ChangeText("Map size: DEBUG 101x101");
}

// Handles the requested event or transfer.
void NewGameScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        menuBackground.UpdateSize(ptr->windowSize);
        menuPanel.UpdateSize(ptr->windowSize);
        backButton.UpdateSize(ptr->windowSize);
        gameName.UpdateSize(ptr->windowSize);
        startGame.UpdateSize(ptr->windowSize);
        startTutorial.UpdateSize(ptr->windowSize);
        sizeButton.UpdateSize(ptr->windowSize);
        difficultyButton.UpdateSize(ptr->windowSize);
        resourceDensity.UpdateSize(ptr->windowSize);
        resourceFieldSize.UpdateSize(ptr->windowSize);
        resourceRichness.UpdateSize(ptr->windowSize);
        aiOpponents.UpdateSize(ptr->windowSize);
        debugMode.UpdateSize(ptr->windowSize);
    }
}
