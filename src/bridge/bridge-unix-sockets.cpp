/*
    SlimeVR Code is placed under the MIT license
    Copyright (c) 2021 Eiren Rain

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/
/**
 * Linux specific IPC between SteamVR driver/app and SlimeVR server based
 * on unix sockets
 */
#include "bridge.hpp"
#ifdef __linux__
#include "unix-sockets.hpp"
#include <string_view>
#include <memory>

#define SOCKET_PATH "/tmp/SlimeVRDriver"

namespace {

inline constexpr int HEADER_SIZE = 4;
/// @return iterator after header
template <typename TBufIt>
std::optional<TBufIt> WriteHeader(TBufIt bufBegin, int bufSize, int msgSize) {
    const int totalSize = msgSize + HEADER_SIZE; // include header bytes in total size
    if (bufSize < totalSize) return std::nullopt; // header won't fit

    const auto size = static_cast<uint32_t>(totalSize);
    TBufIt it = bufBegin;
    *(it++) = static_cast<uint8_t>(size);
    *(it++) = static_cast<uint8_t>(size >> 8U);
    *(it++) = static_cast<uint8_t>(size >> 16U);
    *(it++) = static_cast<uint8_t>(size >> 24U);
    return it;
}

/// @return iterator after header
template <typename TBufIt>
std::optional<TBufIt> ReadHeader(TBufIt bufBegin, int numBytesRecv, int& outMsgSize) {
    if (numBytesRecv < HEADER_SIZE) return std::nullopt; // header won't fit

    uint32_t size = 0;
    TBufIt it = bufBegin;
    size = static_cast<uint32_t>(*(it++));
    size |= static_cast<uint32_t>(*(it++)) << 8U;
    size |= static_cast<uint32_t>(*(it++)) << 16U;
    size |= static_cast<uint32_t>(*(it++)) << 24U;

    const auto totalSize = static_cast<int>(size);
    if (totalSize < HEADER_SIZE) return std::nullopt;
    outMsgSize = totalSize - HEADER_SIZE;
    return it;
}

BasicLocalClient client{};

inline constexpr int BUFFER_SIZE = 1024;
using ByteBuffer = std::array<uint8_t, BUFFER_SIZE>;
ByteBuffer byteBuffer;

}

bool getNextBridgeMessage(messages::ProtobufMessage& message, SlimeVRDriver::VRDriver& driver) {
    if (!client.IsOpen()) return false;

    int bytesRecv = client.Recv(byteBuffer.begin(), HEADER_SIZE);
    if (bytesRecv == 0) return false; // no message waiting

    std::string dbg = "bridge debug: recv ";
    dbg += std::to_string(bytesRecv) + "b: ";

    int bytesToRead = 0;
    const std::optional msgBeginIt = ReadHeader(byteBuffer.begin(), bytesRecv, bytesToRead);
    if (!msgBeginIt) {
        driver.Log("bridge recv error: invalid message header or size");
        return false;
    }
    if (bytesToRead <= 0) {
        driver.Log("bridge recv error: empty message");
        return false;
    }
    const int msgSize = bytesToRead;

    int maxIter = 100;
    auto bufIt = *msgBeginIt;
    while (--maxIter && bytesToRead > 0) {
        try {
            bytesRecv = client.Recv(bufIt, bytesToRead);
            dbg += std::to_string(bytesRecv) + ",";
        } catch (const std::exception& e) {
            client.Close();
            driver.Log("bridge recv error: " + std::string(e.what()));
            return false;
        }
        if (!client.IsOpen()) return false;

        if (bytesRecv == 0) {
            // nothing received but expecting more...
            client.UpdateOnce(); // poll again
        } else if (bytesRecv < 0 || bytesRecv > bytesToRead) {
            // should not be possible
            throw std::length_error("bytesRecv");
        } else {
            // read some or all of the message
            bytesToRead -= bytesRecv;
            bufIt += bytesRecv;
        }
    }

    if (maxIter == 0) {
        driver.Log("bridge recv error: infinite loop");
        return false;
    }
    driver.Log(dbg);

    if (!message.ParseFromArray(&(**msgBeginIt), msgSize)) {
        driver.Log("bridge recv error: failed to parse");
        return false;
    }

    return true;
}

bool sendBridgeMessage(messages::ProtobufMessage& message, SlimeVRDriver::VRDriver& driver) {
    if (!client.IsOpen()) return false;
    const auto bufBegin = byteBuffer.begin();
    const auto bufferSize = static_cast<int>(std::distance(bufBegin, byteBuffer.end()));
    const auto msgSize = static_cast<int>(message.ByteSizeLong());
    const std::optional msgBeginIt = WriteHeader(bufBegin, bufferSize, msgSize);
    if (!msgBeginIt) {
        driver.Log("bridge send error: failed to write header");
        return false;
    }
    if (!message.SerializeToArray(&(**msgBeginIt), msgSize)) {
        driver.Log("bridge send error: failed to serialize");
        return false;
    }
    int bytesToSend = static_cast<int>(std::distance(bufBegin, *msgBeginIt + msgSize));
    if (bytesToSend <= 0) {
        driver.Log("bridge send error: empty message");
        return false;
    }
    if (bytesToSend > bufferSize) {
        driver.Log("bridge send error: message too big");
        return false;
    }
    try {
        driver.Log("bridge debug: send " + std::to_string(bytesToSend) + "b");
        return client.Send(bufBegin, bytesToSend);
    } catch (const std::exception& e) {
        client.Close();
        driver.Log("bridge send error: " + std::string(e.what()));
        return false;
    }
}

BridgeStatus runBridgeFrame(SlimeVRDriver::VRDriver& driver) {
    try {
        if (!client.IsOpen()) {
            client.Open(SOCKET_PATH);
            driver.Log("bridge debug: open = " + std::to_string(client.IsOpen()));
        }
        client.UpdateOnce();

        if (!client.IsOpen()) {
            driver.Log("bridge debug: disconnected");
            return BRIDGE_DISCONNECTED;
        }
        return BRIDGE_CONNECTED;
    } catch (const std::exception& e) {
        client.Close();
        driver.Log("bridge error: " + std::string(e.what()));
        return BRIDGE_ERROR;
    }
}

#endif // linux
