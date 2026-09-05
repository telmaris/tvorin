#include "world/GlobalMap.h"

#include "core/PersistenceLimits.h"
#include "world/ProvinceDefinition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>

namespace
{
    std::uint64_t IntegerSqrtFloor(std::uint64_t value)
    {
        std::uint64_t low = 0;
        std::uint64_t high = (std::uint64_t{1} << 32) + 1;
        while (high - low > 1)
        {
            const std::uint64_t middle = low + (high - low) / 2;
            if (middle != 0 && middle <= value / middle)
                low = middle;
            else
                high = middle;
        }
        return low;
    }

    bool TryComputeRouteLength(Vec2i first, Vec2i second, int& result)
    {
        const std::int64_t deltaX = static_cast<std::int64_t>(first.x) - second.x;
        const std::int64_t deltaY = static_cast<std::int64_t>(first.y) - second.y;
        const std::uint64_t dx = deltaX < 0 ? static_cast<std::uint64_t>(-deltaX)
                                            : static_cast<std::uint64_t>(deltaX);
        const std::uint64_t dy = deltaY < 0 ? static_cast<std::uint64_t>(-deltaY)
                                            : static_cast<std::uint64_t>(deltaY);
        if (dx != 0 && dx > std::numeric_limits<std::uint64_t>::max() / dx)
            return false;
        const std::uint64_t dx2 = dx * dx;
        if (dy != 0 && dy > std::numeric_limits<std::uint64_t>::max() / dy)
            return false;
        const std::uint64_t dy2 = dy * dy;
        if (dx2 > std::numeric_limits<std::uint64_t>::max() - dy2)
            return false;
        const std::uint64_t squared = dx2 + dy2;
        const std::uint64_t floorRoot = IntegerSqrtFloor(squared);
        const std::uint64_t threshold = floorRoot * floorRoot + floorRoot + 1;
        const std::uint64_t rounded = squared >= threshold ? floorRoot + 1 : floorRoot;
        if (rounded == 0 || rounded > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return false;
        result = static_cast<int>(rounded);
        return true;
    }

    bool HasKnownNeighbor(const GlobalMap& map, PlayerId playerId, ProvinceId id)
    {
        for (ProvinceId neighbor : map.GetNeighbors(id))
        {
            const auto* province = map.FindProvince(neighbor);
            if (province != nullptr &&
                province->GetKnowledge(playerId) >= ProvinceKnowledgeLevel::Scouted)
                return true;
        }
        return false;
    }

    long long DistanceSquared(Vec2i a, Vec2i b)
    {
        const long long dx = static_cast<long long>(a.x) - b.x;
        const long long dy = static_cast<long long>(a.y) - b.y;
        return dx * dx + dy * dy;
    }

    std::uint64_t Mix64(std::uint64_t value)
    {
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    class DeterministicRandomStream
    {
    public:
        explicit DeterministicRandomStream(std::uint64_t seed) : state(seed) {}

        std::uint64_t Next()
        {
            state += 0x9E3779B97F4A7C15ull;
            return Mix64(state);
        }

        std::uint64_t UniformBelow(std::uint64_t bound)
        {
            if (bound <= 1)
                return 0;

            // Rejection avoids modulo bias while keeping the stream entirely
            // integer based and stable on every supported platform.
            const std::uint64_t threshold = -bound % bound;
            std::uint64_t value = 0;
            do
            {
                value = Next();
            }
            while (value < threshold);
            return value % bound;
        }

        int UniformInt(int minimum, int maximum)
        {
            if (maximum <= minimum)
                return minimum;
            const auto span = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(maximum) - minimum) + 1ull;
            return minimum + static_cast<int>(UniformBelow(span));
        }

        double UnitInterval()
        {
            constexpr std::uint64_t SampleCount = 1'000'001ull;
            return static_cast<double>(UniformBelow(SampleCount)) / 1'000'000.0;
        }
    private:
        std::uint64_t state{0};
    };

    std::uint64_t DomainSeed(std::uint32_t globalSeed, std::uint64_t domain,
                             std::uint64_t id = 0)
    {
        return Mix64(static_cast<std::uint64_t>(globalSeed) ^
                     (domain * 0xD6E8FEB86659FD93ull) ^
                     (id * 0xA0761D6478BD642Full));
    }

    int SelectInt(const ProvinceIntRange& range, DeterministicRandomStream& stream)
    {
        return stream.UniformInt(range.min, range.max);
    }

    double SelectValue(const ProvinceValueRange& range,
                       DeterministicRandomStream& stream)
    {
        return range.min + (range.max - range.min) * stream.UnitInterval();
    }

    int ScaleInt(int value, double scale)
    {
        const double scaled = std::round(static_cast<double>(value) * scale);
        if (scaled <= 0.0)
            return 0;
        if (scaled >= static_cast<double>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        return static_cast<int>(scaled);
    }

    void InitializeCityState(NeutralCityProvince& province,
                             const ProvinceDefinition& definition,
                             DeterministicRandomStream& stream,
                             double wealthScale)
    {
        auto& state = province.GetStateForAuthority();
        state.wealthTier = province.GetWealthTier();
        state.barterPenaltyMultiplier = definition.barterPenaltyMultiplier;
        for (const auto& [resourceName, range] : definition.cityInitialStock)
        {
            ResourceType resource = ResourceType::Null;
            if (!TryParseResourceType(resourceName, resource))
                continue;
            state.SetStock(resource, ScaleInt(SelectInt(range, stream), wealthScale));
        }
        for (const auto& [resourceName, range] : definition.cityTradePrices)
        {
            ResourceType resource = ResourceType::Null;
            if (!TryParseResourceType(resourceName, resource))
                continue;
            state.SetSellPrice(resource, SelectValue(range, stream));
        }
        for (const auto& [resourceName, range] : definition.cityBuyPrices)
        {
            ResourceType resource = ResourceType::Null;
            if (!TryParseResourceType(resourceName, resource))
                continue;
            state.SetBuyPrice(resource, SelectValue(range, stream));
        }
        // Revision zero is reserved for an invalid/uninitialized state. The
        // selected values above form one deterministic city snapshot.
        state.revision = 1;
    }

    std::optional<BuildableProvinceParameters> SelectBuildableParameters(
        const ProvinceDefinition& definition, ProvinceId provinceId,
        std::uint32_t globalSeed, double wealthScale)
    {
        if (definition.archetype != ProvinceDefinitionArchetype::Buildable)
            return std::nullopt;

        DeterministicRandomStream stream(DomainSeed(globalSeed, 0xB17DAB1Eu, provinceId));
        BuildableProvinceParameters parameters;
        parameters.definitionId = definition.id;
        parameters.sizeX = SelectInt(definition.sizeX, stream);
        parameters.sizeY = SelectInt(definition.sizeY, stream);
        parameters.localSeed = static_cast<std::uint32_t>(stream.Next());
        parameters.resourceWealth = std::max(0.0, SelectValue(definition.resourceWealth, stream) * wealthScale);
        parameters.resourceDensity = SelectValue(definition.resourceDensity, stream);
        parameters.resourceFieldSize = SelectValue(definition.resourceFieldSize, stream);
        parameters.resourceRichness = std::max(0.0, SelectValue(definition.resourceRichness, stream) * wealthScale);
        parameters.waterAmount = SelectValue(definition.waterAmount, stream);
        parameters.mountainAmount = SelectValue(definition.mountainAmount, stream);
        parameters.ruggedness = SelectValue(definition.ruggedness, stream);
        parameters.wealthTier = ScaleInt(SelectInt({0, 3}, stream), wealthScale);
        for (const auto& trait : definition.traits)
            parameters.traitIds.push_back(trait.id);
        return parameters;
    }

    void AssignNaturalResourceProfile(BuildableProvinceParameters& parameters,
                                      ProvinceId provinceId, std::uint32_t globalSeed,
                                      bool homeProvince)
    {
        parameters.naturalResourceTypes = {
            ResourceType::WOOD, ResourceType::STONE,
            ResourceType::COAL, ResourceType::IRON_ORE};
        if (!homeProvince)
        {
            constexpr std::array<ResourceType, 3> rareTypes{
                ResourceType::COPPER_ORE, ResourceType::CLAY, ResourceType::SAND};
            for (const ResourceType resource : rareTypes)
            {
                const std::uint64_t domain =
                    (static_cast<std::uint64_t>(provinceId) << 8) ^
                    static_cast<std::uint64_t>(static_cast<int>(resource));
                DeterministicRandomStream stream(DomainSeed(globalSeed, 0xD3A0517u, domain));
                if (stream.UniformBelow(10000) < 500)
                    parameters.naturalResourceTypes.push_back(resource);
            }
        }
        std::sort(parameters.naturalResourceTypes.begin(),
                  parameters.naturalResourceTypes.end(),
                  [](ResourceType left, ResourceType right)
                  {
                      return static_cast<int>(left) < static_cast<int>(right);
                  });
        parameters.naturalResourceTypes.erase(
            std::unique(parameters.naturalResourceTypes.begin(),
                        parameters.naturalResourceTypes.end()),
            parameters.naturalResourceTypes.end());
    }

    int TypeWeight(ProvinceKind kind, const GlobalMapGenerationParameters& parameters)
    {
        switch (kind)
        {
            case ProvinceKind::Buildable: return parameters.buildableWeight;
            case ProvinceKind::NeutralSettlement: return parameters.neutralCityWeight;
            case ProvinceKind::BanditCamp: return parameters.banditCampWeight;
            case ProvinceKind::TreasureSite: return parameters.eventSiteWeight;
        }
        return 0;
    }
}

bool GlobalMap::AddProvince(std::unique_ptr<IProvince> province)
{
    if (province == nullptr || province->GetId() == InvalidProvinceId ||
        provinces.contains(province->GetId()))
        return false;

    const ProvinceId id = province->GetId();
    provinces.emplace(id, std::move(province));
    incidentConnectionIds.emplace(id, std::vector<ProvinceConnectionId>{});
    return true;
}

bool GlobalMap::AddConnection(ProvinceId from, ProvinceId to,
                              ProvinceConnectionId connectionId,
                              std::string definitionId)
{
    if (from == InvalidProvinceId || from == to || !provinces.contains(from) ||
        !provinces.contains(to) || IsAdjacent(from, to))
        return false;

    if (connectionId == InvalidProvinceConnectionId)
    {
        while (nextConnectionId == InvalidProvinceConnectionId ||
               connections.contains(nextConnectionId))
        {
            if (nextConnectionId == std::numeric_limits<ProvinceConnectionId>::max())
                return false;
            ++nextConnectionId;
        }
        connectionId = nextConnectionId++;
    }
    else
    {
        if (connections.contains(connectionId))
            return false;
        if (connectionId == std::numeric_limits<ProvinceConnectionId>::max())
            nextConnectionId = InvalidProvinceConnectionId;
        else
            nextConnectionId = std::max(nextConnectionId, connectionId + 1);
    }

    if (FindProvinceConnectionDefinition(definitionId) == nullptr)
        return false;
    int lengthUnits = 0;
    if (!TryComputeRouteLength(provinces.at(from)->GetLayoutPosition(),
                               provinces.at(to)->GetLayoutPosition(), lengthUnits))
        return false;
    auto connection = std::make_unique<LandRouteConnection>(
        connectionId, from, to, std::move(definitionId), lengthUnits);
    connections.emplace(connectionId, std::move(connection));
    incidentConnectionIds[from].push_back(connectionId);
    incidentConnectionIds[to].push_back(connectionId);
    std::sort(incidentConnectionIds[from].begin(), incidentConnectionIds[from].end());
    std::sort(incidentConnectionIds[to].begin(), incidentConnectionIds[to].end());
    return true;
}

bool GlobalMap::SetKnowledge(PlayerId playerId, ProvinceId provinceId,
                             ProvinceKnowledgeLevel level)
{
    auto* province = FindProvince(provinceId);
    if (province == nullptr || playerId == InvalidPlayerId)
        return false;
    return province->SetKnowledge(playerId, level);
}

bool GlobalMap::InitializeDiscovery(PlayerId playerId, ProvinceId homeProvinceId)
{
    auto* home = FindBuildableProvince(homeProvinceId);
    if (home == nullptr ||
        (home->GetOwnerId() != InvalidPlayerId && home->GetOwnerId() != playerId))
        return false;

    home->SetOwnerIdFromAuthority(playerId);
    home->SetKnowledge(playerId, ProvinceKnowledgeLevel::Owned);
    for (ProvinceId neighbor : GetNeighbors(homeProvinceId))
        SetKnowledge(playerId, neighbor, ProvinceKnowledgeLevel::ReachableUnknown);
    return true;
}

bool GlobalMap::SetBuildableOwner(PlayerId playerId, ProvinceId provinceId)
{
    if (playerId == InvalidPlayerId)
        return false;
    auto* province = FindBuildableProvince(provinceId);
    if (province == nullptr ||
        (province->GetOwnerId() != InvalidPlayerId && province->GetOwnerId() != playerId))
        return false;
    province->SetOwnerIdFromAuthority(playerId);
    return true;
}

IProvince* GlobalMap::FindProvince(ProvinceId id)
{
    const auto it = provinces.find(id);
    return it == provinces.end() ? nullptr : it->second.get();
}

const IProvince* GlobalMap::FindProvince(ProvinceId id) const
{
    const auto it = provinces.find(id);
    return it == provinces.end() ? nullptr : it->second.get();
}

BuildableProvince* GlobalMap::FindBuildableProvince(ProvinceId id)
{
    return dynamic_cast<BuildableProvince*>(FindProvince(id));
}

const BuildableProvince* GlobalMap::FindBuildableProvince(ProvinceId id) const
{
    return dynamic_cast<const BuildableProvince*>(FindProvince(id));
}

IProvinceConnection* GlobalMap::FindConnection(ProvinceConnectionId id)
{
    const auto it = connections.find(id);
    return it == connections.end() ? nullptr : it->second.get();
}

const IProvinceConnection* GlobalMap::FindConnection(ProvinceConnectionId id) const
{
    const auto it = connections.find(id);
    return it == connections.end() ? nullptr : it->second.get();
}

IProvinceConnection* GlobalMap::FindConnection(ProvinceId first, ProvinceId second)
{
    const auto ids = GetIncidentConnectionIds(first);
    for (ProvinceConnectionId id : ids)
    {
        auto* connection = FindConnection(id);
        if (connection != nullptr &&
            connection->GetFirstProvinceId() == std::min(first, second) &&
            connection->GetSecondProvinceId() == std::max(first, second))
            return connection;
    }
    return nullptr;
}

const IProvinceConnection* GlobalMap::FindConnection(ProvinceId first, ProvinceId second) const
{
    const auto ids = GetIncidentConnectionIds(first);
    for (ProvinceConnectionId id : ids)
    {
        const auto* connection = FindConnection(id);
        if (connection != nullptr &&
            connection->GetFirstProvinceId() == std::min(first, second) &&
            connection->GetSecondProvinceId() == std::max(first, second))
            return connection;
    }
    return nullptr;
}

std::vector<ProvinceConnectionId> GlobalMap::GetIncidentConnectionIds(ProvinceId id) const
{
    const auto it = incidentConnectionIds.find(id);
    return it == incidentConnectionIds.end() ? std::vector<ProvinceConnectionId>{} : it->second;
}

bool GlobalMap::TransformProvince(std::unique_ptr<IProvince> replacement)
{
    if (replacement == nullptr || replacement->GetId() == InvalidProvinceId)
        return false;
    const ProvinceId id = replacement->GetId();
    auto it = provinces.find(id);
    if (it == provinces.end() || replacement->GetLayoutPosition() != it->second->GetLayoutPosition())
        return false;

    auto* oldBase = dynamic_cast<ProvinceBase*>(it->second.get());
    auto* newBase = dynamic_cast<ProvinceBase*>(replacement.get());
    if (oldBase == nullptr || newBase == nullptr)
        return false;
    newBase->CopyKnowledgeFrom(*oldBase);
    it->second = std::move(replacement);
    return true;
}

std::vector<ProvinceId> GlobalMap::GetNeighbors(ProvinceId id) const
{
    std::vector<ProvinceId> result;
    for (ProvinceConnectionId connectionId : GetIncidentConnectionIds(id))
    {
        const auto* connection = FindConnection(connectionId);
        if (connection == nullptr)
            continue;
        const ProvinceId neighbor = connection->GetFirstProvinceId() == id
            ? connection->GetSecondProvinceId() : connection->GetFirstProvinceId();
        if (neighbor != InvalidProvinceId)
            result.push_back(neighbor);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

bool GlobalMap::FindShortestPath(ProvinceId source, ProvinceId target,
                                 std::vector<ProvinceConnectionId>& path) const
{
    path.clear();
    if (source == InvalidProvinceId || target == InvalidProvinceId || source == target ||
        FindProvince(source) == nullptr || FindProvince(target) == nullptr)
        return false;

    struct PreviousStep
    {
        ProvinceId province{InvalidProvinceId};
        ProvinceConnectionId connection{InvalidProvinceConnectionId};
    };

    std::map<ProvinceId, PreviousStep> previous;
    std::queue<ProvinceId> pending;
    previous.emplace(source, PreviousStep{});
    pending.push(source);
    while (!pending.empty())
    {
        const ProvinceId current = pending.front();
        pending.pop();
        if (current == target)
            break;

        auto incident = GetIncidentConnectionIds(current);
        std::sort(incident.begin(), incident.end());
        for (const ProvinceConnectionId connectionId : incident)
        {
            const auto* connection = FindConnection(connectionId);
            if (connection == nullptr)
                continue;
            const ProvinceId next = connection->GetFirstProvinceId() == current
                ? connection->GetSecondProvinceId() : connection->GetFirstProvinceId();
            if (next == InvalidProvinceId || previous.contains(next))
                continue;
            previous.emplace(next, PreviousStep{current, connectionId});
            pending.push(next);
        }
    }
    if (!previous.contains(target))
        return false;

    for (ProvinceId current = target; current != source;)
    {
        const auto it = previous.find(current);
        if (it == previous.end() ||
            it->second.connection == InvalidProvinceConnectionId)
        {
            path.clear();
            return false;
        }
        path.push_back(it->second.connection);
        current = it->second.province;
    }
    std::reverse(path.begin(), path.end());
    return !path.empty();
}

bool GlobalMap::HasPath(ProvinceId source, ProvinceId target) const
{
    std::vector<ProvinceConnectionId> path;
    return FindShortestPath(source, target, path);
}

std::vector<ProvinceId> GlobalMap::GetProvinceIds() const
{
    std::vector<ProvinceId> ids;
    ids.reserve(provinces.size());
    for (const auto& [id, province] : provinces)
        ids.push_back(id);
    return ids;
}

std::vector<ProvinceConnectionId> GlobalMap::GetConnectionIds() const
{
    std::vector<ProvinceConnectionId> ids;
    ids.reserve(connections.size());
    for (const auto& [id, connection] : connections)
    {
        (void)connection;
        ids.push_back(id);
    }
    return ids;
}

std::size_t GlobalMap::GetEdgeCount() const
{
    return connections.size();
}

bool GlobalMap::IsAdjacent(ProvinceId from, ProvinceId to) const
{
    return FindConnection(from, to) != nullptr;
}

bool GlobalMap::IsConnected() const
{
    if (provinces.empty())
        return true;

    std::set<ProvinceId> visited;
    std::vector<ProvinceId> pending{provinces.begin()->first};
    while (!pending.empty())
    {
        const ProvinceId id = pending.back();
        pending.pop_back();
        if (!visited.insert(id).second)
            continue;
        for (ProvinceId neighbor : GetNeighbors(id))
            if (!visited.contains(neighbor))
                pending.push_back(neighbor);
    }
    return visited.size() == provinces.size();
}

void GlobalMap::UpdateConnections()
{
    for (auto& [connectionId, connection] : connections)
    {
        (void)connectionId;
        auto* landRoute = dynamic_cast<LandRouteConnection*>(connection.get());
        if (landRoute != nullptr)
            landRoute->Update();
    }
}

GlobalMapView GlobalMap::BuildViewFor(PlayerId playerId) const
{
    GlobalMapView view;
    bool hasOwnedBuildable = false;
    for (const auto& [provinceId, province] : provinces)
    {
        const auto* buildable = dynamic_cast<const BuildableProvince*>(province.get());
        if (buildable != nullptr && buildable->GetOwnerId() == playerId)
        {
            hasOwnedBuildable = true;
            break;
        }
    }
    for (const auto& [id, province] : provinces)
    {
        const ProvinceKnowledgeLevel knowledge = province->GetKnowledge(playerId);
        const ProvinceKnowledgeLevel viewKnowledge = !fogOfWarEnabled &&
                knowledge < ProvinceKnowledgeLevel::Scouted
            ? ProvinceKnowledgeLevel::Scouted : knowledge;

        GlobalMapNodeView node;
        node.id = id;
        node.knowledge = viewKnowledge;
        node.layoutPosition = province->GetLayoutPosition();
        if (viewKnowledge >= ProvinceKnowledgeLevel::Scouted)
        {
            node.visibleKind = province->GetKind();
            const ProvinceDefinition* definition = nullptr;
            if (const auto* buildable = dynamic_cast<const BuildableProvince*>(province.get()))
            {
                definition = FindProvinceDefinition(buildable->GetParameters().definitionId);
                node.traitIds = buildable->GetParameters().traitIds;
                node.naturalResourceTypes = buildable->GetParameters().naturalResourceTypes;
            }
            else if (const auto* city = dynamic_cast<const NeutralCityProvince*>(province.get()))
                definition = FindProvinceDefinition(city->GetDefinitionId());
            else if (const auto* bandit = dynamic_cast<const BanditProvince*>(province.get()))
                definition = FindProvinceDefinition(bandit->GetDefinitionId());
            else if (const auto* event = dynamic_cast<const EventProvince*>(province.get()))
                definition = FindProvinceDefinition(event->GetEventPoolId());
            if (definition != nullptr)
                node.displayName = definition->displayName;
            if (const auto* buildable = FindBuildableProvince(id);
                buildable != nullptr && buildable->GetOwnerId() != InvalidPlayerId)
                node.visibleOwner = buildable->GetOwnerId();
        }
        node.canScout = fogOfWarEnabled &&
                        knowledge == ProvinceKnowledgeLevel::ReachableUnknown &&
                        HasKnownNeighbor(*this, playerId, id);
        if (viewKnowledge >= ProvinceKnowledgeLevel::Scouted && hasOwnedBuildable)
        {
            const auto* buildable = FindBuildableProvince(id);
            const bool unownedBuildable = buildable != nullptr &&
                                           buildable->GetOwnerId() == InvalidPlayerId;
            node.canTrade = province->GetKind() == ProvinceKind::NeutralSettlement;
            node.canAttack = province->GetKind() == ProvinceKind::NeutralSettlement ||
                             province->GetKind() == ProvinceKind::BanditCamp ||
                             (buildable != nullptr && !unownedBuildable &&
                              buildable->GetOwnerId() != playerId);
            node.canColonize = unownedBuildable;
        }
        view.nodes.push_back(node);
    }

    for (const auto& [connectionId, connection] : connections)
    {
        if (connection == nullptr)
            continue;
        const ProvinceId from = connection->GetFirstProvinceId();
        const ProvinceId to = connection->GetSecondProvinceId();
        const auto* fromProvince = FindProvince(from);
        const auto* toProvince = FindProvince(to);
        if (fromProvince == nullptr || toProvince == nullptr)
            continue;
        const auto fromKnowledge = fromProvince->GetKnowledge(playerId);
        const auto toKnowledge = toProvince->GetKnowledge(playerId);
        const auto fromViewKnowledge = !fogOfWarEnabled &&
                fromKnowledge < ProvinceKnowledgeLevel::Scouted
            ? ProvinceKnowledgeLevel::Scouted : fromKnowledge;
        const auto toViewKnowledge = !fogOfWarEnabled &&
                toKnowledge < ProvinceKnowledgeLevel::Scouted
            ? ProvinceKnowledgeLevel::Scouted : toKnowledge;
        const bool bothScouted = fromViewKnowledge >= ProvinceKnowledgeLevel::Scouted &&
                                 toViewKnowledge >= ProvinceKnowledgeLevel::Scouted;
        bool canUpgrade = false;
        if (bothScouted)
        {
            const auto* fromBuildable = FindBuildableProvince(from);
            const auto* toBuildable = FindBuildableProvince(to);
            const bool ownsEndpoint =
                (fromBuildable != nullptr && fromBuildable->GetOwnerId() == playerId) ||
                (toBuildable != nullptr && toBuildable->GetOwnerId() == playerId);
            const auto* landRoute = dynamic_cast<const LandRouteConnection*>(connection.get());
            canUpgrade = ownsEndpoint && landRoute != nullptr &&
                         landRoute->GetNextLevelDefinition() != nullptr;
        }
        ProvinceEdgeView edge;
        edge.from = from;
        edge.to = to;
        edge.connectionId = connectionId;
        edge.canInspect = bothScouted;
        edge.canUpgrade = canUpgrade;
        if (bothScouted)
        {
            edge.lengthUnits = connection->GetLengthUnits();
            edge.level = connection->GetLevel();
            const auto* landRoute = dynamic_cast<const LandRouteConnection*>(connection.get());
            const auto* current = landRoute != nullptr
                ? landRoute->GetLevelDefinition(edge.level) : nullptr;
            const auto* next = landRoute != nullptr ? landRoute->GetNextLevelDefinition() : nullptr;
            if (current != nullptr)
            {
                edge.routeTimeBasisPoints = static_cast<int>(
                    std::llround(current->traversalTimeMultiplier * 10000.0));
                edge.incidentReductionBasisPoints = current->incidentChanceReductionBasisPoints;
            }
            if (next != nullptr)
            {
                edge.nextLevel = next->level;
                edge.nextRouteTimeBasisPoints = static_cast<int>(
                    std::llround(next->traversalTimeMultiplier * 10000.0));
                edge.nextIncidentReductionBasisPoints = next->incidentChanceReductionBasisPoints;
                edge.nextUpgradeDurationTicks = next->upgradeDurationTicks;
                edge.nextUpgradeCost = next->upgradeCost;
            }
            if (landRoute != nullptr)
                edge.upgradeRemainingTicks = landRoute->GetUpgradeRemainingTicks();
        }
        view.edges.push_back(edge);
    }
    view.fogOfWarEnabled = fogOfWarEnabled;
    return view;
}

GlobalMapGenerationResult GlobalMapGenerator::Generate(
    const GlobalMapGenerationParameters& parameters, int humanPlayerCount)
{
    std::vector<PlayerId> humanPlayers;
    for (int id = 0; id < humanPlayerCount; ++id)
        humanPlayers.push_back(static_cast<PlayerId>(id));
    return Generate(parameters, humanPlayers);
}

GlobalMapGenerationResult GlobalMapGenerator::Generate(
    const GlobalMapGenerationParameters& parameters,
    const std::vector<PlayerId>& humanPlayers)
{
    GlobalMapGenerationResult result;
    const auto invalid = [&](std::string reason)
    {
        result.failureReason = std::move(reason);
        result.map = GlobalMap{};
        result.homeProvinceByPlayer.clear();
        result.success = false;
        return std::move(result);
    };

    if (parameters.provinceCount <= 0 ||
        !PersistenceLimits::IsCountInRange(parameters.provinceCount,
                                           PersistenceLimits::MaxGlobalProvinces) ||
        parameters.extraEdgeCount < 0 ||
        !PersistenceLimits::IsCountInRange(static_cast<std::size_t>(parameters.extraEdgeCount),
                                           PersistenceLimits::MaxGlobalEdges) ||
        humanPlayers.empty() || static_cast<int>(humanPlayers.size()) > parameters.provinceCount)
        return invalid("invalid global-map province or player count");

    if (parameters.layoutRadius <= 0 || parameters.layoutRadius > 1'000'000 ||
        parameters.minimumLayoutSpacing <= 0 ||
        parameters.minimumLayoutSpacing > parameters.layoutRadius * 2 ||
        parameters.maximumPlacementAttemptsPerProvince <= 0 ||
        parameters.maximumPlacementAttemptsPerProvince > 100'000 ||
        parameters.minimumNeutralBuildables < 0 ||
        parameters.minimumNeutralBuildables >
            parameters.provinceCount - static_cast<int>(humanPlayers.size()) ||
        parameters.startBoundaryClearance < 0 ||
        parameters.startBoundaryClearance > parameters.layoutRadius * 2 ||
        parameters.buildableWeight < 0 || parameters.neutralCityWeight < 0 ||
        parameters.banditCampWeight < 0 || parameters.eventSiteWeight < 0 ||
        !std::isfinite(parameters.buildableWealthScale) ||
        !std::isfinite(parameters.cityWealthScale) ||
        !std::isfinite(parameters.banditStrengthScale) ||
        parameters.buildableWealthScale < 0.0 || parameters.cityWealthScale < 0.0 ||
        parameters.banditStrengthScale < 0.0)
        return invalid("invalid bounded global-map layout, weights or value scale");

    const auto nonStartCount = parameters.provinceCount -
                               static_cast<int>(humanPlayers.size());
    const auto weightedTypeTotal = static_cast<std::int64_t>(parameters.buildableWeight) +
                                   parameters.neutralCityWeight + parameters.banditCampWeight +
                                   parameters.eventSiteWeight;
    if (nonStartCount > parameters.minimumNeutralBuildables && weightedTypeTotal <= 0)
        return invalid("at least one province type must have a positive weight");

    std::set<PlayerId> uniquePlayers(humanPlayers.begin(), humanPlayers.end());
    if (uniquePlayers.size() != humanPlayers.size() || uniquePlayers.contains(InvalidPlayerId))
        return invalid("human player IDs must be unique and valid");

    GlobalMap generatedMap;
    generatedMap.SetGenerationSeed(parameters.seed);
    generatedMap.SetFogOfWarEnabled(parameters.fogOfWarEnabled);

    // Layout has its own stream. Changing province values or type weights can
    // therefore never move an existing node.
    DeterministicRandomStream layoutStream(DomainSeed(parameters.seed, 0x1A90'0001u));
    std::vector<Vec2i> positions;
    positions.reserve(static_cast<std::size_t>(parameters.provinceCount));
    const long long radius = parameters.layoutRadius;
    const long long radiusSquared = radius * radius;
    const long long spacingSquared =
        static_cast<long long>(parameters.minimumLayoutSpacing) * parameters.minimumLayoutSpacing;
    for (int index = 0; index < parameters.provinceCount; ++index)
    {
        bool placed = false;
        for (int attempt = 0; attempt < parameters.maximumPlacementAttemptsPerProvince; ++attempt)
        {
            const Vec2i candidate{
                layoutStream.UniformInt(-parameters.layoutRadius, parameters.layoutRadius),
                layoutStream.UniformInt(-parameters.layoutRadius, parameters.layoutRadius)};
            if (DistanceSquared(candidate, Vec2i{}) > radiusSquared)
                continue;

            bool sufficientlySeparated = true;
            for (const Vec2i existing : positions)
            {
                const long long distance = DistanceSquared(candidate, existing);
                if (distance == 0 || distance < spacingSquared)
                {
                    sufficientlySeparated = false;
                    break;
                }
            }
            if (!sufficientlySeparated)
                continue;

            positions.push_back(candidate);
            placed = true;
            break;
        }
        if (!placed || positions.size() != static_cast<std::size_t>(index + 1))
            return invalid("bounded global-map placement could not satisfy spacing");
    }

    // Keep starts away from the disk boundary whenever the requested interior
    // contains enough candidates. The fallback is intentional for very small
    // or deliberately constrained debug maps.
    std::vector<int> startCandidates;
    const long long interiorRadius =
        std::max(0, parameters.layoutRadius - parameters.startBoundaryClearance);
    const long long interiorRadiusSquared = interiorRadius * interiorRadius;
    for (int index = 0; index < parameters.provinceCount; ++index)
        if (DistanceSquared(positions[static_cast<std::size_t>(index)], Vec2i{}) <=
            interiorRadiusSquared)
            startCandidates.push_back(index);
    if (startCandidates.size() < humanPlayers.size())
    {
        startCandidates.clear();
        for (int index = 0; index < parameters.provinceCount; ++index)
            startCandidates.push_back(index);
    }

    const auto isBetterById = [](int left, int right) { return left < right; };
    std::vector<int> startIndices;
    if (humanPlayers.size() == 1)
    {
        long long centroidX = 0;
        long long centroidY = 0;
        for (const Vec2i position : positions)
        {
            centroidX += position.x;
            centroidY += position.y;
        }
        const Vec2i centroid{
            static_cast<int>(centroidX / parameters.provinceCount),
            static_cast<int>(centroidY / parameters.provinceCount)};
        int bestIndex = startCandidates.front();
        for (const int candidate : startCandidates)
        {
            const long long candidateDistance = DistanceSquared(positions[candidate], centroid);
            const long long bestDistance = DistanceSquared(positions[bestIndex], centroid);
            if (candidateDistance < bestDistance ||
                (candidateDistance == bestDistance && isBetterById(candidate, bestIndex)))
                bestIndex = candidate;
        }
        startIndices.push_back(bestIndex);
    }
    else
    {
        int first = startCandidates.front();
        for (const int candidate : startCandidates)
        {
            const long long candidateClearance = radiusSquared -
                DistanceSquared(positions[candidate], Vec2i{});
            const long long firstClearance = radiusSquared -
                DistanceSquared(positions[first], Vec2i{});
            if (candidateClearance > firstClearance ||
                (candidateClearance == firstClearance &&
                 DistanceSquared(positions[candidate], Vec2i{}) <
                     DistanceSquared(positions[first], Vec2i{})) ||
                (candidateClearance == firstClearance &&
                 DistanceSquared(positions[candidate], Vec2i{}) ==
                     DistanceSquared(positions[first], Vec2i{}) &&
                 isBetterById(candidate, first)))
                first = candidate;
        }
        startIndices.push_back(first);
        while (startIndices.size() < humanPlayers.size())
        {
            int bestIndex = -1;
            long long bestNearestDistance = -1;
            long long bestClearance = std::numeric_limits<long long>::min();
            for (const int candidate : startCandidates)
            {
                if (std::find(startIndices.begin(), startIndices.end(), candidate) !=
                    startIndices.end())
                    continue;
                long long nearest = std::numeric_limits<long long>::max();
                for (const int selected : startIndices)
                    nearest = std::min(nearest,
                                       DistanceSquared(positions[candidate], positions[selected]));
                const long long clearance = radiusSquared -
                    DistanceSquared(positions[candidate], Vec2i{});
                if (nearest > bestNearestDistance ||
                    (nearest == bestNearestDistance && clearance > bestClearance) ||
                    (nearest == bestNearestDistance && clearance == bestClearance &&
                     isBetterById(candidate, bestIndex < 0 ? candidate : bestIndex)))
                {
                    bestNearestDistance = nearest;
                    bestClearance = clearance;
                    bestIndex = candidate;
                }
            }
            if (bestIndex < 0)
                return invalid("unable to choose distinct starting provinces");
            startIndices.push_back(bestIndex);
        }
    }

    std::vector<ProvinceKind> kinds(static_cast<std::size_t>(parameters.provinceCount),
                                    ProvinceKind::Buildable);
    std::set<int> startIndexSet(startIndices.begin(), startIndices.end());
    std::vector<int> nonStartIndices;
    for (int index = 0; index < parameters.provinceCount; ++index)
        if (!startIndexSet.contains(index))
            nonStartIndices.push_back(index);

    std::size_t cursor = 0;
    cursor += static_cast<std::size_t>(parameters.minimumNeutralBuildables);
    if (nonStartIndices.size() - static_cast<std::size_t>(parameters.minimumNeutralBuildables) >= 3)
    {
        kinds[static_cast<std::size_t>(nonStartIndices[cursor++])] = ProvinceKind::NeutralSettlement;
        kinds[static_cast<std::size_t>(nonStartIndices[cursor++])] = ProvinceKind::BanditCamp;
        kinds[static_cast<std::size_t>(nonStartIndices[cursor++])] = ProvinceKind::TreasureSite;
    }

    DeterministicRandomStream typeStream(DomainSeed(parameters.seed, 0x7A1E'0002u));
    for (; cursor < nonStartIndices.size(); ++cursor)
    {
        const auto pick = typeStream.UniformBelow(static_cast<std::uint64_t>(weightedTypeTotal));
        std::uint64_t cumulative = 0;
        ProvinceKind kind = ProvinceKind::Buildable;
        for (const ProvinceKind candidate : {ProvinceKind::Buildable,
                                             ProvinceKind::NeutralSettlement,
                                             ProvinceKind::BanditCamp,
                                             ProvinceKind::TreasureSite})
        {
            cumulative += static_cast<std::uint64_t>(TypeWeight(candidate, parameters));
            if (pick < cumulative)
            {
                kind = candidate;
                break;
            }
        }
        kinds[static_cast<std::size_t>(nonStartIndices[cursor])] = kind;
    }

    const ProvinceDefinition* buildableDefinition = FindProvinceDefinition("frontier_buildable");
    const ProvinceDefinition* cityDefinition = FindProvinceDefinition("neutral_city");
    const ProvinceDefinition* banditDefinition = FindProvinceDefinition("bandit_camp");
    const ProvinceDefinition* eventDefinition = FindProvinceDefinition("ancient_event_site");
    if (buildableDefinition == nullptr || cityDefinition == nullptr ||
        banditDefinition == nullptr || eventDefinition == nullptr)
        return invalid("required province definition is missing from the catalog");

    std::vector<int> startByIndex(static_cast<std::size_t>(parameters.provinceCount), -1);
    for (std::size_t playerIndex = 0; playerIndex < startIndices.size(); ++playerIndex)
        startByIndex[static_cast<std::size_t>(startIndices[playerIndex])] =
            static_cast<int>(playerIndex);

    for (int index = 0; index < parameters.provinceCount; ++index)
    {
        const ProvinceId id = static_cast<ProvinceId>(index + 1);
        const Vec2i position = positions[static_cast<std::size_t>(index)];
        const int startPlayerIndex = startByIndex[static_cast<std::size_t>(index)];
        if (startPlayerIndex >= 0)
        {
            const PlayerId playerId = humanPlayers[static_cast<std::size_t>(startPlayerIndex)];
            auto parametersForProvince = SelectBuildableParameters(
                *buildableDefinition, id, parameters.seed, parameters.buildableWealthScale);
            if (!parametersForProvince.has_value())
                return invalid("unable to select starting province parameters");
            AssignNaturalResourceProfile(*parametersForProvince, id, parameters.seed, true);
            auto province = std::make_unique<BuildableProvince>(id, position, playerId);
            province->SetParameters(std::move(*parametersForProvince));
            result.homeProvinceByPlayer[playerId] = id;
            if (!generatedMap.AddProvince(std::move(province)))
                return invalid("unable to add starting province");
        }
        else if (kinds[static_cast<std::size_t>(index)] == ProvinceKind::Buildable)
        {
            auto parametersForProvince = SelectBuildableParameters(
                *buildableDefinition, id, parameters.seed, parameters.buildableWealthScale);
            if (!parametersForProvince.has_value())
                return invalid("unable to select buildable province parameters");
            AssignNaturalResourceProfile(*parametersForProvince, id, parameters.seed, false);
            auto province = std::make_unique<BuildableProvince>(id, position);
            province->SetParameters(std::move(*parametersForProvince));
            if (!generatedMap.AddProvince(std::move(province)))
                return invalid("unable to add buildable province");
        }
        else if (kinds[static_cast<std::size_t>(index)] == ProvinceKind::NeutralSettlement)
        {
            DeterministicRandomStream stream(DomainSeed(parameters.seed, 0xC17A'0003u, id));
            auto province = std::make_unique<NeutralCityProvince>(id, position,
                                                                   cityDefinition->id);
            province->SetWealthTier(ScaleInt(SelectInt(cityDefinition->cityWealth, stream),
                                              parameters.cityWealthScale));
            InitializeCityState(*province, *cityDefinition, stream, parameters.cityWealthScale);
            if (!generatedMap.AddProvince(std::move(province)))
                return invalid("unable to add neutral city province");
        }
        else if (kinds[static_cast<std::size_t>(index)] == ProvinceKind::BanditCamp)
        {
            DeterministicRandomStream stream(DomainSeed(parameters.seed, 0xB4AD'0004u, id));
            auto province = std::make_unique<BanditProvince>(id, position, banditDefinition->id);
            province->SetStrength(ScaleInt(SelectInt(banditDefinition->banditStrength, stream),
                                           parameters.banditStrengthScale));
            province->SetLootTableId(banditDefinition->banditLootTableId);
            province->SetRaidPressure(banditDefinition->banditRaidWeight);
            if (auto futureParameters = SelectBuildableParameters(
                    *buildableDefinition, id, parameters.seed, parameters.buildableWealthScale);
                futureParameters.has_value())
            {
                AssignNaturalResourceProfile(*futureParameters, id, parameters.seed, false);
                province->SetFutureBuildableParameters(std::move(*futureParameters));
            }
            if (!generatedMap.AddProvince(std::move(province)))
                return invalid("unable to add bandit province");
        }
        else if (kinds[static_cast<std::size_t>(index)] == ProvinceKind::TreasureSite)
        {
            auto province = std::make_unique<EventProvince>(id, position,
                                                             eventDefinition->eventPoolId);
            if (!generatedMap.AddProvince(std::move(province)))
                return invalid("unable to add event province");
        }
    }

    // Prim's nearest-neighbour tree is deterministic and guarantees a single
    // connected component. Equal distances are resolved by province IDs.
    std::vector<bool> selected(static_cast<std::size_t>(parameters.provinceCount), false);
    selected[0] = true;
    for (int added = 1; added < parameters.provinceCount; ++added)
    {
        int bestFrom = -1;
        int bestTo = -1;
        long long bestDistance = std::numeric_limits<long long>::max();
        for (int to = 0; to < parameters.provinceCount; ++to)
        {
            if (selected[static_cast<std::size_t>(to)])
                continue;
            for (int from = 0; from < parameters.provinceCount; ++from)
            {
                if (!selected[static_cast<std::size_t>(from)])
                    continue;
                const long long distance = DistanceSquared(positions[from], positions[to]);
                if (distance < bestDistance ||
                    (distance == bestDistance &&
                     std::tie(from, to) < std::tie(bestFrom, bestTo)))
                {
                    bestDistance = distance;
                    bestFrom = from;
                    bestTo = to;
                }
            }
        }
        if (bestFrom < 0 || !generatedMap.AddConnection(
                static_cast<ProvinceId>(bestFrom + 1),
                static_cast<ProvinceId>(bestTo + 1)))
            return invalid("unable to build deterministic spanning tree");
        selected[static_cast<std::size_t>(bestTo)] = true;
    }

    std::vector<std::tuple<long long, ProvinceId, ProvinceId>> candidates;
    for (int from = 0; from < parameters.provinceCount; ++from)
        for (int to = from + 1; to < parameters.provinceCount; ++to)
            if (!generatedMap.IsAdjacent(static_cast<ProvinceId>(from + 1),
                                         static_cast<ProvinceId>(to + 1)))
                candidates.emplace_back(DistanceSquared(positions[from], positions[to]),
                                        static_cast<ProvinceId>(from + 1),
                                        static_cast<ProvinceId>(to + 1));
    std::sort(candidates.begin(), candidates.end());
    const int requestedExtras = std::max(0, parameters.extraEdgeCount);
    int addedExtras = 0;
    const auto addCandidate = [&](const auto& candidate)
    {
        if (addedExtras >= requestedExtras)
            return false;
        if (!generatedMap.AddConnection(std::get<1>(candidate), std::get<2>(candidate)))
            return false;
        ++addedExtras;
        return true;
    };

    // Spend the first extra edges on start spokes when possible. This keeps
    // every normal start connected to several expansion directions without
    // changing the spanning-tree rule for the rest of the graph.
    for (const int startIndex : startIndices)
    {
        while (generatedMap.GetNeighbors(static_cast<ProvinceId>(startIndex + 1)).size() < 2 &&
               addedExtras < requestedExtras)
        {
            const auto candidate = std::find_if(candidates.begin(), candidates.end(),
                [&](const auto& value)
                {
                    const ProvinceId from = std::get<1>(value);
                    const ProvinceId to = std::get<2>(value);
                    return (from == static_cast<ProvinceId>(startIndex + 1) ||
                            to == static_cast<ProvinceId>(startIndex + 1)) &&
                           !generatedMap.IsAdjacent(from, to);
                });
            if (candidate == candidates.end())
                break;
            if (!addCandidate(*candidate))
                return invalid("unable to add required start expansion edge");
        }
    }
    for (const auto& candidate : candidates)
    {
        if (addedExtras >= requestedExtras)
            break;
        if (!generatedMap.IsAdjacent(std::get<1>(candidate), std::get<2>(candidate)))
            if (!addCandidate(candidate))
                return invalid("unable to add bounded global-map loop");
    }

    for (const auto& [playerId, homeId] : result.homeProvinceByPlayer)
        if (!generatedMap.InitializeDiscovery(playerId, homeId))
            return invalid("unable to initialize starting province discovery");

    result.success = generatedMap.IsConnected() &&
                     generatedMap.GetEdgeCount() <= PersistenceLimits::MaxGlobalEdges;
    if (!result.success)
        return invalid("generated global graph exceeded its bounded edge limit");
    result.map = std::move(generatedMap);
    return result;
}

std::uint32_t GlobalMapGenerator::DeriveProvinceSeed(std::uint32_t globalSeed,
                                                     ProvinceId provinceId)
{
    // SplitMix-style integer mixing keeps every local map independent from
    // the global RNG consumption order while remaining stable across runs.
    std::uint64_t value = static_cast<std::uint64_t>(globalSeed) +
                          static_cast<std::uint64_t>(provinceId) * 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    value ^= value >> 31;
    return static_cast<std::uint32_t>(value ^ (value >> 32));
}
