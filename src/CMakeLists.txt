cmake_minimum_required(VERSION 3.16)

include_directories(../inc)

target_sources(game
               PRIVATE
               core/Log.cpp
               core/SimulationState.cpp
               core/GameSession.cpp
               core/GameWorld.cpp
               core/GameWorld.TileMap.cpp
               core/GameWorld.Init.cpp
               core/GameWorld.Persistence.cpp
               core/GameWorld.Commands.cpp
               core/GameWorld.Checksum.cpp
               core/GameWorld.Render.cpp
               ai/Controller.cpp
               research/Technology.cpp
               economy/Building.cpp
               economy/BuildingSalvage.cpp
               economy/BuildingComponentsInternal.cpp
               economy/ProductionComponent.cpp
               economy/LogisticsComponent.cpp
               economy/WorkersComponent.cpp
               economy/StorageComponent.cpp
               economy/PopulationComponent.cpp
               economy/BuildingConfig.cpp
               economy/ConstructionQueue.cpp
               economy/RecruitmentComponent.cpp
               economy/Player.cpp
               economy/StockpileIndex.cpp
               scenes/Game.cpp
               simulation/RoadNetwork.cpp
               simulation/MapGenerator.cpp
               world/Province.cpp
               world/Trade.cpp
               world/JourneyTiming.cpp
               world/ColonizationService.cpp
               world/WorldJourney.cpp world/WorldEventSystem.cpp
               world/ProvinceSimulation.cpp
               world/GlobalMap.cpp
               world/ScoutExpeditionService.cpp
               warfare/UnitDefinition.cpp warfare/BattleUnit.cpp warfare/UnitAssignment.cpp warfare/TaskGroup.cpp warfare/DefenseCoverage.cpp warfare/GarrisonService.cpp warfare/RaidDamage.cpp warfare/WarfareViews.cpp
               warfare/BattleLifecycle.cpp
               multiplayer/FaultInjectingGameTransport.cpp
               multiplayer/NetworkProtocol.cpp
               multiplayer/SnapshotTransfer.cpp
               multiplayer/TcpGameTransport.cpp
               scenes/GameWindow.cpp
               scenes/StudioSplashScene.cpp
               scenes/SceneUtils.cpp
               scenes/MultiplayerLobbyProtocol.cpp
               scenes/MainMenuScene.cpp
               scenes/OptionsScene.cpp
               scenes/NewGameScene.cpp
               scenes/MultiplayerScene.cpp
               scenes/ControlsScene.cpp
               scenes/LoadGameScene.cpp
               scenes/LoadingScene.cpp
               scenes/SaveGameScene.cpp
               scenes/GameScene.cpp
               scenes/TutorialScene.cpp
               scenes/GameMenuScene.cpp
               ui/Gui.cpp
               ui/UnitTypeCard.cpp
               ui/GuiHandler.cpp
               ui/InputManager.cpp
               research/ResearchCatalog.cpp
               ui/Renderer.cpp
               ui/ShaderLibrary.cpp
               ui/WorldLighting.cpp
               ui/GuiController.cpp
               ui/GuiCommon.cpp
    ui/GuiMapWidgets.cpp
    ui/GuiGlobalMap.cpp
               ui/GameplayClock.cpp
               ui/GuiHudPanels.cpp
               ui/GuiResearchTree.cpp
               ui/GuiBuildModes.cpp
               ui/BuildingPresentation.cpp
               ui/GuiUpgrade.cpp
               ui/GuiRoster.cpp
               ui/GuiStockpile.cpp
               ui/AudioSystem.cpp
               WinmmStubs.cpp)

# Organize folders in IDE
source_group("core" FILES
             core/Log.cpp core/SimulationState.cpp core/GameWorld.cpp core/GameWorld.TileMap.cpp core/GameWorld.Init.cpp
             core/GameWorld.Persistence.cpp core/GameWorld.Commands.cpp
             core/GameWorld.Checksum.cpp core/GameWorld.Render.cpp)

source_group("ai" FILES ai/Controller.cpp)
source_group("research" FILES research/Technology.cpp research/ResearchCatalog.cpp)
source_group("economy" FILES economy/Building.cpp economy/BuildingSalvage.cpp economy/BuildingComponentsInternal.cpp
             economy/ProductionComponent.cpp economy/LogisticsComponent.cpp economy/WorkersComponent.cpp
             economy/StorageComponent.cpp economy/PopulationComponent.cpp
             economy/BuildingConfig.cpp economy/ConstructionQueue.cpp economy/RecruitmentComponent.cpp
             economy/Player.cpp economy/StockpileIndex.cpp)
source_group("simulation" FILES simulation/RoadNetwork.cpp simulation/MapGenerator.cpp)
source_group("world" FILES world/Province.cpp world/Trade.cpp world/JourneyTiming.cpp world/ColonizationService.cpp world/WorldJourney.cpp world/ProvinceSimulation.cpp world/GlobalMap.cpp world/ScoutExpeditionService.cpp
             world/ProvinceDefinition.cpp world/ColonizationDefinition.cpp world/ProvinceConnection.cpp
             world/WorldEventDefinition.cpp world/WorldEventSystem.cpp)
source_group("data" FILES)
source_group("platform" FILES)
source_group("warfare" FILES warfare/UnitDefinition.cpp warfare/BattleUnit.cpp warfare/UnitAssignment.cpp warfare/TaskGroup.cpp warfare/DefenseCoverage.cpp warfare/GarrisonService.cpp warfare/RaidDamage.cpp warfare/WarfareViews.cpp
             warfare/BattleLifecycle.cpp)
source_group("multiplayer" FILES multiplayer/FaultInjectingGameTransport.cpp multiplayer/NetworkProtocol.cpp multiplayer/SnapshotTransfer.cpp multiplayer/TcpGameTransport.cpp)
source_group("scenes" FILES scenes/Game.cpp scenes/GameWindow.cpp scenes/SceneUtils.cpp
             scenes/MultiplayerLobbyProtocol.cpp
             scenes/StudioSplashScene.cpp
             scenes/MainMenuScene.cpp scenes/OptionsScene.cpp scenes/NewGameScene.cpp
             scenes/MultiplayerScene.cpp scenes/ControlsScene.cpp scenes/LoadGameScene.cpp
             scenes/LoadingScene.cpp scenes/SaveGameScene.cpp scenes/GameScene.cpp scenes/TutorialScene.cpp scenes/GameMenuScene.cpp)
source_group("ui" FILES ui/Gui.cpp ui/UnitTypeCard.cpp ui/Renderer.cpp ui/ShaderLibrary.cpp ui/WorldLighting.cpp ui/GuiController.cpp ui/GuiCommon.cpp
             ui/GuiMapWidgets.cpp ui/GameplayClock.cpp ui/GuiHudPanels.cpp ui/GuiResearchTree.cpp
             ui/GuiBuildModes.cpp ui/BuildingPresentation.cpp ui/GuiUpgrade.cpp ui/GuiRoster.cpp ui/GuiStockpile.cpp ui/AudioSystem.cpp)
