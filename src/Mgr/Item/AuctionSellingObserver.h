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
#include "EventMap.h"

class Item;
class PlayerbotAI;

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
    std::vector<Candidate> const& GetCandidates() const { return _candidates; }

private:
    static constexpr uint32_t EVENT_REFRESH = 1;
    static constexpr uint32_t REFRESH_MILLISECONDS = 60000;
    static constexpr std::size_t DISPOSITION_COUNT = 5;

    static AuctionEarnedInventory::Identity GetIdentity(Item const* item);
    void Refresh(PlayerbotAI* botAI);

    AuctionEarnedInventory _inventory;
    EventMap _events;
    std::vector<Candidate> _candidates;
    std::array<uint32_t, DISPOSITION_COUNT> _lastCounts{};
    bool _reported = false;
};

#endif
