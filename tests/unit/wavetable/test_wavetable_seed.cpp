// Phase 0 seed — the wavetable workstream replaces/extends this suite.
#include <catch2/catch_test_macros.hpp>

#include "ftus/FactoryTables.h"

TEST_CASE("factory table id strings round-trip", "[wavetable][seed]") {
    for (int i = 0; i < ftus::kNumFactoryTables; ++i) {
        const auto id = static_cast<ftus::FactoryTableId>(i);
        REQUIRE(ftus::factoryTableIdFromString(ftus::factoryTableIdString(id)) == id);
    }
    REQUIRE(ftus::factoryTableIdFromString("nope") == ftus::FactoryTableId::NumTables);
}
