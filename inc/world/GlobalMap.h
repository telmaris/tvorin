#ifndef WORLD_GLOBAL_MAP_H
#define WORLD_GLOBAL_MAP_H

#include "world/Province.h"
#include "world/ProvinceConnection.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct ProvinceEdgeView
{
    ProvinceId from{InvalidProvinceId};
    ProvinceId to{InvalidProvinceId};
    ProvinceConnectionId connectionId{InvalidProvinceConnectionId};
    int lengthUnits{0};
    int level{0};
    int routeTimeBasisPoints{10000};
    int incidentReductionBasisPoints{0};
    int incidentRiskBasisPoints{0};
    int nextLevel{0};
    int nextRouteTimeBasisPoints{10000};
    int nextIncidentReductionBasisPoints{0};
    std::uint64_t nextUpgradeDurationTicks{0};
    std::vector<ProvinceConnectionResourceCost> nextUpgradeCost;
    std::uint64_t upgradeRemainingTicks{0};
    bool canInspect{false};
    bool canUpgrade{false};
};

struct GlobalMapNodeView
{
    ProvinceId id{InvalidProvinceId};
    ProvinceKnowledgeLevel knowledge{ProvinceKnowledgeLevel::Hidden};
    std::optional<ProvinceKind> visibleKind;
    std::optional<PlayerId> visibleOwner;
    Vec2i layoutPosition{};
    std::string displayName;
    std::vector<std::string> traitIds;
    std::vector<ResourceType> naturalResourceTypes;
    bool canScout{false};
    bool canTrade{false};
    bool canAttack{false};
    bool canColonize{false};
};

struct GlobalMapView
{
    // Presentation setting shared by the campaign. The view always contains
    // every node so the global canvas can keep its stable layout. When the
    // setting is disabled, the view is also fully revealed for a no-fog game.
    bool fogOfWarEnabled{true};
    std::vector<GlobalMapNodeView> nodes;
    std::vector<ProvinceEdgeView> edges;
};

struct GlobalMapGenerationParameters
{
    std::uint32_t seed{0xC0FFEEu};
    int provinceCount{32};
    int extraEdgeCount{12};
    int layoutRadius{1000};
    int minimumLayoutSpacing{120};
    int maximumPlacementAttemptsPerProvince{2000};
    int minimumNeutralBuildables{2};
    int buildableWeight{55};
    int neutralCityWeight{20};
    int banditCampWeight{15};
    int eventSiteWeight{10};
    int startBoundaryClearance{300};
    // Campaign-wide global-map presentation rule. Local province maps are
    // intentionally always fully visible; this setting applies only here.
    bool fogOfWarEnabled{true};
    // These sliders affect only selected province values. Layout, starts and
    // topology use separate deterministic streams and therefore remain stable.
    double buildableWealthScale{1.0};
    double cityWealthScale{1.0};
    double banditStrengthScale{1.0};
};

using GlobalMapParameters = GlobalMapGenerationParameters;

class GlobalMap
{
public:
    GlobalMap() = default;
    GlobalMap(const GlobalMap&) = delete;
    GlobalMap& operator=(const GlobalMap&) = delete;
    GlobalMap(GlobalMap&&) noexcept = default;
    GlobalMap& operator=(GlobalMap&&) noexcept = default;

    bool AddProvince(std::unique_ptr<IProvince> province);
    bool AddConnection(ProvinceId from, ProvinceId to,
                       ProvinceConnectionId connectionId = InvalidProvinceConnectionId,
                       std::string definitionId = "land_route");
    bool SetKnowledge(PlayerId playerId, ProvinceId provinceId,
                     ProvinceKnowledgeLevel level);
    bool InitializeDiscovery(PlayerId playerId, ProvinceId homeProvinceId);
    bool SetBuildableOwner(PlayerId playerId, ProvinceId provinceId);

    BuildableProvince* FindBuildableProvince(ProvinceId id);
    const BuildableProvince* FindBuildableProvince(ProvinceId id) const;
    IProvinceConnection* FindConnection(ProvinceConnectionId id);
    const IProvinceConnection* FindConnection(ProvinceConnectionId id) const;
    IProvinceConnection* FindConnection(ProvinceId first, ProvinceId second);
    const IProvinceConnection* FindConnection(ProvinceId first, ProvinceId second) const;
    std::vector<ProvinceConnectionId> GetIncidentConnectionIds(ProvinceId id) const;
    // Replaces a province without changing its stable ID, layout or incident
    // connection index. Knowledge is copied to the replacement.
    bool TransformProvince(std::unique_ptr<IProvince> replacement);

    IProvince* FindProvince(ProvinceId id);
    const IProvince* FindProvince(ProvinceId id) const;
    std::vector<ProvinceId> GetNeighbors(ProvinceId id) const;
    // Deterministic unweighted route used by every campaign operation.
    // Connection IDs define stable tie-breaking across lockstep peers.
    bool FindShortestPath(ProvinceId source, ProvinceId target,
                          std::vector<ProvinceConnectionId>& path) const;
    bool HasPath(ProvinceId source, ProvinceId target) const;
    std::vector<ProvinceId> GetProvinceIds() const;
    std::vector<ProvinceConnectionId> GetConnectionIds() const;
    std::size_t GetProvinceCount() const { return provinces.size(); }
    std::size_t GetEdgeCount() const;
    std::size_t GetConnectionCount() const { return connections.size(); }
    bool IsAdjacent(ProvinceId from, ProvinceId to) const;
    bool IsConnected() const;
    void UpdateConnections();
    GlobalMapView BuildViewFor(PlayerId playerId) const;
    std::uint32_t GetGenerationSeed() const { return generationSeed; }
    void SetGenerationSeed(std::uint32_t seed) { generationSeed = seed; }
    bool IsFogOfWarEnabled() const { return fogOfWarEnabled; }
    void SetFogOfWarEnabled(bool enabled) { fogOfWarEnabled = enabled; }

private:
    std::map<ProvinceId, std::unique_ptr<IProvince>> provinces;
    std::map<ProvinceConnectionId, std::unique_ptr<IProvinceConnection>> connections;
    std::map<ProvinceId, std::vector<ProvinceConnectionId>> incidentConnectionIds;
    ProvinceConnectionId nextConnectionId{1};
    std::uint32_t generationSeed{0};
    bool fogOfWarEnabled{true};
};

struct GlobalMapGenerationResult
{
    bool success{false};
    GlobalMap map;
    std::map<PlayerId, ProvinceId> homeProvinceByPlayer;
    std::string failureReason;
};

class GlobalMapGenerator
{
public:
    static GlobalMapGenerationResult Generate(const GlobalMapGenerationParameters& parameters,
                                              const std::vector<PlayerId>& humanPlayers);
    static GlobalMapGenerationResult Generate(const GlobalMapGenerationParameters& parameters,
                                              int humanPlayerCount);
    static std::uint32_t DeriveProvinceSeed(std::uint32_t globalSeed,
                                            ProvinceId provinceId);
};

#endif
