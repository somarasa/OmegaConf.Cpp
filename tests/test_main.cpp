#include <catch2/catch_test_macros.hpp>
#include "omegaconf/omegaconf.hpp"

TEST_CASE("OmegaConf basic version check", "[core]") {
    REQUIRE(omegaconf::version() == "0.1.0");
}
