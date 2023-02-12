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
#include <cstdlib>
#include <filesystem>

#define TMP_DIR "/tmp"
#define SOCKET_NAME "SlimeVRDriver"

namespace fs = std::filesystem;
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

    int bytesRecv = 0;
    try {
        bytesRecv = client.RecvOnce(byteBuffer.begin(), HEADER_SIZE);
    } catch (const std::exception& e) {
        client.Close();
        driver.Log("bridge send error: " + std::string(e.what()));
        return false;
    }
    if (bytesRecv == 0) return false; // no message waiting

    int msgSize = 0;
    const std::optional msgBeginIt = ReadHeader(byteBuffer.begin(), bytesRecv, msgSize);
    if (!msgBeginIt) {
        driver.Log("bridge recv error: invalid message header or size");
        return false;
    }
    if (msgSize <= 0) {
        driver.Log("bridge recv error: empty message");
        return false;
    }
    try {
        if (!client.RecvAll(*msgBeginIt, msgSize)) {
            driver.Log("bridge recv error: client closed");
            return false;
        }
    } catch (const std::exception& e) {
        client.Close();
        driver.Log("bridge send error: " + std::string(e.what()));
        return false;
    }
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
            // TODO: do this once in the constructor or something
            if(const char* ptr = std::getenv("XDG_RUNTIME_DIR")) {
                const fs::path xdg_runtime = ptr;
                client.Open((xdg_runtime / SOCKET_NAME).native());
            } else {
                client.Open((fs::path(TMP_DIR) / SOCKET_NAME).native());
            }
        }
        client.UpdateOnce();

        if (!client.IsOpen()) {
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
