#include <catch2/catch_test_macros.hpp>

#include "bridge/CircularBuffer.hpp"

TEST_CASE("push/pop", "[CircularBuffer]") {
    CircularBuffer buffer(4);
    char data[4];

    REQUIRE(buffer.push("1234", 4)); // [1234]
    REQUIRE(buffer.pop(data, 2)); // [34]
    REQUIRE(std::string(data, 2) == "12");
    
    // test wraparound
    REQUIRE(buffer.push("56", 2)); // [3456]
    REQUIRE_FALSE(buffer.push("78", 2)); // [3456] buffer full
    REQUIRE(buffer.pop(data, 4)); // []
    REQUIRE(std::string(data, 4) == "3456");
    REQUIRE_FALSE(buffer.pop(data, 4)); // [] buffer empty
}

TEST_CASE("peek/skip", "[CircularBuffer]") {
    CircularBuffer buffer(4);
    char data[4];

    REQUIRE_FALSE(buffer.peek(data, 2)); // [] nothing to peek
    REQUIRE_FALSE(buffer.skip(2)); // [] nothing to skip

    REQUIRE(buffer.push("1234", 4)); // [1234]
    REQUIRE(buffer.peek(data, 2) == 2); // [1234]
    REQUIRE(std::string(data, 2) == "12");
    REQUIRE(buffer.skip(2)); // [34]
    REQUIRE(buffer.peek(data, 1) == 1); // [34]
    REQUIRE(std::string(data, 1) == "3");
}