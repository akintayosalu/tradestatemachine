// Sanity checks for the test framework itself. No engine code here.
// Copy this shape for your own test files: include the header, write TEST_CASEs.
#include "test_framework.hpp"

#include <string>

TEST_CASE("framework: CHECK passes on true") {
    CHECK(1 + 1 == 2);
}

TEST_CASE("framework: CHECK_EQ compares values") {
    std::string s = "abc";
    CHECK_EQ(s.size(), 3u);
    CHECK_EQ(s, std::string("abc"));
}

TEST_CASE("framework: REQUIRE guards later checks") {
    int values[] = {10, 20, 30};
    REQUIRE(sizeof(values) / sizeof(values[0]) == 3);
    CHECK_EQ(values[2], 30);
}
