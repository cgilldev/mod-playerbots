/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PROGRESSION_SESSION_POLICY_H
#define PROGRESSION_SESSION_POLICY_H

#include <algorithm>
#include <cstdint>
#include <limits>

namespace ProgressionSessionPolicy
{
constexpr uint32_t Disarmed = 0;
constexpr uint32_t Armed = 1;
constexpr uint32_t Running = 2;
constexpr uint32_t Completed = 3;
constexpr uint32_t Cancelled = 4;
constexpr uint32_t SafetyStopped = 5;

struct Snapshot
{
    uint32_t state;
    uint32_t seconds;
    uint32_t lastUpdate;
};

struct Input
{
    uint32_t now;
    uint32_t maximumSeconds;
    bool realPlayerOnline;
    bool cappedBot;
    bool safetyStop;
};

constexpr Snapshot Advance(Snapshot snapshot, Input input)
{
    if (snapshot.state != Armed && snapshot.state != Running)
        return snapshot;

    if (input.cappedBot || (input.maximumSeconds && snapshot.seconds >= input.maximumSeconds))
        return {Completed, snapshot.seconds, 0};
    if (input.safetyStop)
        return {SafetyStopped, snapshot.seconds, 0};

    if (snapshot.state == Armed)
    {
        if (!input.realPlayerOnline)
            return {Running, snapshot.seconds, input.now};
        return snapshot;
    }

    if (!input.realPlayerOnline && snapshot.lastUpdate && input.now > snapshot.lastUpdate)
    {
        uint64_t total = static_cast<uint64_t>(snapshot.seconds) + input.now - snapshot.lastUpdate;
        snapshot.seconds = static_cast<uint32_t>(std::min<uint64_t>(total, std::numeric_limits<uint32_t>::max()));
    }
    snapshot.lastUpdate = input.now;
    if (input.maximumSeconds && snapshot.seconds >= input.maximumSeconds)
        return {Completed, snapshot.seconds, 0};
    return snapshot;
}

constexpr uint32_t Arm(uint32_t state, bool cappedBot)
{
    if (state == Completed || state == Armed || state == Running)
        return state;
    return cappedBot ? Completed : Armed;
}

constexpr uint32_t Cancel(uint32_t state)
{
    return state == Armed || state == Running ? Cancelled : state;
}

constexpr uint32_t ActiveBotTarget(bool offlineRunning, bool realPlayerOnline, uint32_t configuredTarget,
    uint32_t configuredPool, uint32_t cohortSize)
{
    if (!offlineRunning || realPlayerOnline)
        return std::min<uint32_t>(16, configuredTarget);
    return std::min(configuredPool, cohortSize);
}
}

#endif
