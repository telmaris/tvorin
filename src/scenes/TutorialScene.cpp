#include "scenes/Scenes.h"

#include <array>
#include <cmath>
#include <mutex>

namespace
{
    struct TutorialPopupDefinition
    {
        TutorialTriggerType trigger;
        const char* title;
        const char* body;
        const char* action;
        TutorialTriggerType nextTrigger;
        bool dimBackground;
    };

    // The tutorial deliberately stays inside the peaceful local economy. The
    // final step explains that the roster will become useful on the global
    // map, without injecting an opponent or mutating simulation state.
    constexpr std::array<TutorialPopupDefinition, 9> PopupDefinitions{{
        {TutorialTriggerType::None,
         "Welcome to the tutorial",
         "Build a self-sufficient settlement, connect its logistics and learn how production supports your people. "
         "The tutorial pauses the simulation whenever an instruction window is open.",
         "Continue", TutorialTriggerType::PracticeIntro, true},
        {TutorialTriggerType::PracticeIntro,
         "Practice: production and logistics",
         "A Woodcutter harvests {resource}Wood{/resource} from a forest. Resources must still reach storage or the next consumer. "
         "Use {icon:mouse_mmb} to pan, {icon:key_q} to open Build, and place a Woodcutter on a forest tile.",
         "Start practice", TutorialTriggerType::None, true},
        {TutorialTriggerType::BuildComplete,
         "Roads keep production alive",
         "Connect the finished Woodcutter to the Headquarters with a continuous four-directional chain of your Road tiles. "
         "Press {icon:key_r} for Road Build Mode, then place or drag roads with {icon:mouse_lmb}.",
         "Connect Woodcutter", TutorialTriggerType::None, true},
        {TutorialTriggerType::LogisticsConnected,
         "Manpower and productivity",
         "Villages generate manpower and buildings use it for work. Food provisions delivered through logistics keep people productive. "
         "The HUD and building panel show the current supply and worker status.",
         "Continue", TutorialTriggerType::BasicResourcesIntro, false},
        {TutorialTriggerType::BasicResourcesIntro,
         "Basic production",
         "Build a second Woodcutter, then start the {resource}Planks{/resource}, {resource}Stone{/resource}, {resource}Iron{/resource} "
         "and {resource}Coal{/resource} chains. Place producers on matching deposits and connect producers, storage and consumers with roads.",
         "Start production", TutorialTriggerType::None, true},
        {TutorialTriggerType::BasicProductionComplete,
         "Food keeps the state running",
         "Build the food chain: Well, Wheat Farm, Windmill, Bakery, Hunter's Hut and Inn. "
         "The Inn supplies {resource}Food Provisions{/resource}; connect the chain and keep a reserve in storage.",
         "Start food production", TutorialTriggerType::None, true},
        {TutorialTriggerType::FoodChainComplete,
         "Decisions shape your state",
         "Choose an available focus or technology. It grants a lasting bonus and introduces the strategic layer of your settlement.",
         "Continue to decisions", TutorialTriggerType::None, true},
        {TutorialTriggerType::DecisionSelected,
         "Prepare the first roster",
         "A Smith produces equipment and a Barracks recruits the first roster units. Build both buildings and recruit ten Militia. "
         "Keep manpower, food provisions and logistics available while the recruitment queue works.",
         "Start recruitment", TutorialTriggerType::None, false},
        {TutorialTriggerType::RecruitmentComplete,
         "Peaceful tutorial complete",
         "Your settlement and first roster are ready. In the next stage, the roster will be used for expeditions and provincial actions on the global map.",
         "Finish tutorial", TutorialTriggerType::None, true}
    }};

    const TutorialPopupDefinition* FindPopupDefinition(TutorialTriggerType trigger)
    {
        for (const auto& definition : PopupDefinitions)
            if (definition.trigger == trigger)
                return &definition;
        return nullptr;
    }
}

TutorialScene::TutorialScene()
{
    inputs.SetDebugActionsEnabled(true);
    tutorialPopup.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    tutorialPopup.SetHorizontalOffset(-70);
    tutorialPopup.SetModalStateCallback([this](bool paused)
    {
        InputManager::SetInputEnabled(!paused);
        if (runtimeLoop != nullptr)
            runtimeLoop->SetPaused(paused);
    });
}

void TutorialScene::OnActivated()
{
    GameScene::OnActivated();
    if (pendingTutorialStart.has_value() && game != nullptr && runtimeLoop != nullptr)
        FinishPendingTutorialStart();
    InputManager::SetInputEnabled(!tutorialPopup.IsVisible());
}

void TutorialScene::HandleRenderDebugInput()
{
    GameScene::HandleRenderDebugInput();
    const int advanceTutorialKey =
        GetDefaultKeyBindings().GetKeyForAction(GameAction::AdvanceTutorialStep);
    if (advanceTutorialKey == 0 || !InputManager::IsKeyPressed(advanceTutorialKey) ||
        tutorialPopup.IsVisible())
        return;

    // F11 only advances peaceful tutorial milestones; it never creates units.
    if (activePopupTrigger == TutorialTriggerType::PracticeIntro || awaitingWoodcutter)
    {
        woodcutterCompletionReported = true;
        awaitingWoodcutter = false;
        EmitTrigger(TutorialTriggerType::BuildComplete, BuildingType::Woodcutter);
    }
    else if (activePopupTrigger == TutorialTriggerType::BuildComplete || awaitingRoadConnection)
    {
        roadConnectionReported = true;
        awaitingRoadConnection = false;
        EmitTrigger(TutorialTriggerType::LogisticsConnected);
    }
    else if (activePopupTrigger == TutorialTriggerType::BasicResourcesIntro || awaitingBasicProduction)
    {
        basicProductionReported = true;
        awaitingBasicProduction = false;
        EmitTrigger(TutorialTriggerType::BasicProductionComplete);
    }
    else if (activePopupTrigger == TutorialTriggerType::BasicProductionComplete ||
             (basicProductionReported && !foodChainReported))
    {
        foodChainReported = true;
        UnlockTutorialDecisions();
        EmitTrigger(TutorialTriggerType::FoodChainComplete);
    }
    else if (activePopupTrigger == TutorialTriggerType::FoodChainComplete ||
             (foodChainReported && !decisionSelectedReported))
    {
        decisionSelectedReported = true;
        EmitTrigger(TutorialTriggerType::DecisionSelected);
    }
    else if (activePopupTrigger == TutorialTriggerType::DecisionSelected || awaitingRecruitment)
    {
        recruitmentReported = true;
        awaitingRecruitment = false;
        EmitTrigger(TutorialTriggerType::RecruitmentComplete);
    }
}

void TutorialScene::PrepareGameplayRender()
{
    // No scripted enemy route or combat-specific fog reveal exists in the
    // peaceful tutorial.
}

void TutorialScene::Update(double dt)
{
    GameScene::Update(dt);
    UpdateTutorialTasks();

    if (game == nullptr || runtimeLoop == nullptr || tutorialPopup.IsVisible())
        return;

    bool completed = false;
    bool connected = false;
    bool basicProductionReady = false;
    bool foodChainReady = false;
    bool decisionSelected = false;
    bool recruitmentReady = false;
    if (auto* mutex = runtimeLoop->GetWorldMutex())
    {
        std::lock_guard<std::recursive_mutex> lock(*mutex);
        auto playerIt = game->GetPlayerHandler().players.find(game->GetLocalPlayerId());
        if (playerIt != game->GetPlayerHandler().players.end() && playerIt->second != nullptr)
        {
            Player* player = playerIt->second.get();
            completed = awaitingWoodcutter && !woodcutterCompletionReported &&
                        player->HasTrackedBuilding(BuildingType::Woodcutter, true);
            if (awaitingRoadConnection && !roadConnectionReported)
            {
                Building* woodcutter = nullptr;
                Building* headquarters = nullptr;
                for (Building* building : player->GetTrackedBuildings())
                {
                    if (building == nullptr || building->IsUnderConstruction())
                        continue;
                    if (building->buildingType == BuildingType::Woodcutter && woodcutter == nullptr)
                        woodcutter = building;
                    if (building->buildingType == BuildingType::Headquarters && headquarters == nullptr)
                        headquarters = building;
                }
                if (woodcutter != nullptr && headquarters != nullptr && player->GetRoadNetwork() != nullptr)
                    connected = !player->GetRoadNetwork()->CalculatePath(woodcutter, headquarters).empty();
            }
            if (awaitingBasicProduction && !basicProductionReported)
            {
                basicProductionReady =
                    player->GetTrackedBuildingCount(BuildingType::Woodcutter, true) >= 2 &&
                    player->HasTrackedBuilding(BuildingType::LumberMill, true) &&
                    player->HasTrackedBuilding(BuildingType::Mine, true) &&
                    player->HasTrackedBuilding(BuildingType::Foundry, true);
            }
            if (basicProductionReported && !foodChainReported)
            {
                for (Building* building : player->GetTrackedBuildings())
                {
                    if (building != nullptr && building->buildingType == BuildingType::Inn &&
                        !building->IsUnderConstruction() && building->GetTotalProduced() > 0)
                    {
                        foodChainReady = true;
                        break;
                    }
                }
            }
            decisionSelected = !player->focuses.GetActiveFocusId().empty() ||
                               !player->focuses.GetUnlocked().empty();
            int militiaCount = 0;
            for (const auto& [instanceId, unit] : player->roster.units)
                if (unit.unitDefId == "militia")
                    ++militiaCount;
            recruitmentReady = awaitingRecruitment && !recruitmentReported &&
                               player->HasTrackedBuilding(BuildingType::Smith, true) &&
                               player->HasTrackedBuilding(BuildingType::Barracks, true) &&
                               militiaCount >= 10;
        }
    }

    if (completed)
    {
        woodcutterCompletionReported = true;
        awaitingWoodcutter = false;
        EmitTrigger(TutorialTriggerType::BuildComplete, BuildingType::Woodcutter);
        return;
    }
    if (connected)
    {
        roadConnectionReported = true;
        awaitingRoadConnection = false;
        EmitTrigger(TutorialTriggerType::LogisticsConnected);
        return;
    }
    if (basicProductionReady)
    {
        basicProductionReported = true;
        awaitingBasicProduction = false;
        EmitTrigger(TutorialTriggerType::BasicProductionComplete);
        return;
    }
    if (foodChainReady)
    {
        foodChainReported = true;
        UnlockTutorialDecisions();
        EmitTrigger(TutorialTriggerType::FoodChainComplete);
        return;
    }
    if (foodChainReported && !decisionSelectedReported && decisionSelected)
    {
        decisionSelectedReported = true;
        EmitTrigger(TutorialTriggerType::DecisionSelected);
        return;
    }
    if (recruitmentReady)
    {
        recruitmentReported = true;
        awaitingRecruitment = false;
        EmitTrigger(TutorialTriggerType::RecruitmentComplete);
    }
}

void TutorialScene::FinishPendingTutorialStart()
{
    PendingTutorialStart pending = std::move(*pendingTutorialStart);
    pendingTutorialStart.reset();
    (void)pending;

    activePopupTrigger = TutorialTriggerType::None;
    awaitingWoodcutter = false;
    woodcutterCompletionReported = false;
    awaitingRoadConnection = false;
    roadConnectionReported = false;
    awaitingBasicProduction = false;
    basicProductionReported = false;
    foodChainReported = false;
    decisionSelectedReported = false;
    awaitingRecruitment = false;
    recruitmentReported = false;
    tutorialDecisionsUnlocked = false;
    taskCameraBaselineSet = false;
    taskBuildModeSeen = false;
    tutorialTasks.tasks.clear();
    ClearTutorialHighlights();
    SetTutorialHudLock(true);
    ShowPopupForTrigger(TutorialTriggerType::None);
}

void TutorialScene::UpdateTutorialTasks()
{
    if (game == nullptr)
        return;

    if (!taskCameraBaselineSet)
    {
        taskCameraStart = {render.camera.target.x, render.camera.target.y};
        taskCameraBaselineSet = true;
    }
    const float cameraDistance = std::abs(render.camera.target.x - taskCameraStart.x) +
                                 std::abs(render.camera.target.y - taskCameraStart.y);
    const bool cameraMoved = cameraDistance > 2.0f;
    if (controller != nullptr && controller->activeSystem != nullptr)
    {
        auto buildIt = controller->systems.find("build");
        if (buildIt != controller->systems.end() && controller->activeSystem == buildIt->second)
            taskBuildModeSeen = true;
    }

    int woodcutters = 0;
    bool lumberMill = false;
    bool mine = false;
    bool foundry = false;
    bool well = false;
    bool wheatFarm = false;
    bool windmill = false;
    bool bakery = false;
    bool huntersHut = false;
    bool inn = false;
    bool foodProvisionsProduced = false;
    bool smith = false;
    bool barracks = false;
    int militiaInRoster = 0;
    if (runtimeLoop != nullptr)
    {
        if (auto* mutex = runtimeLoop->GetWorldMutex())
        {
            std::lock_guard<std::recursive_mutex> lock(*mutex);
            auto playerIt = game->GetPlayerHandler().players.find(game->GetLocalPlayerId());
            if (playerIt != game->GetPlayerHandler().players.end() && playerIt->second != nullptr)
            {
                Player* player = playerIt->second.get();
                woodcutters = player->GetTrackedBuildingCount(BuildingType::Woodcutter, true);
                lumberMill = player->HasTrackedBuilding(BuildingType::LumberMill, true);
                mine = player->HasTrackedBuilding(BuildingType::Mine, true);
                foundry = player->HasTrackedBuilding(BuildingType::Foundry, true);
                well = player->HasTrackedBuilding(BuildingType::Well, true);
                wheatFarm = player->HasTrackedBuilding(BuildingType::WheatFarm, true);
                windmill = player->HasTrackedBuilding(BuildingType::Windmill, true);
                bakery = player->HasTrackedBuilding(BuildingType::Bakery, true);
                huntersHut = player->HasTrackedBuilding(BuildingType::HuntersHut, true);
                inn = player->HasTrackedBuilding(BuildingType::Inn, true);
                smith = player->HasTrackedBuilding(BuildingType::Smith, true);
                barracks = player->HasTrackedBuilding(BuildingType::Barracks, true);
                for (Building* building : player->GetTrackedBuildings())
                {
                    if (building != nullptr && building->buildingType == BuildingType::Inn &&
                        !building->IsUnderConstruction() && building->GetTotalProduced() > 0)
                    {
                        foodProvisionsProduced = true;
                        break;
                    }
                }
                for (const auto& [instanceId, unit] : player->roster.units)
                    if (unit.unitDefId == "militia")
                        ++militiaInRoster;
            }
        }
    }

    if (activePopupTrigger == TutorialTriggerType::PracticeIntro || awaitingWoodcutter)
    {
        tutorialTasks.SetTasks("First production", {
            {"Pan the camera", cameraMoved},
            {"Open the Build panel (Q)", taskBuildModeSeen},
            {"Build a Woodcutter in a forest", woodcutterCompletionReported}});
    }
    else if (activePopupTrigger == TutorialTriggerType::BuildComplete || awaitingRoadConnection)
    {
        tutorialTasks.SetTasks("First logistics link", {
            {"Finish the Woodcutter", woodcutterCompletionReported},
            {"Connect Woodcutter to Headquarters", roadConnectionReported}});
    }
    else if (activePopupTrigger == TutorialTriggerType::LogisticsConnected)
    {
        tutorialTasks.SetTasks("People and productivity", {
            {"Review manpower and food indicators", false},
            {"Continue to the materials objective", false}});
    }
    else if (activePopupTrigger == TutorialTriggerType::BasicResourcesIntro || awaitingBasicProduction)
    {
        tutorialTasks.SetTasks("Basic materials", {
            {"Build a second Woodcutter", woodcutters >= 2},
            {"Build a Lumber Mill", lumberMill},
            {"Build a Mine", mine},
            {"Build a Foundry", foundry}});
    }
    else if (activePopupTrigger == TutorialTriggerType::BasicProductionComplete || basicProductionReported)
    {
        tutorialTasks.SetTasks("Food chain", {
            {"Build a Well for Water", well},
            {"Build a Wheat Farm", wheatFarm},
            {"Build a Windmill", windmill},
            {"Build a Bakery", bakery},
            {"Build a Hunter's Hut for Meat", huntersHut},
            {"Build an Inn for Food Provisions", inn},
            {"Produce Food Provisions", foodProvisionsProduced}});
    }
    else if (activePopupTrigger == TutorialTriggerType::FoodChainComplete ||
             (foodChainReported && !decisionSelectedReported))
    {
        tutorialTasks.SetTasks("Decisions", {
            {"Complete the Food Provisions chain", foodChainReported},
            {"Choose any first focus or technology", decisionSelectedReported}});
    }
    else if (activePopupTrigger == TutorialTriggerType::DecisionSelected || awaitingRecruitment)
    {
        tutorialTasks.SetTasks("First roster", {
            {"Build a Smith", smith},
            {"Build a Barracks", barracks},
            {"Recruit 10 Militia", militiaInRoster >= 10}});
    }
    else if (activePopupTrigger == TutorialTriggerType::RecruitmentComplete || recruitmentReported)
    {
        tutorialTasks.SetTasks("Global map preview", {
            {"Prepare the peaceful settlement", true},
            {"Prepare the first roster", true},
            {"Continue to the global map in the next stage", true}});
    }
    else
    {
        tutorialTasks.SetTasks("Welcome", {{"Continue to the practice stage", false}});
    }
}

void TutorialScene::HandleEvent(std::shared_ptr<Event> e)
{
    if (auto resize = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e))
    {
        GameScene::HandleEvent(e);
        tutorialPopup.UpdateSize(resize->windowSize);
        return;
    }

    if (auto sceneChange = std::dynamic_pointer_cast<ChangeSceneEvent>(e))
    {
        if (sceneChange->sceneName == "MainScene")
        {
            tutorialPopup.Hide();
            activePopupTrigger = TutorialTriggerType::None;
            awaitingWoodcutter = false;
            woodcutterCompletionReported = false;
            awaitingRoadConnection = false;
            roadConnectionReported = false;
            awaitingBasicProduction = false;
            basicProductionReported = false;
            foodChainReported = false;
            decisionSelectedReported = false;
            awaitingRecruitment = false;
            recruitmentReported = false;
            tutorialDecisionsUnlocked = false;
            taskCameraBaselineSet = false;
            taskBuildModeSeen = false;
            tutorialTasks.tasks.clear();
            ClearTutorialHighlights();
            SetTutorialHudLock(false);
            ShutdownActiveGame();
        }
        return;
    }

    if (auto tutorial = std::dynamic_pointer_cast<TutorialGameEvent>(e))
    {
        pendingTutorialStart = PendingTutorialStart{tutorial->name, tutorial->params};
        return;
    }

    auto trigger = std::dynamic_pointer_cast<TutorialTriggerEvent>(e);
    if (trigger == nullptr)
        return;
    if (trigger->type == TutorialTriggerType::BuildComplete &&
        trigger->buildingType != BuildingType::Woodcutter)
        return;
    ShowPopupForTrigger(trigger->type, trigger->buildingType);
}

void TutorialScene::ShowPopupForTrigger(TutorialTriggerType trigger, BuildingType buildingType)
{
    const TutorialPopupDefinition* definition = FindPopupDefinition(trigger);
    if (definition == nullptr || tutorialPopup.IsVisible())
        return;
    if (trigger == TutorialTriggerType::BuildComplete && buildingType != BuildingType::Woodcutter)
        return;

    activePopupTrigger = trigger;
    tutorialPopup.SetDimBackground(definition->dimBackground);
    if (trigger == TutorialTriggerType::LogisticsConnected)
        OpenWoodcutterPanelAndHighlights();
    tutorialPopup.Show(definition->title, definition->body, definition->action,
                       [this]() { OnPopupDismissed(); });
}

void TutorialScene::OnPopupDismissed()
{
    const TutorialPopupDefinition* definition = FindPopupDefinition(activePopupTrigger);
    const TutorialTriggerType nextTrigger = definition != nullptr ? definition->nextTrigger : TutorialTriggerType::None;

    if (activePopupTrigger == TutorialTriggerType::PracticeIntro)
        awaitingWoodcutter = true;
    if (activePopupTrigger == TutorialTriggerType::BuildComplete)
        awaitingRoadConnection = true;
    if (activePopupTrigger == TutorialTriggerType::BasicResourcesIntro)
    {
        awaitingBasicProduction = true;
        ClearTutorialHighlights();
    }
    if (activePopupTrigger == TutorialTriggerType::DecisionSelected)
        awaitingRecruitment = true;

    activePopupTrigger = TutorialTriggerType::None;
    tutorialPopup.Hide();
    if (nextTrigger != TutorialTriggerType::None)
        EmitTrigger(nextTrigger);
}

void TutorialScene::OpenWoodcutterPanelAndHighlights()
{
    if (controller == nullptr || game == nullptr || runtimeLoop == nullptr)
        return;
    auto systemIt = controller->systems.find("default");
    if (systemIt == controller->systems.end())
        return;
    auto* basic = dynamic_cast<BasicMapViewSystem*>(systemIt->second.get());
    if (basic == nullptr)
        return;
    auto* worldMutex = runtimeLoop->GetWorldMutex();
    if (worldMutex == nullptr)
        return;
    std::lock_guard<std::recursive_mutex> lock(*worldMutex);
    auto playerIt = game->GetPlayerHandler().players.find(game->GetLocalPlayerId());
    if (playerIt == game->GetPlayerHandler().players.end() || playerIt->second == nullptr)
        return;
    Building* woodcutter = nullptr;
    for (Building* building : playerIt->second->GetTrackedBuildings())
    {
        if (building != nullptr && building->buildingType == BuildingType::Woodcutter &&
            !building->IsUnderConstruction())
        {
            woodcutter = building;
            break;
        }
    }
    if (woodcutter == nullptr)
        return;
    controller->ChangeSystem("default");
    basic->SelectBuilding(woodcutter);
    basic->buildingInfoPanel.SetTutorialHighlight(true);
    basic->strategicHudWidget.SetTutorialHighlights(true, true);
}

void TutorialScene::ClearTutorialHighlights()
{
    if (controller == nullptr)
        return;
    auto systemIt = controller->systems.find("default");
    if (systemIt == controller->systems.end())
        return;
    auto* basic = dynamic_cast<BasicMapViewSystem*>(systemIt->second.get());
    if (basic == nullptr)
        return;
    basic->buildingInfoPanel.SetTutorialHighlight(false);
    basic->strategicHudWidget.SetTutorialHighlights(false, false);
}

void TutorialScene::SetTutorialHudLock(bool locked)
{
    tutorialHudLocked = locked;
    if (controller == nullptr)
        return;
    auto systemIt = controller->systems.find("default");
    if (systemIt == controller->systems.end())
        return;
    auto* basic = dynamic_cast<BasicMapViewSystem*>(systemIt->second.get());
    if (basic != nullptr)
        basic->strategicHudWidget.SetTutorialLockedButtons(locked);
}

void TutorialScene::UnlockTutorialDecisions()
{
    tutorialDecisionsUnlocked = true;
    tutorialHudLocked = false;
    if (controller == nullptr)
        return;
    auto systemIt = controller->systems.find("default");
    if (systemIt == controller->systems.end())
        return;
    auto* basic = dynamic_cast<BasicMapViewSystem*>(systemIt->second.get());
    if (basic == nullptr)
        return;
    basic->strategicHudWidget.SetTutorialLockedButtons(true);
    basic->strategicHudWidget.SetTutorialDecisionsLocked(false);
}

void TutorialScene::EmitTrigger(TutorialTriggerType trigger, BuildingType buildingType)
{
    auto event = std::make_shared<TutorialTriggerEvent>();
    event->sender = this;
    event->type = trigger;
    event->buildingType = buildingType;
    broker->Broadcast(event);
}
