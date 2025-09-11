#include <catch2/catch_test_macros.hpp>
#include <thread>

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

void consumer(int n, CircularBuffer& buf, int& sum1) {
    char k;
    int i = 0;
    while (i != n) {
        if (!buf.Pop(&k, 1)) continue;
        sum1 += k;
        i++;
    }
}

void threading(int size) {
    CircularBuffer buf(size);
    const int n = 1000000;

    int sum0 = 0, sum1 = 0;
    char v = 1;
    std::thread t { [&]() { consumer(n, buf, sum1); } };
    int i = 0;
    while (i != n) {
        if (!buf.Push(&v, 1)) continue;
        sum0 += v;
        v = 3 + 2 * v;
        i++;
    }
    t.join();
    REQUIRE(sum0 == sum1);
}

TEST_CASE("Threading8192", "[CircularBuffer]") {
    threading(8192);
}
TEST_CASE("Threading4", "[CircularBuffer]") {
    threading(4);
}
TEST_CASE("Threading1", "[CircularBuffer]") {
    threading(1);
}