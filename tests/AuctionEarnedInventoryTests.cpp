/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include <cassert>
#include <limits>

#include "../src/Mgr/Item/AuctionEarnedInventory.h"

int main()
{
    using Disposition = AuctionEarnedInventory::Disposition;
    constexpr AuctionEarnedInventory::Identity cloth{2589, 0, 0};
    constexpr AuctionEarnedInventory::Identity other{2592, 0, 0};
    constexpr AuctionEarnedInventory::Identity variant{2589, -12, 8};
    AuctionEarnedInventory inventory;

    // Old inventory and ordinary factory/vendor/craft acquisitions cannot
    // acquire provenance just because the bot owns them.
    assert(inventory.ObserveCount(1, cloth, 5) == 0);
    inventory.RecordStored(1, cloth, 5, 5);
    assert(inventory.ObserveCount(1, cloth, 5) == 0);
    inventory.RecordLoot(1, cloth, 5, 5);  // A snapshot expires unmatched acquisition.
    assert(inventory.ObserveCount(1, cloth, 5) == 0);

    // A matching native store/loot sequence grants only the actual earned
    // quantity. Replaying a loot callback cannot grant it a second time.
    inventory.RecordStored(2, cloth, 8, 3);
    inventory.RecordLoot(2, cloth, 8, 3);
    inventory.RecordLoot(2, cloth, 8, 3);
    assert(inventory.ObserveCount(2, cloth, 8) == 3);
    assert(AuctionEarnedInventory::Classify(8, 3, true, true) == Disposition::PartialEarned);

    // Subsequent earned additions to an existing stack accumulate normally.
    inventory.RecordStored(2, cloth, 10, 2);
    inventory.RecordLoot(2, cloth, 10, 2);
    assert(inventory.ObserveCount(2, cloth, 10) == 5);

    // Consumption is charged against earned goods first. Replenishing the
    // stack from an untrusted source must not restore consumed provenance.
    assert(inventory.ObserveCount(2, cloth, 8) == 3);
    inventory.RecordStored(2, cloth, 10, 2);
    assert(inventory.ObserveCount(2, cloth, 10) == 3);
    assert(inventory.ObserveCount(2, cloth, 6) == 0);
    assert(inventory.ObserveCount(2, cloth, 10) == 0);

    // Multi-destination storage returns only the final stack. Conservatively
    // recognize at most that stack, even if the loot event describes more.
    inventory.RecordStored(3, cloth, 2, 7);
    inventory.RecordLoot(3, cloth, 2, 7);
    assert(inventory.ObserveCount(3, cloth, 2) == 2);
    assert(AuctionEarnedInventory::Classify(2, 2, true, true) == Disposition::WholeStackCandidate);

    // Untrusted additions after consumption between snapshots are bounded by
    // the stock that existed before the declared addition.
    inventory.RecordStored(3, cloth, 2, 2);
    assert(inventory.ObserveCount(3, cloth, 2) == 0);

    // Mismatched store counts and item variants do not prove loot origin.
    inventory.RecordStored(4, cloth, 7, 2);
    inventory.RecordLoot(4, cloth, 7, 3);
    inventory.RecordLoot(4, variant, 7, 2);
    assert(inventory.ObserveCount(4, cloth, 7) == 0);
    inventory.RecordStored(4, cloth, 8, 1);
    inventory.RecordLoot(4, cloth, 8, 1);
    assert(inventory.ObserveCount(4, other, 8) == 0);

    // A transferred, missing or split-away GUID loses its allocation; the
    // destination never inherits provenance merely from matching metadata.
    inventory.RecordStored(5, cloth, 5, 5);
    inventory.RecordLoot(5, cloth, 5, 5);
    inventory.Forget(5);
    assert(inventory.ObserveCount(5, cloth, 5) == 0);
    inventory.RecordStored(6, cloth, 5, 5);
    inventory.RecordLoot(6, cloth, 5, 5);
    assert(inventory.ObserveCount(6, cloth, 3) == 3);
    assert(inventory.ObserveCount(7, cloth, 2) == 0);
    inventory.Retain({});
    assert(inventory.ObserveCount(6, cloth, 3) == 0);

    // Protected use and native trade restrictions override proven provenance.
    assert(AuctionEarnedInventory::Classify(5, 5, false, true) == Disposition::Protected);
    assert(AuctionEarnedInventory::Classify(5, 5, true, false) == Disposition::Untradeable);
    assert(AuctionEarnedInventory::Classify(5, 0, true, true) == Disposition::Unproven);
    assert(AuctionEarnedInventory::Classify(0, 0, true, true) == Disposition::Unproven);
    assert(AuctionEarnedInventory::Classify(5, 6, true, true) == Disposition::Unproven);

    // Fuzz the boundaries of count and additions without wrapping arithmetic.
    for (uint32_t count : {1u, 2u, 20u, std::numeric_limits<uint32_t>::max()})
    {
        for (uint32_t added : {1u, 20u, std::numeric_limits<uint32_t>::max()})
        {
            AuctionEarnedInventory bounded;
            bounded.RecordStored(8, cloth, count, added);
            bounded.RecordLoot(8, cloth, count, added);
            assert(bounded.ObserveCount(8, cloth, count) == std::min(count, added));
            assert(bounded.ObserveCount(8, cloth, 0) == 0);
            assert(bounded.ObserveCount(8, cloth, count) == 0);
        }
    }

    // Unmatched stores are bounded even when eligibility postpones snapshots.
    // Discarded events can lose provenance but cannot grant extra earned stock.
    AuctionEarnedInventory boundedHistory;
    for (uint64_t guid = 1; guid <= 300; ++guid)
        boundedHistory.RecordStored(guid, cloth, 1, 1);
    boundedHistory.RecordLoot(300, cloth, 1, 1);
    assert(boundedHistory.ObserveCount(300, cloth, 1) == 0);
    boundedHistory.Retain({});
    boundedHistory.RecordStored(300, cloth, 1, 1);
    boundedHistory.RecordLoot(300, cloth, 1, 1);
    assert(boundedHistory.ObserveCount(300, cloth, 1) == 1);

    AuctionEarnedInventory lifecycle;
    lifecycle.RecordStored(50, variant, 5, 5);
    lifecycle.RecordLoot(50, variant, 5, 5);
    assert(lifecycle.MarkAuction(50, 100));
    assert(!lifecycle.MarkAuction(50, 101));
    lifecycle.Forget(50);  // Native inventory removal retains the auction lifecycle.
    lifecycle.Retain({});
    assert(lifecycle.GetAuctionId(50) == 100);
    assert(lifecycle.ObserveCount(50, variant, 5) == 0);
    AuctionEarnedInventory restarted;
    assert(restarted.Load(lifecycle.Serialize(1234)) == 1234);
    assert(restarted.GetAuctionId(50) == 100);
    assert(restarted.RestoreReturn(50, 51, variant, 5, 5));
    assert(restarted.GetAttempts(51) == 1);
    assert(restarted.ObserveCount(51, variant, 5) == 5);
    assert(restarted.MarkAuction(51, 101));
    assert(restarted.RestoreReturn(51, 52, variant, 5, 5));
    assert(restarted.GetAttempts(52) == 2);
    assert(!restarted.MarkAuction(52, 102));
    // A returned stack merged into unrelated stock cannot prove that whole
    // stack. Replaying or mismatching the return cannot reset attempts.
    AuctionEarnedInventory merged;
    merged.RecordStored(60, cloth, 5, 5);
    merged.RecordLoot(60, cloth, 5, 5);
    assert(merged.MarkAuction(60, 200));
    assert(!merged.RestoreReturn(60, 61, other, 5, 5));
    assert(!merged.RestoreReturn(60, 61, cloth, 5, 4));
    assert(merged.RestoreReturn(60, 61, cloth, 8, 5));
    assert(merged.ObserveCount(61, cloth, 8) == 5);
    assert(!merged.MarkAuction(61, 201));
    assert(!merged.RestoreReturn(60, 61, cloth, 8, 5));
    assert(merged.GetAttempts(61) == 1);

    // Settling native auction mail removes just that auction, not unrelated
    // earned inventory or another outstanding listing.
    assert(lifecycle.HasAuctions());
    lifecycle.SettleAuction(999);
    assert(lifecycle.HasAuctions());
    lifecycle.SettleAuction(100);
    assert(!lifecycle.HasAuctions());
    assert(lifecycle.GetAuctionId(50) == 0);

    auto duplicate = merged.Serialize(0);
    duplicate[2] = 2;
    auto record = std::vector<uint32_t>(duplicate.begin() + 3, duplicate.end());
    duplicate.insert(duplicate.end(), record.begin(), record.end());
    assert(restarted.Load(duplicate) == 0);
    assert(restarted.ObserveCount(61, cloth, 8) == 0);

    auto corrupt = restarted.Serialize(1234);
    corrupt.pop_back();
    assert(restarted.Load(corrupt) == 0);
    assert(restarted.ObserveCount(52, variant, 5) == 0);
}
