#ifndef CONTROLLER_H
#define CONTROLLER_H

class GameWorld;

class IController
{
public:
    virtual ~IController() = default;
    explicit IController(int controlledPlayerId) : playerId(controlledPlayerId) {}

    virtual void Update(GameWorld& world, double dt) = 0;

    int playerId{0};
};

class LocalController : public IController
{
public:
    explicit LocalController(int controlledPlayerId) : IController(controlledPlayerId) {}

    void Update(GameWorld& world, double dt) override;
};

class RemoteController : public IController
{
public:
    explicit RemoteController(int controlledPlayerId) : IController(controlledPlayerId) {}

    void Update(GameWorld& world, double dt) override;
};

// Deliberately empty integration seam. A future AI implementation can issue
// GameCommands from Update without changing GameWorld or player ownership.
class AIController : public IController
{
public:
    explicit AIController(int controlledPlayerId) : IController(controlledPlayerId) {}

    void Update(GameWorld& world, double dt) override;
};

#endif
