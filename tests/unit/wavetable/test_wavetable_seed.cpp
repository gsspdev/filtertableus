// Phase 0 seed, kept and renamed into the wt: suite by the wavetable workstream.
#include <catch2/catch_test_macros.hpp>

#include "ftus/FactoryTables.h"

TEST_CASE("wt: factory table id strings round-trip", "[wavetable][seed]") {
    for (int i = 0; i < ftus::kNumFactoryTables; ++i) {
        const auto id = static_cast<ftus::FactoryTableId>(i);
        REQUIRE(ftus::factoryTableIdFromString(ftus::factoryTableIdString(id)) == id);
    }
    REQUIRE(ftus::factoryTableIdFromString("nope") == ftus::FactoryTableId::NumTables);
}
