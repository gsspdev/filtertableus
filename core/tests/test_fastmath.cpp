#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "ftc/FastMath.h"

TEST_CASE("logAbsApprox absolute accuracy", "[fastmath]") {
    std::vector<float> in, out;
    for (float decade = 1e-6f; decade < 1e6f; decade *= 10.0f)
        for (float m = 1.0f; m < 10.0f; m += 0.173f)
            in.push_back(decade * m);
    in.push_back(-3.7f); // |x| path
    in.push_back(0.0f);  // clamp path
    out.resize(in.size());
    ftc::logAbsApprox(in.data(), out.data(), static_cast<int>(in.size()));
    for (size_t i = 0; i < in.size(); ++i) {
        const float a = std::max(std::fabs(in[i]), 1e-6f);
        REQUIRE_THAT(out[i], Catch::Matchers::WithinAbs(std::log(a), 5e-4));
    }
}

TEST_CASE("expApprox relative accuracy", "[fastmath]") {
    std::vector<float> in, out;
    for (float x = -30.0f; x <= 30.0f; x += 0.037f)
        in.push_back(x);
    out.resize(in.size());
    ftc::expApprox(in.data(), out.data(), static_cast<int>(in.size()));
    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE_THAT(out[i], Catch::Matchers::WithinRel(std::exp(in[i]), 2e-4f));
}

TEST_CASE("expApprox clamps extremes to finite", "[fastmath]") {
    const float in[2] = {-1000.0f, 1000.0f};
    float out[2];
    ftc::expApprox(in, out, 2);
    REQUIRE(std::isfinite(out[0]));
    REQUIRE(std::isfinite(out[1]));
    REQUIRE(out[0] > 0.0f);
}

TEST_CASE("expComplex matches std", "[fastmath]") {
    std::vector<std::complex<float>> in, out;
    for (float re = -8.0f; re <= 2.0f; re += 0.61f)
        for (float im = -3.1f; im <= 3.1f; im += 0.37f)
            in.emplace_back(re, im);
    out.resize(in.size());
    ftc::expComplex(in.data(), out.data(), static_cast<int>(in.size()));
    for (size_t i = 0; i < in.size(); ++i) {
        const auto ref = std::exp(in[i]);
        REQUIRE_THAT(out[i].real(), Catch::Matchers::WithinAbs(ref.real(), 5e-4 * (1.0 + std::abs(ref))));
        REQUIRE_THAT(out[i].imag(), Catch::Matchers::WithinAbs(ref.imag(), 5e-4 * (1.0 + std::abs(ref))));
    }
}
