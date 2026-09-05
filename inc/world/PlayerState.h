#ifndef WORLD_PLAYER_STATE_H
#define WORLD_PLAYER_STATE_H

#include "data/StrategicResource.h"
#include "economy/BalanceModifiers.h"
#include "research/Technology.h"
#include "warfare/BattleUnit.h"
#include "warfare/TaskGroup.h"
#include "world/WorldIds.h"
#include "raylib.h"

#include <string>

enum class PlayerControllerType
{
    LocalHuman,
    AI,
    Remote
};

// Campaign-wide player state. It deliberately contains no TileMap, road or
// building pointer: moving a player between provinces must not invalidate it.
struct PlayerState
{
    PlayerId id{InvalidPlayerId};
    ProvinceId homeProvinceId{InvalidProvinceId};
    std::string name{"Player"};
    Color color{66, 154, 255, 255};
    PlayerControllerType controllerType{PlayerControllerType::LocalHuman};
    bool debugMode{false};

    StrategicResourcePool strategicResources;
    TechnologyState technologies;
    FocusState focuses;
    BalanceModifierSet balanceModifiers;
    UnitRoster roster;
    TaskGroupRegistry taskGroups;
    int nextUnitInstanceId{1};
};

#endif
