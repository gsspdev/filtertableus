#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include "ftc/RealtimeExchange.h"

namespace {
struct Pair {
    int a = 0;
    int b = 0; // invariant under test: b == 2*a
};
} // namespace

TEST_CASE("TripleBuffer basic semantics", "[exchange]") {
    ftc::TripleBuffer<Pair> tb;
    Pair out{};
    REQUIRE_FALSE(tb.read(out));          // nothing written yet

    tb.write({1, 2});
    REQUIRE(tb.read(out));
    REQUIRE(out.a == 1);
    REQUIRE_FALSE(tb.read(out));          // consumed

    tb.write({2, 4});
    tb.write({3, 6});                     // latest wins
    REQUIRE(tb.read(out));
    REQUIRE(out.a == 3);
    REQUIRE_FALSE(tb.read(out));
}

TEST_CASE("TripleBuffer two-thread hammer never tears", "[exchange]") {
    ftc::TripleBuffer<Pair> tb;
    std::atomic<bool> stop{false};
    std::atomic<bool> torn{false};
    std::atomic<int> lastSeen{0};

    std::thread reader([&] {
        Pair p{};
        while (!stop.load(std::memory_order_relaxed)) {
            if (tb.read(p)) {
                if (p.b != 2 * p.a)
                    torn.store(true);
                if (p.a < lastSeen.load(std::memory_order_relaxed))
                    torn.store(true); // must be monotonic (latest-wins)
                lastSeen.store(p.a, std::memory_order_relaxed);
            }
        }
    });

    for (int i = 1; i <= 200000; ++i)
        tb.write({i, 2 * i});
    stop.store(true);
    reader.join();

    REQUIRE_FALSE(torn.load());
    REQUIRE(lastSeen.load() > 0);
}

TEST_CASE("ObjectHandoff lifetime and GC", "[exchange]") {
    ftc::ObjectHandoff<int> ho;
    REQUIRE(ho.acquire() == nullptr);

    auto a = std::make_shared<const int>(1);
    const int* rawA = a.get();
    ho.publish(a);
    a.reset();
    REQUIRE(ho.acquire() == rawA);
    REQUIRE(ho.retainedCount() == 1);
    ho.collectGarbage();
    REQUIRE(ho.retainedCount() == 1);     // still current

    auto b = std::make_shared<const int>(2);
    const int* rawB = b.get();
    ho.publish(std::move(b));
    REQUIRE(ho.acquire() == rawB);
    ho.collectGarbage();
    REQUIRE(ho.retainedCount() == 2);     // current + previous both kept (crossfade safety)

    auto c = std::make_shared<const int>(3);
    const int* rawC = c.get();
    ho.publish(std::move(c));
    REQUIRE(ho.acquire() == rawC);
    ho.collectGarbage();                   // 'a' no longer pending/current/previous
    REQUIRE(ho.retainedCount() == 2);
    REQUIRE(*ho.currentAcquired() == 3);
}
