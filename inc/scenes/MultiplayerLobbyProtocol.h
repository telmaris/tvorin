#ifndef MULTIPLAYER_LOBBY_PROTOCOL_H
#define MULTIPLAYER_LOBBY_PROTOCOL_H

#include "simulation/MapGenerator.h"
#include "core/CampaignGeneration.h"

#include <string>

// Positional lobby payloads are kept stable for older LAN clients. AI fields
// remain in their historical positions, but are compatibility-only and are
// neutralized on both serialization and deserialization.
namespace MultiplayerLobbyProtocol
{
    std::string SerializeStart(const std::string& sessionName, const MapParameters& params);
    bool TryDeserializeStart(const std::string& payload, std::string& sessionName, MapParameters& params);
    std::string SerializeStart(const std::string& sessionName,
                               const CampaignGenerationParameters& params);
    bool TryDeserializeStart(const std::string& payload, std::string& sessionName,
                             CampaignGenerationParameters& params);

    std::string SerializeState(const std::string& sessionName,
                               const std::string& hostName,
                               const std::string& remoteName,
                               const MapParameters& params);
    bool TryDeserializeState(const std::string& payload,
                             std::string& sessionName,
                             std::string& hostName,
                             std::string& remoteName,
                             MapParameters& params);
    std::string SerializeState(const std::string& sessionName,
                               const std::string& hostName,
                               const std::string& remoteName,
                               const CampaignGenerationParameters& params);
    bool TryDeserializeState(const std::string& payload,
                             std::string& sessionName,
                             std::string& hostName,
                             std::string& remoteName,
                             CampaignGenerationParameters& params);
}

#endif
