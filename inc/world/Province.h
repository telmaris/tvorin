#ifndef WORLD_PROVINCE_H
#define WORLD_PROVINCE_H

#include "core/Types.h"
#include "data/Resource.h"
#include "world/WorldIds.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class ProvinceSimulation;
class TileMap;
class Player;

struct BuildableProvinceParameters
{
    struct ResourceDeposit
    {
        ResourceType resource{ResourceType::Null};
        double richness{1.0};
    };
    std::string definitionId;
    int sizeX{201};
    int sizeY{201};
    std::uint32_t localSeed{0};
    double resourceWealth{1.0};
    double resourceDensity{0.5};
    double resourceFieldSize{0.5};
    double resourceRichness{1.0};
    double waterAmount{0.0};
    double mountainAmount{0.0};
    double ruggedness{0.0};
    int wealthTier{0};
    std::vector<std::string> traitIds;
    // Sorted, unique campaign-level deposit profile. Local map generation
    // filters its existing patch catalog through this list.
    std::vector<ResourceType> naturalResourceTypes;
    std::vector<ResourceDeposit> resourceDeposits;
};

struct NeutralCityState
{
    std::map<ResourceType, int> stock;
    std::map<ResourceType, double> buyPrices;
    std::map<ResourceType, double> sellPrices;
    std::map<PlayerId, int> tradeScore;
    int wealthTier{0};
    double barterPenaltyMultiplier{1.25};
    std::uint64_t revision{1};

    int GetStock(ResourceType type) const;
    double GetBuyPrice(ResourceType type) const;
    double GetSellPrice(ResourceType type) const;
    int GetTradeScore(PlayerId playerId) const;
    bool SetStock(ResourceType type, int amount);
    bool SetBuyPrice(ResourceType type, double price);
    bool SetSellPrice(ResourceType type, double price);
    bool AddTradeScore(PlayerId playerId, int amount, int maximum = 1000);
    void RestoreRevision(std::uint64_t value) { revision = value == 0 ? 1 : value; }
};

enum class ProvinceKind : std::uint8_t
{
    Buildable,
    NeutralSettlement,
    BanditCamp,
    TreasureSite
};

enum class ProvinceKnowledgeLevel : std::uint8_t
{
    Hidden,
    ReachableUnknown,
    Scouted,
    Owned
};

class IProvince
{
public:
    virtual ~IProvince() = default;
    virtual ProvinceId GetId() const = 0;
    virtual ProvinceKind GetKind() const = 0;
    virtual ProvinceKnowledgeLevel GetKnowledge(PlayerId playerId) const = 0;
    virtual bool SetKnowledge(PlayerId playerId, ProvinceKnowledgeLevel level) = 0;
    virtual Vec2i GetLayoutPosition() const = 0;
    virtual bool IsBuildable() const { return GetKind() == ProvinceKind::Buildable; }
};

// Shared identity, layout and monotonic per-player discovery state. Concrete
// provinces add only the state belonging to their own domain capability.
class ProvinceBase : public IProvince
{
public:
    ProvinceBase(ProvinceId id, Vec2i layoutPosition)
        : id(id), layoutPosition(layoutPosition) {}
    ~ProvinceBase() override = default;

    ProvinceId GetId() const override { return id; }
    ProvinceKnowledgeLevel GetKnowledge(PlayerId playerId) const override;
    bool SetKnowledge(PlayerId playerId, ProvinceKnowledgeLevel level) override;
    Vec2i GetLayoutPosition() const override { return layoutPosition; }
    const std::vector<std::string>& GetTraitIds() const { return traitIds; }
    void SetTraitIds(std::vector<std::string> value) { traitIds = std::move(value); }

protected:
    ProvinceId id{InvalidProvinceId};
    Vec2i layoutPosition{};
    std::map<PlayerId, ProvinceKnowledgeLevel> knowledgeByPlayer;
    std::vector<std::string> traitIds;

private:
    friend class GlobalMap;
    void CopyKnowledgeFrom(const ProvinceBase& source)
    {
        knowledgeByPlayer = source.knowledgeByPlayer;
        traitIds = source.traitIds;
    }
};

class BuildableProvince final : public ProvinceBase
{
public:
    BuildableProvince(ProvinceId id, Vec2i layoutPosition,
                      PlayerId ownerId = InvalidPlayerId);
    ~BuildableProvince() override;

    ProvinceKind GetKind() const override { return ProvinceKind::Buildable; }
    PlayerId GetOwnerId() const { return ownerId; }
    const BuildableProvinceParameters& GetParameters() const { return parameters; }
    void SetParameters(BuildableProvinceParameters value);
    ProvinceSimulation* GetSimulation() { return simulation.get(); }
    const ProvinceSimulation* GetSimulation() const { return simulation.get(); }

    ProvinceSimulation& CreateSimulation();
    bool InstallSimulation(std::unique_ptr<ProvinceSimulation> value);
    void BindSimulationToTileMap(TileMap& tileMap);

private:
    friend class GlobalMap;
    void SetOwnerIdFromAuthority(PlayerId value);
    PlayerId ownerId{InvalidPlayerId};
    BuildableProvinceParameters parameters;
    std::unique_ptr<ProvinceSimulation> simulation;
};

class NeutralCityProvince final : public ProvinceBase
{
public:
    explicit NeutralCityProvince(ProvinceId id, Vec2i layoutPosition,
                                 std::string definitionId = {});
    ~NeutralCityProvince() override = default;

    ProvinceKind GetKind() const override { return ProvinceKind::NeutralSettlement; }
    const std::string& GetDefinitionId() const { return definitionId; }
    void SetDefinitionId(std::string value) { definitionId = std::move(value); }
    int GetWealthTier() const { return wealthTier; }
    void SetWealthTier(int value) { wealthTier = value < 0 ? 0 : value; }
    const NeutralCityState& GetState() const { return state; }
    NeutralCityState& GetStateForAuthority() { return state; }
    void SetBarterPenaltyMultiplier(double value);

private:
    std::string definitionId;
    int wealthTier{0};
    NeutralCityState state;
};

class BanditProvince final : public ProvinceBase
{
public:
    explicit BanditProvince(ProvinceId id, Vec2i layoutPosition,
                            std::string definitionId = {});
    ~BanditProvince() override = default;

    ProvinceKind GetKind() const override { return ProvinceKind::BanditCamp; }
    const std::string& GetDefinitionId() const { return definitionId; }
    void SetDefinitionId(std::string value) { definitionId = std::move(value); }
    int GetStrength() const { return strength; }
    int GetRaidPressure() const { return raidPressure; }
    const std::string& GetLootTableId() const { return lootTableId; }
    const BuildableProvinceParameters& GetFutureBuildableParameters() const
    {
        return futureBuildableParameters;
    }
    void SetFutureBuildableParameters(BuildableProvinceParameters value)
    {
        futureBuildableParameters = std::move(value);
    }
    void SetStrength(int value) { strength = value < 0 ? 0 : value; }
    void SetRaidPressure(int value) { raidPressure = value < 0 ? 0 : value; }
    void SetLootTableId(std::string value) { lootTableId = std::move(value); }

private:
    std::string definitionId;
    int strength{0};
    int raidPressure{0};
    std::string lootTableId;
    BuildableProvinceParameters futureBuildableParameters;
};

class EventProvince final : public ProvinceBase
{
public:
    explicit EventProvince(ProvinceId id, Vec2i layoutPosition,
                           std::string eventPoolId = {});
    ~EventProvince() override = default;

    ProvinceKind GetKind() const override { return ProvinceKind::TreasureSite; }
    const std::string& GetEventPoolId() const { return eventPoolId; }
    void SetEventPoolId(std::string value) { eventPoolId = std::move(value); }
    std::uint32_t GetDiscoveryAttempts(PlayerId playerId) const;
    bool HasResolvedFor(PlayerId playerId) const;
    void RecordDiscoveryAttempt(PlayerId playerId);
    void MarkResolvedFor(PlayerId playerId);
    const std::map<PlayerId, std::uint32_t>& GetDiscoveryAttemptsByPlayer() const
    {
        return discoveryAttempts;
    }
    const std::map<PlayerId, bool>& GetResolvedByPlayer() const { return resolvedByPlayer; }
    bool RestoreDiscoveryState(PlayerId playerId, std::uint32_t attempts, bool resolved);

private:
    std::string eventPoolId;
    std::map<PlayerId, std::uint32_t> discoveryAttempts;
    std::map<PlayerId, bool> resolvedByPlayer;
};

// Transitional source-compatibility shim for pre-3.0 callers. Runtime
// generation and persistence create NeutralCityProvince, BanditProvince or
// EventProvince directly; this class is deliberately capability-free.
class StaticProvince final : public ProvinceBase
{
public:
    StaticProvince(ProvinceId id, ProvinceKind kind, Vec2i layoutPosition);
    ~StaticProvince() override = default;

    ProvinceKind GetKind() const override { return kind; }

private:
    ProvinceKind kind{ProvinceKind::NeutralSettlement};
};

#endif
