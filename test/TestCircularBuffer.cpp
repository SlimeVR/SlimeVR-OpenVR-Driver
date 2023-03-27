#include <catch2/catch_test_macros.hpp>

#include "bridge/CircularBuffer.hpp"

TEST_CASE("Push/Pop", "[CircularBuffer]") {
    CircularBuffer buffer(4);
    char data[4];

    REQUIRE(buffer.Push("1234", 4)); // [1234]
    REQUIRE(buffer.BytesAvailable() == 4);
    REQUIRE(buffer.Pop(data, 2)); // [34]
    REQUIRE(buffer.BytesAvailable() == 2);
    REQUIRE(std::string(data, 2) == "12");
    
    // test wraparound
    REQUIRE(buffer.Push("56", 2)); // [3456]
    REQUIRE(buffer.BytesAvailable() == 4);
    REQUIRE_FALSE(buffer.Push("78", 2)); // [3456] buffer full
    REQUIRE(buffer.BytesAvailable() == 4);
    REQUIRE(buffer.Pop(data, 4)); // []
    REQUIRE(buffer.BytesAvailable() == 0);
    REQUIRE(std::string(data, 4) == "3456");
    REQUIRE_FALSE(buffer.Pop(data, 4)); // [] buffer empty
    REQUIRE(buffer.BytesAvailable() == 0);
}

TEST_CASE("Peek/Skip", "[CircularBuffer]") {
    CircularBuffer buffer(4);
    char data[4];

    REQUIRE_FALSE(buffer.Peek(data, 2)); // [] nothing to peek
    REQUIRE(buffer.BytesAvailable() == 0);
    REQUIRE_FALSE(buffer.Skip(2)); // [] nothing to skip
    REQUIRE(buffer.BytesAvailable() == 0);

    REQUIRE(buffer.Push("1234", 4)); // [1234]
    REQUIRE(buffer.BytesAvailable() == 4);
    REQUIRE(buffer.Peek(data, 2) == 2); // [1234]
    REQUIRE(buffer.BytesAvailable() == 4);
    REQUIRE(std::string(data, 2) == "12");
    REQUIRE(buffer.Skip(2)); // [34]
    REQUIRE(buffer.BytesAvailable() == 2);
    REQUIRE(buffer.Peek(data, 1) == 1); // [34]
    REQUIRE(buffer.BytesAvailable() == 2);
    REQUIRE(std::string(data, 1) == "3");
}