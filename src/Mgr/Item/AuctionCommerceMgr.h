/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#ifndef PLAYERBOTS_AUCTIONCOMMERCEMGR_H
#define PLAYERBOTS_AUCTIONCOMMERCEMGR_H
#include "AuctionSellingObserver.h"
class PlayerbotAI;
class AuctionCommerceMgr
{
public:
    static bool QueuePlan(PlayerbotAI* botAI, uint64_t generation);
    static bool QueueInteraction(PlayerbotAI* botAI, uint64_t generation);
    static bool QueueFlight(PlayerbotAI* botAI, uint64_t generation);
    static void Fly(PlayerbotAI* botAI, AuctionSellingObserver* observer);
    static void Plan(PlayerbotAI* botAI, AuctionSellingObserver* observer);
    static void Interact(PlayerbotAI* botAI, AuctionSellingObserver* observer);
};
void AddPlayerbotCommerceScripts();
#endif
