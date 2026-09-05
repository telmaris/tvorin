#ifndef SNAPSHOT_TRANSFER_H
#define SNAPSHOT_TRANSFER_H

#include "core/PersistenceLimits.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Snapshot transfer is independent from the game state format. It moves one
// already-serialized payload safely over the Snapshot protocol channel.
struct SnapshotTransferLimits
{
    static constexpr std::size_t SnapshotChunkEnvelopeBytes = 12; // transferId + chunk index
    static constexpr std::size_t DefaultChunkDataBytes = 32 * 1024 - SnapshotChunkEnvelopeBytes;

    std::size_t maxTotalBytes{PersistenceLimits::MaxSerializedStateBytes};
    std::size_t maxChunkDataBytes{DefaultChunkDataBytes};
    std::size_t maxChunkCount{
        (PersistenceLimits::MaxSerializedStateBytes + DefaultChunkDataBytes - 1) /
        DefaultChunkDataBytes};
};

struct SnapshotTransferManifest
{
    std::uint64_t transferId{0};
    std::uint64_t simulationTick{0};
    std::uint64_t totalBytes{0};
    std::uint32_t chunkCount{0};
    std::uint64_t payloadHash{0};
};

struct SnapshotTransferChunk
{
    std::uint64_t transferId{0};
    std::uint32_t index{0};
    std::string data;
};

struct SnapshotTransferEnd
{
    std::uint64_t transferId{0};
    std::uint64_t payloadHash{0};
};

enum class SnapshotTransferError
{
    None,
    InvalidManifest,
    InvalidManifestEncoding,
    InvalidChunkEncoding,
    PayloadExceedsLimit,
    ChunkExceedsLimit,
    ChunkCountExceedsLimit,
    UnexpectedTransfer,
    TransferAlreadyFailed,
    ChunkIndexOutOfRange,
    InvalidChunkSize,
    ConflictingDuplicateChunk,
    PayloadHashMismatch,
    StorageAllocationFailed,
    TransferIncomplete
};

class SnapshotTransferCodec
{
public:
    static constexpr std::uint16_t ManifestVersion = 1;
    static constexpr std::size_t ManifestBytes = 38;
    static constexpr std::size_t ChunkHeaderBytes = SnapshotTransferLimits::SnapshotChunkEnvelopeBytes;
    static constexpr std::size_t EndBytes = 16;

    static std::uint64_t ComputePayloadHash(std::string_view payload);
    static bool IsManifestValid(const SnapshotTransferManifest& manifest,
                                const SnapshotTransferLimits& limits = {});
    static std::uint32_t CalculateChunkCount(std::size_t totalBytes,
                                             const SnapshotTransferLimits& limits = {});

    static bool CreateManifest(std::uint64_t transferId, std::uint64_t simulationTick,
                               std::string_view payload, SnapshotTransferManifest& manifest,
                               const SnapshotTransferLimits& limits = {});
    static bool CreateChunk(const SnapshotTransferManifest& manifest, std::string_view payload,
                            std::uint32_t index, SnapshotTransferChunk& chunk,
                            const SnapshotTransferLimits& limits = {});

    static bool SerializeManifest(const SnapshotTransferManifest& manifest, std::string& output,
                                  const SnapshotTransferLimits& limits = {});
    static SnapshotTransferError TryDeserializeManifest(std::string_view payload,
                                                        SnapshotTransferManifest& manifest,
                                                        const SnapshotTransferLimits& limits = {});
    static bool SerializeChunk(const SnapshotTransferChunk& chunk, std::string& output,
                               const SnapshotTransferLimits& limits = {});
    static SnapshotTransferError TryDeserializeChunk(std::string_view payload,
                                                     SnapshotTransferChunk& chunk,
                                                     const SnapshotTransferLimits& limits = {});
    static void SerializeEnd(const SnapshotTransferEnd& end, std::string& output);
    static SnapshotTransferError TryDeserializeEnd(std::string_view payload, SnapshotTransferEnd& end);
};

class SnapshotChunkAssembler
{
public:
    explicit SnapshotChunkAssembler(SnapshotTransferLimits limits = {});

    SnapshotTransferError Begin(const SnapshotTransferManifest& manifest);
    SnapshotTransferError AddChunk(const SnapshotTransferChunk& chunk);
    SnapshotTransferError Finish(const SnapshotTransferEnd& end);
    bool IsActive() const { return active; }
    bool IsComplete() const { return complete; }
    const SnapshotTransferManifest* GetManifest() const { return active ? &manifest : nullptr; }
    std::vector<std::uint32_t> GetMissingChunkIndices() const;

    // Moves the verified payload out of the assembler. The assembler returns to
    // its idle state only after a successful take.
    SnapshotTransferError TakeCompletedPayload(std::string& payload);
    void Reset();

private:
    std::size_t ExpectedChunkBytes(std::uint32_t index) const;

    SnapshotTransferLimits limits;
    SnapshotTransferManifest manifest;
    std::string assembledPayload;
    std::vector<bool> receivedChunks;
    std::size_t receivedCount{0};
    bool active{false};
    bool complete{false};
    bool failed{false};
};

#endif
