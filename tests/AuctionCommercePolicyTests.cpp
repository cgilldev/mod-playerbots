/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#include <cassert>
#include <limits>

#include "../src/Mgr/Item/AuctionCommercePolicy.h"
int main()
{
    // Sparse markets use a bounded fallback; malicious/extreme asks cannot
    // make a cheap earned item an unlimited gold faucet.
    assert(AuctionCommercePolicy::Price(100, 0, 0) == 500);
    assert(AuctionCommercePolicy::Price(100, 1, 0) == 200);
    assert(AuctionCommercePolicy::Price(100, std::numeric_limits<uint64_t>::max(), 0) == 2000);
    assert(AuctionCommercePolicy::Price(100, 500, 1) == 450);
    assert(AuctionCommercePolicy::Price(0, 500, 0) == 0);
    assert(AuctionCommercePolicy::Price(std::numeric_limits<uint64_t>::max(), 0, 0) == 0);

    // A successful sale returns the deposit, while failure loses it. Fees,
    // vendor proceeds and failure risk can rule out an apparently high price.
    assert(AuctionCommercePolicy::ExpectedBenefit(500, 25, 30, 100) == 122);
    assert(AuctionCommercePolicy::ExpectedBenefit(200, 10, 100, 100) == 0);
    assert(AuctionCommercePolicy::ExpectedBenefit(500, 501, 30, 100) == 0);
    assert(AuctionCommercePolicy::ExpectedBenefit(std::numeric_limits<uint64_t>::max(), 0, 0, 0) == 0);

    // Preserve the spending reserve and cap the entire batch's deposit budget.
    assert(!AuctionCommercePolicy::CanDeposit(100, 100, 0, 1));
    assert(AuctionCommercePolicy::CanDeposit(1100, 100, 0, 100));
    assert(!AuctionCommercePolicy::CanDeposit(1100, 100, 0, 101));
    assert(AuctionCommercePolicy::CanDeposit(1100, 100, 90, 10));
    assert(!AuctionCommercePolicy::CanDeposit(1100, 100, 90, 11));
    assert(!AuctionCommercePolicy::CanDeposit(1100, 100, std::numeric_limits<uint64_t>::max(), 1));
}
