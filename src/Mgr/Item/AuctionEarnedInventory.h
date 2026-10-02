/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUCTIONEARNEDINVENTORY_H
#define PLAYERBOTS_AUCTIONEARNEDINVENTORY_H

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Conservative earned-stock ledger. Native inventory transactions persist this
// state; auction and mailbox handlers remain authoritative for item ownership.
class AuctionEarnedInventory
{
public:
    struct Identity
    {
        uint32_t Entry;
        int32_t RandomProperty;
        uint32_t SuffixFactor;

        bool operator==(Identity const&) const = default;
    };

    enum class Disposition
    {
        Protected,
        Untradeable,
        Unproven,
        PartialEarned,
        WholeStackCandidate
    };

    static Disposition Classify(uint32_t count, uint32_t earnedCount, bool surplus, bool tradeable)
    {
        if (!surplus)
            return Disposition::Protected;
        if (!tradeable)
            return Disposition::Untradeable;
        if (!count || !earnedCount || earnedCount > count)
            return Disposition::Unproven;
        if (earnedCount != count)
            return Disposition::PartialEarned;
        return Disposition::WholeStackCandidate;
    }

    // StoreNewItem precedes LootItem in the native loot path. New stock is
    // unproven until that matching loot event arrives, once, in the same call.
    void RecordStored(uint64_t guid, Identity identity, uint32_t count, uint32_t addedCount)
    {
        if (!guid || !count || !addedCount)
            return;
        // Long grouped/dead sessions may postpone inventory refresh. Bound
        // unmatched acquisition history rather than grow it without limit.
        if (_lots.size() >= MAX_TRACKED_ITEMS && !_lots.contains(guid))
            return;

        bool newGuid = !_lots.contains(guid);
        Lot& lot = _lots[guid];
        uint32_t actualAdded = newGuid ? std::min(count, addedCount) : std::min(lot.NativeAddedCount, addedCount);
        Observe(lot, identity, count);
        lot.EarnedCount = std::min(lot.EarnedCount, count - std::min(count, actualAdded));
        lot.PendingEarnedCount = actualAdded;
        lot.NativeAddedCount = 0;
        lot.PendingCount = count;
        lot.PendingAddedCount = addedCount;
    }

    void RecordLoot(uint64_t guid, Identity identity, uint32_t count, uint32_t lootedCount)
    {
        auto found = _lots.find(guid);
        if (found == _lots.end())
            return;

        Lot& lot = found->second;
        if (lot.ItemIdentity != identity || !lootedCount || lot.PendingCount != count ||
            lot.PendingAddedCount != lootedCount)
            return;

        lot.EarnedCount = static_cast<uint32_t>(std::min<uint64_t>(
            count, static_cast<uint64_t>(lot.EarnedCount) + std::min(count, lot.PendingEarnedCount)));
        lot.PendingAddedCount = 0;
        lot.PendingEarnedCount = 0;
    }

    // Native count events expose the actual per-stack delta. StoreNewItem's
    // count describes the entire loot batch, which may fill several stacks.
    void ObserveNativeCount(uint64_t guid, Identity identity, uint32_t previousCount, uint32_t count)
    {
        if (!guid || (_lots.size() >= MAX_TRACKED_ITEMS && !_lots.contains(guid)))
            return;
        Lot& lot = _lots[guid];
        if (lot.AuctionId)
            return;
        Observe(lot, identity, previousCount);
        Observe(lot, identity, count);
        lot.NativeAddedCount = count > previousCount ? count - previousCount : 0;
        lot.PendingAddedCount = 0;
        lot.PendingEarnedCount = 0;
    }

    uint32_t ObserveCount(uint64_t guid, Identity identity, uint32_t count)
    {
        auto found = _lots.find(guid);
        if (found == _lots.end())
            return 0;
        if (found->second.AuctionId)
            return 0;

        Observe(found->second, identity, count);
        // Acquisition callbacks are synchronous; a later snapshot cannot claim
        // an earlier unmatched store event as newly earned goods.
        found->second.PendingAddedCount = 0;
        found->second.PendingEarnedCount = 0;
        found->second.NativeAddedCount = 0;
        return found->second.EarnedCount;
    }

    void Forget(uint64_t guid)
    {
        if (!GetAuctionId(guid))
            _lots.erase(guid);
    }

    bool HasAuctions() const
    {
        return std::any_of(_lots.begin(), _lots.end(), [](auto const& lot) { return lot.second.AuctionId != 0; });
    }

    void Settle(uint64_t guid) { _lots.erase(guid); }
    void SettleAuction(uint32_t auctionId)
    {
        for (auto it = _lots.begin(); it != _lots.end();)
            if (it->second.AuctionId == auctionId)
                it = _lots.erase(it);
            else
                ++it;
    }

    void Retain(std::unordered_set<uint64_t> const& presentGuids)
    {
        for (auto it = _lots.begin(); it != _lots.end();)
        {
            if (!presentGuids.contains(it->first) && !it->second.AuctionId)
                it = _lots.erase(it);
            else
                ++it;
        }
    }

    uint32_t GetAttempts(uint64_t guid) const
    {
        auto it = _lots.find(guid);
        return it == _lots.end() ? 0 : it->second.Attempts;
    }

    uint32_t GetAuctionId(uint64_t guid) const
    {
        auto it = _lots.find(guid);
        return it == _lots.end() ? 0 : it->second.AuctionId;
    }

    bool MarkAuction(uint64_t guid, uint32_t auctionId)
    {
        auto it = _lots.find(guid);
        if (it == _lots.end() || !auctionId || it->second.AuctionId || it->second.Attempts >= 2 || !it->second.Count ||
            it->second.EarnedCount != it->second.Count)
            return false;
        it->second.AuctionId = auctionId;
        ++it->second.Attempts;
        return true;
    }

    bool RestoreReturn(uint64_t oldGuid, uint64_t newGuid, Identity identity, uint32_t count, uint32_t returnedCount)
    {
        auto old = _lots.find(oldGuid);
        if (old == _lots.end() || !old->second.AuctionId || old->second.ItemIdentity != identity ||
            returnedCount != old->second.Count || count < returnedCount)
            return false;
        uint32_t attempts = old->second.Attempts;
        _lots.erase(old);
        Lot& target = _lots[newGuid];
        Observe(target, identity, count);
        target.EarnedCount =
            static_cast<uint32_t>(std::min<uint64_t>(count, static_cast<uint64_t>(target.EarnedCount) + returnedCount));
        target.Attempts = std::max(target.Attempts, attempts);
        target.AuctionId = 0;
        return true;
    }

    std::vector<uint32_t> Serialize(uint32_t nextTrip) const
    {
        std::vector<uint32_t> words{1, nextTrip, 0};
        for (auto const& [guid, lot] : _lots)
        {
            if (!lot.EarnedCount && !lot.AuctionId)
                continue;
            words.insert(words.end(),
                         {static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32), lot.ItemIdentity.Entry,
                          std::bit_cast<uint32_t>(lot.ItemIdentity.RandomProperty), lot.ItemIdentity.SuffixFactor,
                          lot.Count, lot.EarnedCount, lot.Attempts, lot.AuctionId});
            ++words[2];
        }
        return words;
    }

    uint32_t Load(std::vector<uint32_t> const& words)
    {
        _lots.clear();
        if (words.size() < 3 || words[0] != 1 || words[2] > MAX_TRACKED_ITEMS ||
            words.size() != 3 + static_cast<std::size_t>(words[2]) * 9)
            return 0;
        for (std::size_t offset = 3; offset < words.size(); offset += 9)
        {
            uint64_t guid = words[offset] | (static_cast<uint64_t>(words[offset + 1]) << 32);
            Lot lot{{words[offset + 2], std::bit_cast<int32_t>(words[offset + 3]), words[offset + 4]},
                    words[offset + 5],
                    words[offset + 6],
                    0,
                    0,
                    words[offset + 7],
                    words[offset + 8]};
            if (!guid || !lot.ItemIdentity.Entry || !lot.Count || lot.EarnedCount > lot.Count || lot.Attempts > 2 ||
                (lot.AuctionId && (!lot.Attempts || lot.EarnedCount != lot.Count)) || _lots.contains(guid))
            {
                _lots.clear();
                return 0;
            }
            _lots[guid] = lot;
        }
        return words[1];
    }

private:
    static constexpr std::size_t MAX_TRACKED_ITEMS = 256;

    struct Lot
    {
        Identity ItemIdentity{};
        uint32_t Count = 0;
        uint32_t EarnedCount = 0;
        uint32_t PendingCount = 0;
        uint32_t PendingAddedCount = 0;
        uint32_t Attempts = 0;
        uint32_t AuctionId = 0;
        uint32_t NativeAddedCount = 0;
        uint32_t PendingEarnedCount = 0;
    };

    static void Observe(Lot& lot, Identity identity, uint32_t count)
    {
        if (lot.ItemIdentity != identity)
            lot = {identity};

        // Charge all observed consumption to earned stock first. Unknown
        // additions and a later count increase never restore provenance.
        if (count < lot.Count)
        {
            uint32_t removedCount = lot.Count - count;
            lot.EarnedCount -= std::min(lot.EarnedCount, removedCount);
        }
        lot.EarnedCount = std::min(lot.EarnedCount, count);
        lot.Count = count;
    }

    std::unordered_map<uint64_t, Lot> _lots;
};

#endif
