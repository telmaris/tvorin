#include "scenes/Scenes.h"
#include "scenes/SceneUtils.h"
#include "core/PersistenceLimits.h"

#include <chrono>
#include <cmath>

namespace
{
    constexpr int MinimumProvinceCount = 16;
    constexpr int DefaultProvinceCount = 32;
    constexpr int MaximumProvinceCount =
        static_cast<int>(PersistenceLimits::MaxGlobalProvinces);

    constexpr float ProvinceCountSliderDefault =
        static_cast<float>(DefaultProvinceCount - MinimumProvinceCount) /
        static_cast<float>(MaximumProvinceCount - MinimumProvinceCount);
}

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

    // The form is intentionally one compact column. The global campaign
    // controls used to be hidden behind preset buttons; keeping them as
    // sliders makes the generated graph understandable before starting it.
    sizeButton.ChangePositionAnchor(Vec2f{0.30f, 0.10f});
    sizeButton.ChangeSizeAnchor(Vec2f{0.40f, 0.040f});
    sizeButton.func = std::bind(&NewGameScene::OnSizePressed, this);

    resourceDensity.ChangePositionAnchor(Vec2f{0.30f, 0.155f});
    resourceDensity.ChangeSizeAnchor(Vec2f{0.40f, 0.035f});
    resourceDensity.currentValue = 0.5f;
    resourceFieldSize.ChangePositionAnchor(Vec2f{0.30f, 0.200f});
    resourceFieldSize.ChangeSizeAnchor(Vec2f{0.40f, 0.035f});
    resourceFieldSize.currentValue = 0.5f;
    resourceRichness.ChangePositionAnchor(Vec2f{0.30f, 0.245f});
    resourceRichness.ChangeSizeAnchor(Vec2f{0.40f, 0.035f});
    resourceRichness.currentValue = 0.5f;

    auto configureSlider = [](SliderBar& slider, float y, float value)
    {
        slider.ChangePositionAnchor(Vec2f{0.30f, y});
        slider.ChangeSizeAnchor(Vec2f{0.40f, 0.035f});
        slider.currentValue = value;
        slider.previousValue = value;
    };
    configureSlider(globalProvinceCount, 0.300f, ProvinceCountSliderDefault);
    configureSlider(globalProvinceSpacing, 0.345f, 0.5f);
    configureSlider(globalBuildableWeight, 0.390f, 0.55f);
    configureSlider(globalCityWeight, 0.435f, 0.20f);
    configureSlider(globalBanditWeight, 0.480f, 0.15f);
    configureSlider(globalEventWeight, 0.525f, 0.10f);
    configureSlider(globalBuildableWealth, 0.580f, 0.5f);
    configureSlider(globalCityWealth, 0.625f, 0.5f);
    configureSlider(globalBanditStrength, 0.670f, 0.5f);
    configureSlider(globalMapFogOfWar, 0.715f, 1.0f);

    debugMode.ChangeText("Debug mode");
    debugMode.ChangePositionAnchor(Vec2f{0.30f, 0.765f});
    debugMode.ChangeSizeAnchor(Vec2f{0.20f, 0.035f});

    startGame.ChangeText("Start game");
    startGame.ChangePositionAnchor(Vec2f{0.19f, 0.845f});
    startGame.ChangeSizeAnchor(Vec2f{0.18f, 0.065f});
    startGame.func = std::bind(&NewGameScene::OnStartPressed, this);

    startTutorial.ChangeText("Tutorial");
    startTutorial.ChangePositionAnchor(Vec2f{0.41f, 0.845f});
    startTutorial.ChangeSizeAnchor(Vec2f{0.18f, 0.065f});
    startTutorial.func = std::bind(&NewGameScene::OnTutorialPressed, this);

    tooltipWidget.func = [this](double)
    {
        Vector2 mouse = GetMousePosition();
        Vec2i mp{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
        if (sizeButton.ContainsPoint(mp))
            Tooltip::Draw("Map size", {
                "Sets the tile dimensions of the generated world.",
                "{separator}",
                "S 201x201 - fast to generate, good for testing",
                "M 301x301 - standard game",
                "L 401x401 - large, longer game",
                "XL 501x501 - very large, expect long generation"});
        else if (resourceDensity.ContainsPoint(mp))
            Tooltip::Draw("Resource density", {"How frequently resource deposits are scattered across the map.", "{bonus}Higher values = more deposits, easier economy.", "{penalty}Lower values = sparse resources, tighter logistics required."});
        else if (resourceFieldSize.ContainsPoint(mp))
            Tooltip::Draw("Resource field size", {"How large each individual resource deposit is.", "{bonus}Higher values = bigger fields, longer before depletion.", "{penalty}Lower values = small pockets scattered around."});
        else if (resourceRichness.ContainsPoint(mp))
            Tooltip::Draw("Resource richness", {"Total amount of resources in each deposit.", "{bonus}Higher values = more total resources per field.", "{penalty}Lower values = deposits run out faster."});
        else if (globalProvinceCount.ContainsPoint(mp))
            Tooltip::Draw("Global province count", {"Number of nodes on the strategic map.", "Higher values create a longer campaign with more destinations."});
        else if (globalProvinceSpacing.ContainsPoint(mp))
            Tooltip::Draw("Global province spacing", {"Minimum distance between strategic nodes.", "The generator keeps the graph connected and bounded."});
        else if (globalBuildableWeight.ContainsPoint(mp))
            Tooltip::Draw("Buildable province weight", {"Relative chance for neutral provinces that can be colonized."});
        else if (globalCityWeight.ContainsPoint(mp))
            Tooltip::Draw("Neutral city weight", {"Relative chance for neutral trading cities."});
        else if (globalBanditWeight.ContainsPoint(mp))
            Tooltip::Draw("Bandit camp weight", {"Relative chance for hostile bandit provinces."});
        else if (globalEventWeight.ContainsPoint(mp))
            Tooltip::Draw("Event site weight", {"Relative chance for one-time event provinces."});
        else if (globalBuildableWealth.ContainsPoint(mp))
            Tooltip::Draw("Buildable province wealth", {"Scales the resource potential of newly generated buildable provinces."});
        else if (globalCityWealth.ContainsPoint(mp))
            Tooltip::Draw("City wealth", {"Scales the starting stock and trade capacity of neutral cities."});
        else if (globalBanditStrength.ContainsPoint(mp))
            Tooltip::Draw("Bandit strength", {"Scales the initial strength and pressure of bandit camps."});
        else if (globalMapFogOfWar.ContainsPoint(mp))
            Tooltip::Draw("Global map fog of war", {"Hides the details of undiscovered provinces while keeping every node in the stable map layout.", "The local province map remains fully visible."});
        else if (debugMode.ContainsPoint(mp))
            Tooltip::Draw("Debug mode", {"Uses the smallest safe map and preset resource values.", "Useful for rapid testing with a compact world."});
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
    startGame.UpdateSize(windowSize);
    startTutorial.UpdateSize(windowSize);
    sizeButton.UpdateSize(windowSize);
    resourceDensity.UpdateSize(windowSize);
    resourceFieldSize.UpdateSize(windowSize);
    resourceRichness.UpdateSize(windowSize);
    globalProvinceCount.UpdateSize(windowSize);
    globalProvinceSpacing.UpdateSize(windowSize);
    globalBuildableWeight.UpdateSize(windowSize);
    globalCityWeight.UpdateSize(windowSize);
    globalBanditWeight.UpdateSize(windowSize);
    globalEventWeight.UpdateSize(windowSize);
    globalBuildableWealth.UpdateSize(windowSize);
    globalCityWealth.UpdateSize(windowSize);
    globalBanditStrength.UpdateSize(windowSize);
    globalMapFogOfWar.UpdateSize(windowSize);
    debugMode.UpdateSize(windowSize);
}

// Advances this object's state for one frame.
void NewGameScene::Update(double dt)
{
    ProcessGuiInput(dt);
    RefreshOptionLabels();

    std::vector<UiWidget*> widgets{&menuBackground, &menuPanel, &sizeButton,
                                   &resourceDensity, &resourceFieldSize, &resourceRichness,
                                   &globalProvinceCount, &globalProvinceSpacing,
                                   &globalBuildableWeight, &globalCityWeight,
                                   &globalBanditWeight, &globalEventWeight,
                                   &globalBuildableWealth, &globalCityWealth,
                                   &globalBanditStrength, &globalMapFogOfWar,
                                   &debugMode};
    widgets.insert(widgets.end(), {&startGame, &startTutorial, &backButton, &tooltipWidget});
    render.Draw(widgets, dt);
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
    msg->name = "new_game";
    msg->params = BuildCampaignParameters();
    broker->Broadcast(msg);
}

void NewGameScene::OnTutorialPressed()
{
    auto msg = std::make_shared<TutorialGameEvent>();
    msg->sender = this;
    msg->name = "tutorial";
    msg->params = BuildCampaignParameters();
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
    ApplySinglePlayerParameters(params);
    params.debugMode = debugMode.IsActive();
    if (params.debugMode)
        ApplyDebugLocalMapPreset(params);

    return params;
}

CampaignGenerationParameters NewGameScene::BuildCampaignParameters()
{
    CampaignGenerationParameters campaign{BuildMapParameters()};
    campaign.globalMap.seed = campaign.localMap.seed;
    campaign.globalMap.provinceCount = SliderToInt(
        globalProvinceCount.GetValue(), MinimumProvinceCount, MaximumProvinceCount);
    campaign.globalMap.minimumLayoutSpacing = SliderToInt(globalProvinceSpacing.GetValue(), 90, 160);
    // Keep layout radius and optional edge density coupled to the number of
    // nodes. The sliders therefore expose the meaningful controls without
    // allowing an impossible graph configuration.
    const int provinceDelta = campaign.globalMap.provinceCount - MinimumProvinceCount;
    campaign.globalMap.layoutRadius = static_cast<int>(std::round(
        800.0 * std::sqrt(static_cast<double>(campaign.globalMap.provinceCount) /
                          MinimumProvinceCount)));
    campaign.globalMap.extraEdgeCount = std::max(5, static_cast<int>(std::round(
        6.0 + provinceDelta * 0.375)));
    campaign.globalMap.buildableWeight = SliderToInt(globalBuildableWeight.GetValue(), 0, 100);
    campaign.globalMap.neutralCityWeight = SliderToInt(globalCityWeight.GetValue(), 0, 100);
    campaign.globalMap.banditCampWeight = SliderToInt(globalBanditWeight.GetValue(), 0, 100);
    campaign.globalMap.eventSiteWeight = SliderToInt(globalEventWeight.GetValue(), 0, 100);
    if (campaign.globalMap.buildableWeight + campaign.globalMap.neutralCityWeight +
            campaign.globalMap.banditCampWeight + campaign.globalMap.eventSiteWeight <= 0)
        campaign.globalMap.buildableWeight = 1;
    campaign.globalMap.buildableWealthScale = 0.75 + globalBuildableWealth.GetValue() * 0.75;
    campaign.globalMap.cityWealthScale = 0.75 + globalCityWealth.GetValue() * 0.75;
    campaign.globalMap.banditStrengthScale = 0.75 + globalBanditStrength.GetValue() * 0.75;
    campaign.globalMap.fogOfWarEnabled = globalMapFogOfWar.GetValue() >= 0.5f;
    return campaign;
}

void NewGameScene::ApplySinglePlayerParameters(MapParameters& params)
{
    // AI remains a compatibility seam in MapParameters, but new games never
    // expose or create AI slots.
    params.aiOpponentCount = 0;
    params.aiDifficulty = 0;
}

// Handles the UI action represented by OnSizePressed.
void NewGameScene::OnSizePressed()
{
    int next = (static_cast<int>(selectedSize) + 1) % 4;
    selectedSize = static_cast<MapSizePreset>(next);
    RefreshOptionLabels();
}

// Initializes NewGameScene::RefreshOptionLabels.
void NewGameScene::RefreshOptionLabels()
{
    sizeButton.ChangeText("Map size: " + MapSizeName(selectedSize));
    resourceDensity.ChangeText("Resource density " + std::to_string(SliderToInt(resourceDensity.GetValue(), 50, 225)) + "%");
    resourceFieldSize.ChangeText("Field size " + std::to_string(SliderToInt(resourceFieldSize.GetValue(), 65, 200)) + "%");
    resourceRichness.ChangeText("Richness " + std::to_string(SliderToInt(resourceRichness.GetValue(), 30, 250)));
    if (debugMode.IsActive())
        sizeButton.ChangeText("Map size: DEBUG 201x201");
    globalProvinceCount.ChangeText("Global provinces " +
        std::to_string(SliderToInt(globalProvinceCount.GetValue(),
                                   MinimumProvinceCount, MaximumProvinceCount)));
    globalProvinceSpacing.ChangeText("Global spacing " +
        std::to_string(SliderToInt(globalProvinceSpacing.GetValue(), 90, 160)));
    globalBuildableWeight.ChangeText("Buildable weight " +
        std::to_string(SliderToInt(globalBuildableWeight.GetValue(), 0, 100)));
    globalCityWeight.ChangeText("City weight " +
        std::to_string(SliderToInt(globalCityWeight.GetValue(), 0, 100)));
    globalBanditWeight.ChangeText("Bandit weight " +
        std::to_string(SliderToInt(globalBanditWeight.GetValue(), 0, 100)));
    globalEventWeight.ChangeText("Event weight " +
        std::to_string(SliderToInt(globalEventWeight.GetValue(), 0, 100)));
    globalBuildableWealth.ChangeText("Buildable wealth " +
        std::to_string(SliderToInt(globalBuildableWealth.GetValue(), 75, 150)) + "%");
    globalCityWealth.ChangeText("City wealth " +
        std::to_string(SliderToInt(globalCityWealth.GetValue(), 75, 150)) + "%");
    globalBanditStrength.ChangeText("Bandit strength " +
        std::to_string(SliderToInt(globalBanditStrength.GetValue(), 75, 150)) + "%");
    globalMapFogOfWar.ChangeText(globalMapFogOfWar.GetValue() >= 0.5f
        ? "Global map fog of war: On" : "Global map fog of war: Off");
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
        startGame.UpdateSize(ptr->windowSize);
        startTutorial.UpdateSize(ptr->windowSize);
        sizeButton.UpdateSize(ptr->windowSize);
        resourceDensity.UpdateSize(ptr->windowSize);
        resourceFieldSize.UpdateSize(ptr->windowSize);
        resourceRichness.UpdateSize(ptr->windowSize);
        globalProvinceCount.UpdateSize(ptr->windowSize);
        globalProvinceSpacing.UpdateSize(ptr->windowSize);
        globalBuildableWeight.UpdateSize(ptr->windowSize);
        globalCityWeight.UpdateSize(ptr->windowSize);
        globalBanditWeight.UpdateSize(ptr->windowSize);
        globalEventWeight.UpdateSize(ptr->windowSize);
        globalBuildableWealth.UpdateSize(ptr->windowSize);
        globalCityWealth.UpdateSize(ptr->windowSize);
        globalBanditStrength.UpdateSize(ptr->windowSize);
        globalMapFogOfWar.UpdateSize(ptr->windowSize);
        debugMode.UpdateSize(ptr->windowSize);
    }
}
