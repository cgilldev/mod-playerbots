/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#ifndef PLAYERBOTS_AUCTIONCOMMERCEACTION_H
#define PLAYERBOTS_AUCTIONCOMMERCEACTION_H
#include "AuctionCommerceMgr.h"
#include "AuctionSellingObserver.h"
#include "NewRpgBaseAction.h"

class AuctionCommerceAction : public NewRpgBaseAction
{
public:
    AuctionCommerceAction(PlayerbotAI* botAI) : NewRpgBaseAction(botAI, "auction commerce") {}
    bool isUseful() override
    {
        auto observer = botAI->GetAuctionSellingObserver();
        return observer && observer->HasErrand() && !observer->IsPending() && !bot->IsInCombat() && bot->IsAlive() &&
               !bot->GetGroup() && !botAI->GetMaster();
    }
    bool Execute(Event /*event*/) override
    {
        auto observer = botAI->GetAuctionSellingObserver();
        if (!observer || !isUseful())
            return false;
        if (bot->IsInFlight())
            return true;
        auto const& target = observer->GetMovementTarget();
        if (bot->GetMapId() != target.Map)
        {
            observer->Finish(botAI);
            return false;
        }
        WorldPosition destination(target.Map, target.X, target.Y, target.Z);
        if (bot->GetExactDist(target.X, target.Y, target.Z) <= INTERACTION_DISTANCE - 1.0f)
        {
            observer->SetPending(observer->NeedsFlight()
                                     ? AuctionCommerceMgr::QueueFlight(botAI, observer->GetGeneration())
                                     : AuctionCommerceMgr::QueueInteraction(botAI, observer->GetGeneration()));
            return observer->IsPending();
        }
        bool moving = MoveFarTo(destination, false);
        if (!moving && botAI->rpgInfo.stuckAttempts >= 5 && GetMSTimeDiffToNow(botAI->rpgInfo.stuckTs) >= stuckTime)
            observer->Finish(botAI);
        return moving;
    }
};
#endif
