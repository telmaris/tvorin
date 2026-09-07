#include "world/Province.h"

#include "world/ProvinceSimulation.h"

#include <algorithm>
#include <cmath>
#include <limits>

int NeutralCityState::GetStock(ResourceType type) const
{
    const auto it = stock.find(type);
    return it == stock.end() ? 0 : it->second;
}

double NeutralCityState::GetBuyPrice(ResourceType type) const
{
    const auto it = buyPrices.find(type);
    return it == buyPrices.end() ? 0.0 : it->second;
}

double NeutralCityState::GetSellPrice(ResourceType type) const
{
    const auto it = sellPrices.find(type);
    return it == sellPrices.end() ? 0.0 : it->second;
}

int NeutralCityState::GetTradeScore(PlayerId playerId) const
{
    const auto it = tradeScore.find(playerId);
    return it == tradeScore.end() ? 0 : it->second;
}

bool NeutralCityState::SetStock(ResourceType type, int amount)
{
    if (type == ResourceType::Null || amount < 0)
        return false;
    stock[type] = amount;
    ++revision;
    return true;
}

bool NeutralCityState::SetBuyPrice(ResourceType type, double price)
{
    if (type == ResourceType::Null || !std::isfinite(price) || price <= 0.0)
        return false;
    buyPrices[type] = price;
    ++revision;
    return true;
}

bool NeutralCityState::SetSellPrice(ResourceType type, double price)
{
    if (type == ResourceType::Null || !std::isfinite(price) || price <= 0.0)
        return false;
    sellPrices[type] = price;
    ++revision;
    return true;
}

bool NeutralCityState::AddTradeScore(PlayerId playerId, int amount, int maximum)
{
    if (playerId == InvalidPlayerId || amount < 0 || maximum < 0)
        return false;
    const long long updated = static_cast<long long>(GetTradeScore(playerId)) + amount;
    tradeScore[playerId] = static_cast<int>(std::clamp<long long>(updated, 0, maximum));
    ++revision;
    return true;
}

ProvinceKnowledgeLevel ProvinceBase::GetKnowledge(PlayerId playerId) const
{
    const auto it = knowledgeByPlayer.find(playerId);
    return it == knowledgeByPlayer.end() ? ProvinceKnowledgeLevel::Hidden : it->second;
}

bool ProvinceBase::SetKnowledge(PlayerId playerId, ProvinceKnowledgeLevel level)
{
    if (playerId == InvalidPlayerId)
        return false;
    const ProvinceKnowledgeLevel current = GetKnowledge(playerId);
    if (level < current)
        return false;
    knowledgeByPlayer[playerId] = level;
    return true;
}

BuildableProvince::BuildableProvince(ProvinceId provinceId, Vec2i position, PlayerId owner)
    : ProvinceBase(provinceId, position), ownerId(owner)
{
}

BuildableProvince::~BuildableProvince() = default;

void BuildableProvince::SetOwnerIdFromAuthority(PlayerId value)
{
    ownerId = value;
    if (simulation != nullptr)
        simulation->SetOwnerId(value);
}

ProvinceSimulation& BuildableProvince::CreateSimulation()
{
    if (simulation == nullptr)
        simulation = std::make_unique<ProvinceSimulation>(id, ownerId);
    simulation->GetEconomy().SetTraitIds(parameters.traitIds);
    return *simulation;
}

void BuildableProvince::SetParameters(BuildableProvinceParameters value)
{
    parameters = std::move(value);
    SetTraitIds(parameters.traitIds);
    if (simulation != nullptr)
        simulation->GetEconomy().SetTraitIds(parameters.traitIds);
}

bool BuildableProvince::InstallSimulation(std::unique_ptr<ProvinceSimulation> value)
{
    if (value == nullptr || value->GetProvinceId() != id)
        return false;
    simulation = std::move(value);
    return true;
}

void BuildableProvince::BindSimulationToTileMap(TileMap& tileMap)
{
    CreateSimulation().BindExternalTileMap(tileMap);
}

NeutralCityProvince::NeutralCityProvince(ProvinceId provinceId, Vec2i position,
                                         std::string definition)
    : ProvinceBase(provinceId, position), definitionId(std::move(definition))
{
}

void NeutralCityProvince::SetBarterPenaltyMultiplier(double value)
{
    if (std::isfinite(value) && value >= 1.0)
    {
        state.barterPenaltyMultiplier = value;
        ++state.revision;
    }
}

BanditProvince::BanditProvince(ProvinceId provinceId, Vec2i position,
                               std::string definition)
    : ProvinceBase(provinceId, position), definitionId(std::move(definition))
{
}

EventProvince::EventProvince(ProvinceId provinceId, Vec2i position,
                             std::string poolId)
    : ProvinceBase(provinceId, position), eventPoolId(std::move(poolId))
{
}

std::uint32_t EventProvince::GetDiscoveryAttempts(PlayerId playerId) const
{
    const auto it = discoveryAttempts.find(playerId);
    return it == discoveryAttempts.end() ? 0u : it->second;
}

bool EventProvince::HasResolvedFor(PlayerId playerId) const
{
    const auto it = resolvedByPlayer.find(playerId);
    return it != resolvedByPlayer.end() && it->second;
}

void EventProvince::RecordDiscoveryAttempt(PlayerId playerId)
{
    if (playerId != InvalidPlayerId)
        ++discoveryAttempts[playerId];
}

void EventProvince::MarkResolvedFor(PlayerId playerId)
{
    if (playerId != InvalidPlayerId)
        resolvedByPlayer[playerId] = true;
}

bool EventProvince::RestoreDiscoveryState(PlayerId playerId, std::uint32_t attempts, bool resolved)
{
    if (playerId == InvalidPlayerId)
        return false;
    discoveryAttempts[playerId] = attempts;
    resolvedByPlayer[playerId] = resolved;
    return true;
}

StaticProvince::StaticProvince(ProvinceId provinceId, ProvinceKind provinceKind, Vec2i position)
    : ProvinceBase(provinceId, position), kind(provinceKind)
{
    if (kind == ProvinceKind::Buildable)
        kind = ProvinceKind::NeutralSettlement;
}
