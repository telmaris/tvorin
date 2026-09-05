#ifndef EVENTS_H
#define EVENTS_H

#include "core/Types.h"
#include "core/GameWorld.h"
#include "core/CampaignGeneration.h"

class EventBroker;
class Event;
class IGameTransport;

// Object that can receive events from an EventBroker.
class EventClient
{
    public:

    virtual ~EventClient() = default;

    // Handles one event delivered by a broker.
    virtual void HandleEvent(std::shared_ptr<Event>) {}

    EventBroker* broker{nullptr};
};

// Broadcast-only event dispatcher used by scenes and the game window.
class EventBroker
{
    public:
    virtual ~EventBroker() = default;

    // Registers a named event client.
    inline void AddClient(std::string name, EventClient* client)
    {
        clients[name] = client;
    }

    // Sends an event to the broker itself and every registered client.
    inline void Broadcast(std::shared_ptr<Event> e)
    {
        HandleEvent(e);

        for(auto [name, client] : clients)
        {
            client->HandleEvent(e);
        }
    }

    // Handles events received by the broker itself.
    virtual void HandleEvent(std::shared_ptr<Event>) {}

    std::map<std::string, EventClient*> clients;
};

// Base event payload.
struct Event
{
    virtual ~Event() = default;

    EventClient* sender{nullptr};
    std::string  msgName;
};

// Requests application shutdown.
struct QuitGameEvent : Event
{
    QuitGameEvent() {msgName = "QuitGameEvent";}
};

// Requests scene activation.
struct ChangeSceneEvent : Event
{
    ChangeSceneEvent() {msgName = "ChangeSceneEvent";}
    std::string sceneName;
    std::string previousSceneName;
};

// Shows a one-shot network status message after leaving a multiplayer game.
struct NetworkStatusEvent : Event
{
    NetworkStatusEvent() {msgName = "NetworkStatusEvent";}
    std::string message;
};

// Reports a render-size change.
struct WindowSizeChangedEvent : Event
{
    WindowSizeChangedEvent() {msgName = "WindowSizeChangedEvent";}

    Vec2i windowSize;
};

// Requests borderless fullscreen toggle.
struct ToggleFullscreenEvent : Event
{
    ToggleFullscreenEvent() {msgName = "ToggleFullscreenEvent";}
};

// Requests new world creation.
struct NewGameEvent : Event
{
    NewGameEvent() {msgName = "NewGameEvent";}

    CampaignGenerationParameters params;
    std::string name;
};

// Requests a generated single-player world hosted by TutorialScene.
struct TutorialGameEvent : Event
{
    TutorialGameEvent() {msgName = "TutorialGameEvent";}

    CampaignGenerationParameters params;
    std::string name;
};

// Generic scripted-tutorial signal. Gameplay can emit the same event for
// future milestones without coupling the simulation to a particular popup.
enum class TutorialTriggerType
{
    None,
    PracticeIntro,
    BuildComplete,
    LogisticsConnected,
    BasicResourcesIntro,
    BasicProductionComplete,
    FoodChainComplete,
    DecisionSelected,
    RecruitmentComplete
};

struct TutorialTriggerEvent : Event
{
    TutorialTriggerEvent() {msgName = "TutorialTriggerEvent";}

    TutorialTriggerType type{TutorialTriggerType::None};
    BuildingType buildingType{BuildingType::Building};
};

// Starts a LAN host session.
struct HostMultiplayerGameEvent : Event
{
    HostMultiplayerGameEvent() {msgName = "HostMultiplayerGameEvent";}

    CampaignGenerationParameters params;
    std::string name;
    unsigned short port{27015};
    std::shared_ptr<IGameTransport> transport;
};

// Starts a LAN client session.
struct JoinMultiplayerGameEvent : Event
{
    JoinMultiplayerGameEvent() {msgName = "JoinMultiplayerGameEvent";}

    CampaignGenerationParameters params;
    std::string name;
    std::string address{"127.0.0.1"};
    unsigned short port{27015};
    std::shared_ptr<IGameTransport> transport;
};

// Requests loading an existing save.
struct LoadGameEvent : Event
{
    LoadGameEvent() {msgName = "LoadGameEvent";}

    std::string name;
};

// Requests saving the current game.
struct SaveGameEvent : Event
{
    SaveGameEvent() {msgName = "SaveGameEvent";}

    std::string name;
};

// Notifies menus that save files changed on disk.
struct SaveListChangedEvent : Event
{
    SaveListChangedEvent() {msgName = "SaveListChangedEvent";}
};

#endif
