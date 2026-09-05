#include "core/GameWorld.h"
#include "core/GameSession.h"
#include "economy/BuildingConfig.h"
#include "economy/BuildingComponents.h"
#include "scenes/MultiplayerLobbyProtocol.h"
#include "scenes/Scenes.h"
#include "ui/Renderer.h"
#include "warfare/BattleUnit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace
{
    MapParameters MakePeacefulDebugParameters(unsigned int seed)
    {
        MapParameters params;
        params.sizePreset = MapSizePreset::S;
        params.sizeX = MapGenerator::SizeFromPreset(MapSizePreset::S);
        params.sizeY = params.sizeX;
        params.seed = seed;
        params.aiOpponentCount = 0;
        params.debugMode = true;
        return params;
    }

    MapParameters MakeSeedSweepParameters(MapSizePreset preset, unsigned int seed)
    {
        MapParameters params;
        params.sizePreset = preset;
        params.sizeX = MapGenerator::SizeFromPreset(preset);
        params.sizeY = params.sizeX;
        params.seed = seed;
        // These compatibility values deliberately exercise the world-init
        // contract: active single-player and transitional MP must ignore AI.
        params.aiOpponentCount = 5;
        params.aiDifficulty = 3;
        params.resourceDensity = 0.5f;
        params.resourceFieldSize = 0.5f;
        params.debugMode = false;
        return params;
    }

    std::optional<Vec2i> FindBuildAnchor(GameWorld& world,
                                         Player& player,
                                         BuildingType type)
    {
        const TileMap& map = world.GetTileMap();
        const Vec2i footprint = GetBuildingDefinition(type).footprint;
        for (int y = 0; y < map.params.sizeY; ++y)
        {
            for (int x = 0; x < map.params.sizeX; ++x)
            {
                const Vec2i anchor{x, y};
                if (!map.CanPlaceBuilding(type, anchor, footprint,
                                          &player))
                    continue;
                if (IsFogOfWarPreferenceEnabled() &&
                    !world.IsBuildFootprintVisibleToPlayer(player.id, anchor, footprint))
                    continue;
                return anchor;
            }
        }
        return std::nullopt;
    }

    bool SubmitAndAccept(GameWorld& world, const GameCommand& command)
    {
        const std::uint64_t commandId = world.SubmitCommand(command);
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        const auto results = world.ConsumeCommandResults();
        return results.size() == 1 &&
               results.front().commandId == commandId &&
               results.front().accepted;
    }

    class FogPreferenceGuard
    {
    public:
        FogPreferenceGuard() : previous(IsFogOfWarPreferenceEnabled()) {}
        ~FogPreferenceGuard() { SetFogOfWarPreferenceEnabled(previous); }

        FogPreferenceGuard(const FogPreferenceGuard&) = delete;
        FogPreferenceGuard& operator=(const FogPreferenceGuard&) = delete;

    private:
        bool previous;
    };
}

TEST(PeacefulWorldTests, SinglePlayerSupportsProductionRoadsAndRecruitment)
{
    FogPreferenceGuard fogPreference;
    SetFogOfWarPreferenceEnabled(false);

    GameWorld world;
    ASSERT_TRUE(world.InitWorld(
        "peaceful-contract", nullptr,
        MakePeacefulDebugParameters(20260903u)));
    ASSERT_TRUE(world.IsInitialized());
    ASSERT_EQ(world.GetPlayerHandler().players.size(), 1u);

    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    EXPECT_EQ(player->controllerType, PlayerControllerType::LocalHuman);

    const auto roadAnchor = FindBuildAnchor(world, *player, BuildingType::Road);
    ASSERT_TRUE(roadAnchor.has_value());
    ASSERT_TRUE(SubmitAndAccept(
        world, GameCommand::BuildBuilding(0, BuildingType::Road, *roadAnchor)));
    EXPECT_GE(player->GetTrackedBuildingCount(BuildingType::Road), 1);

    const auto woodcutterAnchor = FindBuildAnchor(world, *player, BuildingType::Woodcutter);
    ASSERT_TRUE(woodcutterAnchor.has_value());
    ASSERT_TRUE(SubmitAndAccept(
        world, GameCommand::BuildBuilding(0, BuildingType::Woodcutter, *woodcutterAnchor)));
    Building* woodcutter = world.GetTileMap().GetBuilding(*woodcutterAnchor);
    ASSERT_NE(woodcutter, nullptr);
    ASSERT_EQ(woodcutter->buildingType, BuildingType::Woodcutter);

    const auto barracksAnchor = FindBuildAnchor(world, *player, BuildingType::Barracks);
    ASSERT_TRUE(barracksAnchor.has_value());
    ASSERT_TRUE(SubmitAndAccept(
        world, GameCommand::BuildBuilding(0, BuildingType::Barracks, *barracksAnchor)));
    auto* barracks = dynamic_cast<Barracks*>(
        world.GetTileMap().GetBuilding(*barracksAnchor));
    ASSERT_NE(barracks, nullptr);

    for (int tick = 0; tick < 500; ++tick)
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
    EXPECT_GT(woodcutter->GetTotalProduced(), 0)
        << "the peaceful world must keep ordinary production running";

    auto foodIt = barracks->storage.buffers.find(ResourceType::FOOD_PROVISIONS);
    ASSERT_NE(foodIt, barracks->storage.buffers.end());
    foodIt->second.SetStoredAmount(3);
    player->strategicResources.Set(StrategicResourceType::Manpower, 100.0);

    ASSERT_TRUE(SubmitAndAccept(
        world, GameCommand::RecruitUnit(0, barracks->positionId, "militia")));
    for (int tick = 0; tick < 1000 && player->roster.units.empty(); ++tick)
        world.UpdateSimulation(FixedSimulationClock::FixedDt);

    ASSERT_EQ(player->roster.units.size(), 1u);
    EXPECT_EQ(player->roster.units.begin()->second.unitDefId, "militia");
}

TEST(PeacefulWorldTests, TenThousandTicksDoNotCreateAutomaticCommandsOrPlayers)
{
    GameWorld world;
    MapParameters params = MakePeacefulDebugParameters(20260904u);
    ASSERT_TRUE(world.InitWorld("peaceful-smoke", nullptr, params));

    bool sawAutomaticCommand = false;
    for (int tick = 0; tick < 10000; ++tick)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        sawAutomaticCommand = sawAutomaticCommand ||
                              !world.ConsumeCommandResults().empty();
    }

    ASSERT_FALSE(sawAutomaticCommand)
        << "a peaceful single-player smoke test must not receive commands from an AI controller";
    ASSERT_EQ(world.GetPlayerHandler().players.size(), 1u);
    EXPECT_EQ(world.GetPlayerHandler().players.begin()->second->controllerType,
              PlayerControllerType::LocalHuman);
}

TEST(PeacefulWorldTests, RosterRoundTripDoesNotRequireDeployment)
{
    GameWorld world;
    MapParameters params = MakePeacefulDebugParameters(20260905u);
    ASSERT_TRUE(world.InitWorld("roster-round-trip", nullptr, params));
    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);

    BattleUnit militia(1, 0, "militia");
    BattleUnit swordsman(2, 0, "swordsman");
    player->roster.AddUnit(militia);
    player->roster.AddUnit(swordsman);
    player->nextUnitInstanceId = 3;

    const std::uint64_t checksumBefore = world.BuildChecksum();
    const auto path = std::filesystem::temp_directory_path() /
                      "tvorin_stage0_roster_round_trip.save";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    ASSERT_TRUE(world.SaveToFile(path.string()));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path.string(), nullptr));
    std::filesystem::remove(path, ignored);

    ASSERT_EQ(loaded.GetPlayerHandler().players.size(), 1u);
    const Player* loadedPlayer = loaded.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(loadedPlayer, nullptr);
    ASSERT_EQ(loadedPlayer->roster.units.size(), 2u);
    EXPECT_EQ(loadedPlayer->nextUnitInstanceId, 3);
    EXPECT_EQ(loaded.BuildChecksum(), checksumBefore);
}

TEST(PeacefulWorldTests, ReportsStage0MapAndStateSizeBaseline)
{
    struct PresetCase
    {
        MapSizePreset preset;
        unsigned int seed;
        const char* label;
    };
    constexpr PresetCase cases[]{{MapSizePreset::S, 20260906u, "S"},
                                 {MapSizePreset::XL, 20260907u, "XL"}};

    for (const PresetCase& presetCase : cases)
    {
        MapParameters params;
        params.sizePreset = presetCase.preset;
        params.sizeX = MapGenerator::SizeFromPreset(presetCase.preset);
        params.sizeY = params.sizeX;
        params.seed = presetCase.seed;
        params.aiOpponentCount = 0;

        const auto generationStarted = std::chrono::steady_clock::now();
        GameWorld world;
        ASSERT_TRUE(world.InitWorld(
            std::string("stage0-baseline-") + presetCase.label,
            nullptr, params));
        const auto generationFinished = std::chrono::steady_clock::now();
        const auto generationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            generationFinished - generationStarted).count();

        const std::string presentationSnapshot = world.BuildSnapshot().Serialize();
        const std::string fullSimulationState = world.SerializeSimulationState();
        ASSERT_FALSE(fullSimulationState.empty());

        const auto path = std::filesystem::temp_directory_path() /
                          (std::string("tvorin_stage0_baseline_") + presetCase.label + ".save");
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        ASSERT_TRUE(world.SaveToFile(path.string()));
        const std::uintmax_t saveBytes = std::filesystem::file_size(path);
        std::filesystem::remove(path, ignored);

        fprintf(stderr,
                "[STAGE0-BASELINE] preset=%s map=%dx%d generation_ms=%lld "
                "save_bytes=%llu presentation_snapshot_bytes=%zu full_state_bytes=%zu\n",
                presetCase.label, world.GetTileMap().params.sizeX,
                world.GetTileMap().params.sizeY,
                static_cast<long long>(generationMs),
                static_cast<unsigned long long>(saveBytes),
                presentationSnapshot.size(), fullSimulationState.size());
    }
}

TEST(PeacefulWorldTests, NewGameParametersNeverCreateAiSlots)
{
    MapParameters params;
    params.aiOpponentCount = 5;
    params.aiDifficulty = 3;
    NewGameScene::ApplySinglePlayerParameters(params);

    EXPECT_EQ(params.aiOpponentCount, 0);
    EXPECT_EQ(params.aiDifficulty, 0);
}

TEST(PeacefulWorldTests, MultiplayerStartPayloadCreatesTwoHumanSlots)
{
    MapParameters source;
    source.sizePreset = MapSizePreset::S;
    source.resourceDensity = 0.72f;
    source.resourceFieldSize = 0.38f;
    source.resourceRichness = 137;
    source.aiOpponentCount = 5;
    source.aiDifficulty = 3;

    const std::string serialized = MultiplayerLobbyProtocol::SerializeStart("two-human-slots", source);
    ASSERT_EQ(serialized.rfind("START ", 0), 0u);

    std::string sessionName;
    MapParameters parsed;
    ASSERT_TRUE(MultiplayerLobbyProtocol::TryDeserializeStart(
        serialized.substr(6), sessionName, parsed));
    EXPECT_EQ(sessionName, "two-human-slots");
    EXPECT_EQ(parsed.aiOpponentCount, 0);
    EXPECT_EQ(parsed.aiDifficulty, 0);

    GameWorld host;
    GameWorld client;
    ASSERT_TRUE(host.InitMultiplayerWorld(
        sessionName, nullptr, parsed, 0, true));
    ASSERT_TRUE(client.InitMultiplayerWorld(
        sessionName, nullptr, parsed, 1, false));

    ASSERT_EQ(host.GetPlayerHandlerForTesting().players.size(), 2u);
    ASSERT_EQ(client.GetPlayerHandlerForTesting().players.size(), 2u);
    EXPECT_EQ(host.GetPlayerHandlerForTesting().players.at(0)->controllerType,
              PlayerControllerType::LocalHuman);
    EXPECT_EQ(host.GetPlayerHandlerForTesting().players.at(1)->controllerType,
              PlayerControllerType::Remote);
    EXPECT_EQ(client.GetPlayerHandlerForTesting().players.at(0)->controllerType,
              PlayerControllerType::Remote);
    EXPECT_EQ(client.GetPlayerHandlerForTesting().players.at(1)->controllerType,
              PlayerControllerType::LocalHuman);
}

TEST(PeacefulWorldTests, LegacyLobbyAiTokensDoNotShiftSettings)
{
    const std::string legacyPayload =
        "\"legacy-session\" \"legacy-host\" \"legacy-client\" "
        "5 2 3 0.27 0.81 99 1";

    std::string sessionName;
    std::string hostName;
    std::string remoteName;
    MapParameters params;
    ASSERT_TRUE(MultiplayerLobbyProtocol::TryDeserializeState(
        legacyPayload, sessionName, hostName, remoteName, params));

    EXPECT_EQ(sessionName, "legacy-session");
    EXPECT_EQ(hostName, "legacy-host");
    EXPECT_EQ(remoteName, "legacy-client");
    EXPECT_EQ(params.sizePreset, MapSizePreset::L);
    EXPECT_FLOAT_EQ(params.resourceDensity, 0.27f);
    EXPECT_FLOAT_EQ(params.resourceFieldSize, 0.81f);
    EXPECT_EQ(params.resourceRichness, 99);
    EXPECT_TRUE(params.debugMode);
    EXPECT_EQ(params.aiOpponentCount, 0);
    EXPECT_EQ(params.aiDifficulty, 0);
}

TEST(PeacefulWorldTests, VersionedCampaignLobbyPayloadRoundTripsGlobalSettings)
{
    CampaignGenerationParameters source;
    source.localMap.sizePreset = MapSizePreset::L;
    source.localMap.resourceDensity = 0.72f;
    source.localMap.resourceFieldSize = 0.38f;
    source.localMap.resourceRichness = 137;
    source.localMap.seed = 123456u;
    source.localMap.debugMode = true;
    source.globalMap.seed = 987654u;
    source.globalMap.provinceCount = 42;
    source.globalMap.extraEdgeCount = 17;
    source.globalMap.layoutRadius = 1800;
    source.globalMap.minimumLayoutSpacing = 145;
    source.globalMap.maximumPlacementAttemptsPerProvince = 3210;
    source.globalMap.minimumNeutralBuildables = 4;
    source.globalMap.buildableWeight = 61;
    source.globalMap.neutralCityWeight = 19;
    source.globalMap.banditCampWeight = 13;
    source.globalMap.eventSiteWeight = 7;
    source.globalMap.startBoundaryClearance = 410;
    source.globalMap.buildableWealthScale = 1.35;
    source.globalMap.cityWealthScale = 1.15;
    source.globalMap.banditStrengthScale = 0.8;
    source.globalMap.fogOfWarEnabled = false;

    const std::string serialized =
        MultiplayerLobbyProtocol::SerializeStart("campaign-v2", source);
    ASSERT_EQ(serialized.rfind("START ", 0), 0u);

    std::string sessionName;
    CampaignGenerationParameters parsed;
    ASSERT_TRUE(MultiplayerLobbyProtocol::TryDeserializeStart(
        serialized.substr(6), sessionName, parsed));
    EXPECT_EQ(sessionName, "campaign-v2");
    EXPECT_EQ(parsed.localMap.sizePreset, source.localMap.sizePreset);
    EXPECT_FLOAT_EQ(parsed.localMap.resourceDensity, source.localMap.resourceDensity);
    EXPECT_FLOAT_EQ(parsed.localMap.resourceFieldSize, source.localMap.resourceFieldSize);
    EXPECT_EQ(parsed.localMap.resourceRichness, source.localMap.resourceRichness);
    EXPECT_EQ(parsed.localMap.seed, source.localMap.seed);
    EXPECT_EQ(parsed.globalMap.seed, source.globalMap.seed);
    EXPECT_EQ(parsed.globalMap.provinceCount, source.globalMap.provinceCount);
    EXPECT_EQ(parsed.globalMap.extraEdgeCount, source.globalMap.extraEdgeCount);
    EXPECT_EQ(parsed.globalMap.minimumLayoutSpacing, source.globalMap.minimumLayoutSpacing);
    EXPECT_EQ(parsed.globalMap.minimumNeutralBuildables,
              source.globalMap.minimumNeutralBuildables);
    EXPECT_DOUBLE_EQ(parsed.globalMap.buildableWealthScale,
                     source.globalMap.buildableWealthScale);
    EXPECT_DOUBLE_EQ(parsed.globalMap.cityWealthScale, source.globalMap.cityWealthScale);
    EXPECT_DOUBLE_EQ(parsed.globalMap.banditStrengthScale,
                     source.globalMap.banditStrengthScale);
    EXPECT_EQ(parsed.globalMap.fogOfWarEnabled, source.globalMap.fogOfWarEnabled);
}

TEST(PeacefulWorldTests, CampaignLobbyParserRejectsInvalidGlobalFogSetting)
{
    CampaignGenerationParameters source;
    const std::string serialized =
        MultiplayerLobbyProtocol::SerializeStart("campaign-invalid-fog", source);
    const std::size_t lastSpace = serialized.find_last_of(' ');
    ASSERT_NE(lastSpace, std::string::npos);
    const std::string invalid = serialized.substr(0, lastSpace + 1) + "2";

    std::string sessionName;
    CampaignGenerationParameters parsed;
    EXPECT_FALSE(MultiplayerLobbyProtocol::TryDeserializeStart(
        invalid.substr(6), sessionName, parsed));
}

TEST(PeacefulWorldTests, CampaignLobbyParserAcceptsHistoricalPayloadWithSafeDefaults)
{
    const std::string legacyPayload =
        "\"legacy-campaign\" 0 0 0 0.27 0.81 99 1";
    std::string sessionName;
    CampaignGenerationParameters parsed;
    ASSERT_TRUE(MultiplayerLobbyProtocol::TryDeserializeStart(
        legacyPayload, sessionName, parsed));
    EXPECT_EQ(sessionName, "legacy-campaign");
    EXPECT_FLOAT_EQ(parsed.localMap.resourceDensity, 0.27f);
    EXPECT_EQ(parsed.localMap.resourceRichness, 99);
    EXPECT_EQ(parsed.globalMap.seed, parsed.localMap.seed);
    EXPECT_EQ(parsed.globalMap.provinceCount, 32);
}

TEST(PeacefulWorldTests, ProvincePresetSizesAreCanonical)
{
    EXPECT_EQ(MapGenerator::SizeFromPreset(MapSizePreset::S), 201);
    EXPECT_EQ(MapGenerator::SizeFromPreset(MapSizePreset::M), 301);
    EXPECT_EQ(MapGenerator::SizeFromPreset(MapSizePreset::L), 401);
    EXPECT_EQ(MapGenerator::SizeFromPreset(MapSizePreset::XL), 501);
}

TEST(PeacefulWorldTests, SelectingAProvincePresetSuppliesItsImplicitDimensions)
{
    constexpr std::array<MapSizePreset, 3> nonDefaultPresets{
        MapSizePreset::M, MapSizePreset::L, MapSizePreset::XL};
    for (MapSizePreset preset : nonDefaultPresets)
    {
        MapParameters parameters;
        parameters.sizePreset = preset;
        parameters.seed = 20260908u + static_cast<unsigned int>(preset);
        TileMap map;
        map.generator.GenerateTileMap(map, parameters);

        const int expected = MapGenerator::SizeFromPreset(preset);
        EXPECT_EQ(parameters.sizeX, expected);
        EXPECT_EQ(parameters.sizeY, expected);
        EXPECT_EQ(map.params.sizeX, expected);
        EXPECT_EQ(map.params.sizeY, expected);
    }
}

TEST(PeacefulWorldTests, SinglePlayerWorldIsPeacefulAcrossFiftySeedsPerPreset)
{
    constexpr std::array<MapSizePreset, 4> presets{
        MapSizePreset::S, MapSizePreset::M, MapSizePreset::L, MapSizePreset::XL};

    for (size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex)
    {
        const MapSizePreset preset = presets[presetIndex];
        const int expectedSize = MapGenerator::SizeFromPreset(preset);
        for (unsigned int seedIndex = 0; seedIndex < 50; ++seedIndex)
        {
            const unsigned int seed = 0x1A2B0000u +
                static_cast<unsigned int>(presetIndex * 1000u) + seedIndex;
            GameWorld world;
            ASSERT_TRUE(world.InitWorld(
                "peaceful-seed-sweep", nullptr,
                MakeSeedSweepParameters(preset, seed)))
                << "preset=" << expectedSize << " seed=" << seed;

            ASSERT_EQ(world.GetTileMap().params.sizeX, expectedSize);
            ASSERT_EQ(world.GetTileMap().params.sizeY, expectedSize);
            ASSERT_EQ(world.GetPlayerHandler().players.size(), 1u)
                << "preset=" << expectedSize << " seed=" << seed;
            ASSERT_NE(world.GetPlayerHandler().players.at(0), nullptr);
            EXPECT_EQ(world.GetPlayerHandler().players.at(0)->controllerType,
                      PlayerControllerType::LocalHuman);
        }
    }
}

TEST(PeacefulWorldTests, TransitionalTwoPlayerWorldIsPeacefulAcrossFiftySeedsPerPreset)
{
    constexpr std::array<MapSizePreset, 4> presets{
        MapSizePreset::S, MapSizePreset::M, MapSizePreset::L, MapSizePreset::XL};

    for (size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex)
    {
        const MapSizePreset preset = presets[presetIndex];
        const int expectedSize = MapGenerator::SizeFromPreset(preset);
        for (unsigned int seedIndex = 0; seedIndex < 50; ++seedIndex)
        {
            const unsigned int seed = 0x2B3C0000u +
                static_cast<unsigned int>(presetIndex * 1000u) + seedIndex;
            GameWorld world;
            ASSERT_TRUE(world.InitMultiplayerWorld(
                "peaceful-multiplayer-seed-sweep", nullptr,
                MakeSeedSweepParameters(preset, seed), 0, true))
                << "preset=" << expectedSize << " seed=" << seed;

            ASSERT_EQ(world.GetTileMap().params.sizeX, expectedSize);
            ASSERT_EQ(world.GetTileMap().params.sizeY, expectedSize);
            ASSERT_EQ(world.GetPlayerHandler().players.size(), 2u)
                << "preset=" << expectedSize << " seed=" << seed;
            EXPECT_EQ(world.GetPlayerHandler().players.at(0)->controllerType,
                      PlayerControllerType::LocalHuman);
            EXPECT_EQ(world.GetPlayerHandler().players.at(1)->controllerType,
                      PlayerControllerType::Remote);
        }
    }
}
