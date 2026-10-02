/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#ifndef PLAYERBOTS_AUCTIONCOMMERCEPOLICY_H
#define PLAYERBOTS_AUCTIONCOMMERCEPOLICY_H
#include <algorithm>
#include <cstdint>

class AuctionCommercePolicy
{
public:
    static uint64_t Price(uint64_t vendorValue, uint64_t marketValue, uint32_t attempts)
    {
        if (!vendorValue || vendorValue > 2000000000 / 20)
            return 0;
        uint64_t price = marketValue ? std::clamp(marketValue, vendorValue * 2, vendorValue * 20) : vendorValue * 5;
        if (attempts)
            price = price * 90 / 100;
        return price;
    }

    static uint64_t ExpectedBenefit(uint64_t price, uint64_t cut, uint64_t deposit, uint64_t vendorValue)
    {
        if (cut > price || price > 2000000000 || deposit > 2000000000)
            return 0;
        // Conservative 50% chance of a sale; a successful sale returns its deposit.
        uint64_t expected = (price - cut) / 2;
        uint64_t cost = vendorValue + deposit / 2;
        return expected > cost ? expected - cost : 0;
    }

    static bool CanDeposit(uint64_t money, uint64_t reserve, uint64_t deposits, uint64_t nextDeposit)
    {
        return money > reserve && deposits <= (money - reserve) / 10 &&
               nextDeposit <= (money - reserve) / 10 - deposits;
    }
};
#endif
