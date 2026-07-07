#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "ftc/Denormals.h"

TEST_CASE("ScopedNoDenormals flushes subnormals to zero", "[denormals]") {
    volatile float tiny = 1e-38f;
    volatile float half = 0.5f;
    {
        ftc::ScopedNoDenormals guard;
        volatile float v = tiny;
        for (int i = 0; i < 8; ++i)
            v = v * half; // decays into the subnormal range; FTZ should zero it
        REQUIRE((v == 0.0f || std::fpclassify(v) != FP_SUBNORMAL));
    }
}
