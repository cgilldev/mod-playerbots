#include "../src/Bot/ProgressionSessionPolicy.h"

#include <cassert>

using namespace ProgressionSessionPolicy;

int main()
{
    constexpr uint32_t limit = 12 * 3600;

    // A newly armed session starts only after the last real player leaves.
    assert(Arm(Disarmed, false) == Armed);
    assert(Advance({Armed, 0, 0}, {100, limit, true, false, false}).state == Armed);
    Snapshot running = Advance({Armed, 0, 0}, {110, limit, false, false, false});
    assert(running.state == Running && running.seconds == 0 && running.lastUpdate == 110);

    // First cap and a bot already capped at arm both complete immediately.
    assert(Advance(running, {111, limit, false, true, false}).state == Completed);
    assert(Arm(Disarmed, true) == Completed);
    assert(Arm(Completed, false) == Completed); // Restart cannot rearm a completed session.

    // Player time does not consume the cumulative offline budget.
    running = Advance(running, {170, limit, false, false, false});
    assert(running.seconds == 60);
    running = Advance(running, {400, limit, true, false, false});
    assert(running.seconds == 60 && running.lastUpdate == 400);
    running = Advance(running, {460, limit, false, false, false});
    assert(running.seconds == 120);

    // A restarted world has no prior wall-clock timestamp to charge.
    Snapshot restarted{Running, 120, 0};
    restarted = Advance(restarted, {10000, limit, false, false, false});
    assert(restarted.seconds == 120 && restarted.lastUpdate == 10000);
    assert(Advance({Running, limit, 0}, {10001, limit, true, false, false}).state == Completed);

    // Cancellation and a safety stop are terminal until a GM explicitly arms again.
    assert(Cancel(Running) == Cancelled);
    assert(Advance({Cancelled, 20, 0}, {200, limit, false, false, false}).state == Cancelled);
    assert(Advance({Running, 20, 100}, {101, limit, false, false, true}).state == SafetyStopped);
    assert(Arm(SafetyStopped, false) == Armed);

    // Offline operation may activate the eligible pool, but a player return
    // immediately restores the normal 16-bot cap.
    assert(ActiveBotTarget(true, false, 16, 80, 62) == 62);
    assert(ActiveBotTarget(true, false, 16, 40, 62) == 40);
    assert(ActiveBotTarget(true, true, 16, 80, 62) == 16);
    assert(ActiveBotTarget(false, false, 20, 80, 62) == 16);
}
