#include "core/GameWorld.h"
#include "economy/StockpileIndex.h"

#include <gtest/gtest.h>

namespace
{
    CampaignGenerationParameters DebugCampaign()
    {
        CampaignGenerationParameters campaign;
        campaign.localMap.debugMode = true;
        campaign.localMap.seed = 20260904u;
        campaign.globalMap.seed = campaign.localMap.seed;
        campaign.globalMap.provinceCount = 8;
        campaign.globalMap.extraEdgeCount = 5;
        campaign.globalMap.layoutRadius = 700;
        campaign.globalMap.minimumLayoutSpacing = 100;
        campaign.globalMap.maximumPlacementAttemptsPerProvince = 4000;
        campaign.globalMap.minimumNeutralBuildables = 1;
        campaign.globalMap.startBoundaryClearance = 180;
        return campaign;
    }
}

TEST(ColonizationTests, ColonizationUsesDataDefinitionAndCreatesIndependentEconomy)
{
    GameWorld world;
    ASSERT_TRUE(world.InitWorld("colonization-test", nullptr,
                               DebugCampaign()));
    Player* player = world.GetPlayerHandlerForTesting().players.at(0).get();
    ASSERT_NE(player, nullptr);
    const ProvinceId sourceId = player->homeProvinceId;
    ProvinceId targetId = InvalidProvinceId;
    for (const ProvinceId id : world.GetGlobalMap().GetProvinceIds())
    {
        auto* buildable = world.GetGlobalMap().FindBuildableProvince(id);
        if (buildable != nullptr && buildable->GetOwnerId() == InvalidPlayerId &&
            id != sourceId)
        {
            targetId = id;
            ASSERT_TRUE(world.GetGlobalMap().SetKnowledge(0, id,
                                                          ProvinceKnowledgeLevel::Scouted));
            break;
        }
    }
    ASSERT_NE(targetId, InvalidProvinceId);

    auto* source = world.GetGlobalMap().FindBuildableProvince(sourceId);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(source->GetSimulation(), nullptr);
    StockpileIndex::Deposit(source->GetSimulation()->GetEconomy(), ResourceType::WOOD, 20);
    StockpileIndex::Deposit(source->GetSimulation()->GetEconomy(), ResourceType::STONE, 10);
    StockpileIndex::Deposit(source->GetSimulation()->GetEconomy(), ResourceType::PLANKS, 10);
    ASSERT_GE(StockpileIndex::GetTotal(source->GetSimulation()->GetEconomy(),
                                       ResourceType::WOOD), 20);
    ASSERT_GE(StockpileIndex::GetTotal(source->GetSimulation()->GetEconomy(),
                                       ResourceType::STONE), 10);
    ASSERT_GE(StockpileIndex::GetTotal(source->GetSimulation()->GetEconomy(),
                                       ResourceType::PLANKS), 10);

    const auto commandId = world.SubmitCommand(
        GameCommand::ColonizeProvince(0, sourceId, targetId));
    ASSERT_NE(commandId, 0u);
    world.UpdateSimulation(0.01);
    const auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results.front().accepted) << results.front().reason;
    EXPECT_TRUE(world.IsColonizationInProgress(targetId));
    const auto initialProgress = world.GetColonizationProgress(targetId);
    ASSERT_TRUE(initialProgress.has_value());
    EXPECT_EQ(initialProgress->phase, ColonizationPhase::Traveling);
    EXPECT_GE(initialProgress->progress, 0.0f);
    EXPECT_LE(initialProgress->progress, 1.0f);
    EXPECT_EQ(world.GetGlobalMap().FindBuildableProvince(targetId)->GetOwnerId(),
              InvalidPlayerId);

    int safetyTicks = 0;
    bool observedEstablishingProgress = false;
    while (world.IsColonizationInProgress(targetId) && safetyTicks++ < 100'000)
    {
        world.UpdateSimulation(0.01);
        const auto progress = world.GetColonizationProgress(targetId);
        if (progress.has_value())
        {
            EXPECT_GE(progress->progress, 0.0f);
            EXPECT_LE(progress->progress, 1.0f);
            observedEstablishingProgress |=
                progress->phase == ColonizationPhase::Establishing;
        }
    }

    auto* target = world.GetGlobalMap().FindBuildableProvince(targetId);
    ASSERT_NE(target, nullptr);
    ASSERT_NE(target->GetSimulation(), nullptr);
    EXPECT_FALSE(world.IsColonizationInProgress(targetId));
    EXPECT_TRUE(observedEstablishingProgress);
    EXPECT_FALSE(world.GetColonizationProgress(targetId).has_value());
    EXPECT_EQ(target->GetOwnerId(), 0);
    EXPECT_EQ(target->GetKnowledge(0), ProvinceKnowledgeLevel::Owned);
    EXPECT_NE(player->GetProvinceSimulation(sourceId),
              player->GetProvinceSimulation(targetId));
    EXPECT_NE(player->GetProvinceEconomy(sourceId),
              player->GetProvinceEconomy(targetId));
    EXPECT_NE(player->GetTileMap(sourceId), player->GetTileMap(targetId));
    EXPECT_EQ(player->GetProvinceEconomy(sourceId)->construction.QueueLength(), 0u);
    EXPECT_EQ(player->GetProvinceEconomy(targetId)->construction.QueueLength(), 0u);

    const auto& targetBuildings = target->GetSimulation()->GetEconomy().dataTracker.buildings;
    EXPECT_TRUE(std::any_of(targetBuildings.begin(), targetBuildings.end(),
        [](const Building* building)
        {
            return building != nullptr && building->buildingType == BuildingType::Headquarters;
        }));
    EXPECT_GT(StockpileIndex::GetTotal(target->GetSimulation()->GetEconomy(),
                                       ResourceType::WOOD), 0);
    EXPECT_GT(StockpileIndex::GetTotal(target->GetSimulation()->GetEconomy(),
                                       ResourceType::PLANKS), 0);

    // Regression: the new province used to retain the zero-sized navigation
    // mirror created before terrain generation. Its village requested food on
    // the first tick after activation and crashed in CalculatePath.
    ASSERT_TRUE(player->SetActiveProvince(targetId));
    EXPECT_NO_FATAL_FAILURE(world.UpdateSimulation(0.01));
    EXPECT_EQ(player->GetActiveProvinceId(), targetId);
    EXPECT_EQ(world.BuildSnapshot().activeProvinceId, targetId);
}

TEST(ColonizationTests, ColonizationRejectsUnscoutedOrCompetingTarget)
{
    GameWorld world;
    ASSERT_TRUE(world.InitWorld("colonization-validation-test", nullptr,
                               DebugCampaign()));
    Player* player = world.GetPlayerHandlerForTesting().players.at(0).get();
    const ProvinceId sourceId = player->homeProvinceId;
    ProvinceId targetId = InvalidProvinceId;
    for (const ProvinceId id : world.GetGlobalMap().GetProvinceIds())
    {
        const auto* buildable = world.GetGlobalMap().FindBuildableProvince(id);
        if (buildable != nullptr && buildable->GetOwnerId() == InvalidPlayerId && id != sourceId)
        {
            targetId = id;
            break;
        }
    }
    ASSERT_NE(targetId, InvalidProvinceId);
    EXPECT_NE(world.SubmitCommand(GameCommand::ColonizeProvince(0, sourceId, targetId)), 0u);
    world.UpdateSimulation(0.01);
    auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results.front().accepted);
    EXPECT_FALSE(world.IsColonizationInProgress(targetId));
}
