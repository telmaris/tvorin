#include <gtest/gtest.h>

#include "economy/ProductionBuildings.h"
#include "ui/BuildingPresentation.h"

TEST(BuildingPresentationTests, ConstructionHasHighestPriority)
{
    Building building(1);
    building.buildTime = 10.0;
    building.constructionRemaining = 7.5;

    const auto status = BuildBuildingPresentationStatus(building);

    EXPECT_EQ(status.general, "Under construction - 25%");
}

TEST(BuildingPresentationTests, UpgradeHasPriorityOverConnectivity)
{
    Road road(1);
    auto* upgrade = road.GetComponent<UpgradeComponent>();
    ASSERT_NE(upgrade, nullptr);
    upgrade->isUpgrading = true;
    upgrade->upgradeRemaining = 1.0;

    const auto status = BuildBuildingPresentationStatus(road, true);

    EXPECT_NE(status.general.find("Upgrading to level"), std::string::npos);
    EXPECT_EQ(status.general.find("Disconnected"), std::string::npos);
}

TEST(BuildingPresentationTests, ReportsDisconnectedBuildingBeforeProductionDetails)
{
    ConfiguredProductionBuilding building(1, BuildingType::LumberMill);
    auto* workers = building.GetComponent<WorkerComponent>();
    ASSERT_NE(workers, nullptr);
    workers->assigned = workers->GetModifiedCapacity(building);

    const auto status = BuildBuildingPresentationStatus(building, true);

    EXPECT_EQ(status.general, "Disconnected from road network");
}

TEST(BuildingPresentationTests, ReportsNoWorkersBeforeMissingInputs)
{
    ConfiguredProductionBuilding building(1, BuildingType::LumberMill);

    const auto status = BuildBuildingPresentationStatus(building);

    EXPECT_EQ(status.general, "Stopped - no workers");
}

TEST(BuildingPresentationTests, ProductionStatusUsesOutputResourceInsteadOfRecipeLabel)
{
    ConfiguredProductionBuilding building(1, BuildingType::Foundry);
    auto* workers = building.GetComponent<WorkerComponent>();
    auto* production = building.GetComponent<ProductionComponent>();
    ASSERT_NE(workers, nullptr);
    ASSERT_NE(production, nullptr);
    workers->assigned = workers->GetModifiedCapacity(building);

    for (const auto& [type, amount] : production->ingredients)
    {
        auto& buffer = production->inputBuffers[type];
        while (static_cast<int>(buffer.buffer.size()) < amount)
            buffer.GenerateResource(type);
    }

    const auto status = BuildBuildingPresentationStatus(building);
    EXPECT_EQ(status.general, "Producing Iron");
    EXPECT_EQ(status.general.find("Default"), std::string::npos);
    EXPECT_EQ(status.general.find('%'), std::string::npos);
}

TEST(BuildingPresentationTests, ReportsWaitingInputsAndOutputFull)
{
    ConfiguredProductionBuilding building(1, BuildingType::LumberMill);
    auto* workers = building.GetComponent<WorkerComponent>();
    auto* production = building.GetComponent<ProductionComponent>();
    ASSERT_NE(workers, nullptr);
    ASSERT_NE(production, nullptr);
    workers->assigned = workers->GetModifiedCapacity(building);

    EXPECT_TRUE(BuildBuildingPresentationStatus(building).general.starts_with("Waiting for:"));

    for (const auto& [type, amount] : production->ingredients)
    {
        auto& buffer = production->inputBuffers[type];
        while (static_cast<int>(buffer.buffer.size()) < amount)
            buffer.GenerateResource(type);
    }

    for (auto& [type, buffer] : production->outputBuffers)
    {
        while (static_cast<int>(buffer.buffer.size()) < buffer.bufferSize)
            buffer.GenerateResource(type);
    }

    EXPECT_EQ(BuildBuildingPresentationStatus(building).general, "Output full");
}

TEST(BuildingPresentationTests, ReportsIdleAndOperationalForNonProductionBuilding)
{
    Building idle(1);
    EXPECT_EQ(BuildBuildingPresentationStatus(idle).general, "Idle");

    idle.lifetime = 1.0;
    EXPECT_EQ(BuildBuildingPresentationStatus(idle).general, "Operational");
}

TEST(BuildingPresentationTests, OpponentStatusDoesNotExposeRecipeBuffersOrProgress)
{
    ConfiguredProductionBuilding building(1, BuildingType::LumberMill);
    auto* workers = building.GetComponent<WorkerComponent>();
    ASSERT_NE(workers, nullptr);
    workers->assigned = workers->GetModifiedCapacity(building);

    const auto waiting = BuildBuildingPresentationStatus(
        building, true, BuildingPresentationAudience::Opponent);
    EXPECT_EQ(waiting.general, "Operational");
    EXPECT_TRUE(waiting.production.empty());
    EXPECT_EQ(waiting.general.find("Waiting for"), std::string::npos);
    EXPECT_EQ(waiting.general.find("Disconnected"), std::string::npos);

    building.buildTime = 10.0;
    building.constructionRemaining = 5.0;
    const auto construction = BuildBuildingPresentationStatus(
        building, false, BuildingPresentationAudience::Opponent);
    EXPECT_EQ(construction.general, "Under construction");
    EXPECT_EQ(construction.general.find('%'), std::string::npos);
}
