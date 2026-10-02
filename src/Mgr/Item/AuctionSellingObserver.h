/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUCTIONSELLINGOBSERVER_H
#define PLAYERBOTS_AUCTIONSELLINGOBSERVER_H

#include <array>
#include <vector>

#include "AuctionEarnedInventory.h"
#include "DatabaseEnvFwd.h"
#include "EventMap.h"

class Item;
class PlayerbotAI;
class Player;
struct AuctionEntry;

class AuctionSellingObserver
{
public:
    struct Candidate
    {
        uint64_t ItemGuid;
        uint32_t Entry;
        uint32_t Count;
        int32_t RandomProperty;
        uint32_t SuffixFactor;
        uint64_t VendorValue;

        bool operator==(Candidate const&) const = default;
    };

    explicit AuctionSellingObserver(uint32_t initialDelayMilliseconds);
    void RecordStored(Item const* item, uint32_t count);
    void RecordLoot(Item const* item, uint32_t count);
    void Forget(Item const* item);
    void Update(PlayerbotAI* botAI, uint32_t elapsedMilliseconds);
    void Load(Player* bot);
    void Save(Player* bot, CharacterDatabaseTransaction trans);
    void ObserveCount(Item const* item, uint32_t count);
    void OnAuctionAdded(AuctionEntry* auction);
    bool IsReserved(uint64_t guid) const;
    static bool IsParticipant(Player* bot);
    void Reserve(std::vector<uint64_t> guids) { _reserved = std::move(guids); }
    bool CanPost(Item const* item);
    bool HasErrand() const { return _hasErrand; }
    bool IsPending() const { return _pending; }
    uint64_t GetGeneration() const { return _generation; }
    void Finish(PlayerbotAI* botAI);
    static constexpr char const* SETTINGS_SOURCE = "playerbot_commerce_v1";
    struct Destination
    {
        uint32_t Map;
        uint32_t Entry;
        uint32_t Spawn;
        float X;
        float Y;
        float Z;
        bool Mailbox;
    };
    Destination const& GetDestination() const { return _destination; }
    Destination const& GetMovementTarget() const { return NeedsFlight() ? _flightMaster : _destination; }
    bool NeedsFlight() const { return !_flightTaken && !_flightNodes.empty(); }
    std::vector<uint32_t> const& GetFlightNodes() const { return _flightNodes; }
    void SetFlight(Destination master, std::vector<uint32_t> nodes)
    {
        _flightMaster = master;
        _flightNodes = std::move(nodes);
        _flightTaken = false;
    }
    void MarkFlightTaken() { _flightTaken = true; }
    void AcceptDestination(Destination destination);
    void SetPending(bool pending);
    void SetPosting(uint64_t guid) { _postingGuid = guid; }
    void SetReturning(uint64_t guid, uint32_t count)
    {
        _returningGuid = guid;
        _returningCount = count;
    }
    void RecordIncoming(Item const* item);
    uint32_t GetAttempts(uint64_t guid) const { return _inventory.GetAttempts(guid); }
    uint32_t GetAuctionId(uint64_t guid) const { return _inventory.GetAuctionId(guid); }
    void Settle(uint64_t guid) { _inventory.Settle(guid); }
    void SettleAuction(uint32_t auctionId) { _inventory.SettleAuction(auctionId); }
    std::vector<Candidate> const& GetCandidates() const { return _candidates; }

private:
    static constexpr uint32_t EVENT_REFRESH = 1;
    static constexpr uint32_t EVENT_OPERATION_TIMEOUT = 2;
    static constexpr uint32_t REFRESH_MILLISECONDS = 60000;
    static constexpr std::size_t DISPOSITION_COUNT = 5;

    static AuctionEarnedInventory::Identity GetIdentity(Item const* item);
    static uint64_t NextGeneration();
    void Refresh(PlayerbotAI* botAI);

    AuctionEarnedInventory _inventory;
    EventMap _events;
    std::vector<Candidate> _candidates;
    std::vector<uint64_t> _reserved;
    std::array<uint32_t, DISPOSITION_COUNT> _lastCounts{};
    bool _reported = false;
    uint64_t _generation = 0;
    uint32_t _nextTrip = 0;
    uint32_t _tripMilliseconds = 0;
    bool _hasErrand = false;
    bool _pending = false;
    bool _mailPending = false;
    Destination _destination{};
    Destination _flightMaster{};
    std::vector<uint32_t> _flightNodes;
    bool _flightTaken = false;
    uint64_t _postingGuid = 0;
    uint64_t _returningGuid = 0;
    uint32_t _returningCount = 0;
};

#endif
