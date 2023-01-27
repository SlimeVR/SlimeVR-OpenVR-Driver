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
#include "../VRDriver.hpp"
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

SlimeVRDriver::Bridge::Bridge(VRDriver *i_pDriver)
        : m_pDriver(i_pDriver), m_pPipe(nullptr), m_pBuffer(nullptr),
          m_eState(EState::BRIDGE_DISCONNECTED), m_bStop(false) {
}

SlimeVRDriver::Bridge::~Bridge() {
    this->stop();
}

void SlimeVRDriver::Bridge::run() {

    messages::ProtobufMessage oMsg;

    while (!this->m_bStop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));

        switch (this->m_eState) {
            case EState::BRIDGE_DISCONNECTED:
                attemptPipeConnect();
                break;
            case EState::BRIDGE_ERROR:
                resetPipe();
                break;
            case EState::BRIDGE_CONNECTED:
            default:
                break;
        }

        client.UpdateOnce();

        // Read all msg
        while (this->fetchNextBridgeMessage(oMsg)) {
            std::lock_guard<std::recursive_mutex> lk(this->m_oMutex);
            this->m_aRecvQueue.push(std::move(oMsg));
        };


        while (true) {
            {   // Custom scope for send queue mutex
                std::lock_guard<std::recursive_mutex> lk(this->m_oMutex);
                if (this->m_aSendQueue.empty()) {
                    break;
                }

                oMsg = std::move(this->m_aSendQueue.front());
                this->m_aSendQueue.pop();
            }

            this->sendBridgeMessageFromQueue(oMsg);
        }

    }
}
void SlimeVRDriver::Bridge::sendBridgeMessageFromQueue(messages::ProtobufMessage &i_oMessage) {
    if (!client.IsOpen()) {
        this->m_pDriver->Log("bridge send error: failed to write header");
        this->setBridgeError();
    };

    const auto bufBegin = byteBuffer.begin();
    const auto bufferSize = static_cast<int>(std::distance(bufBegin, byteBuffer.end()));
    const auto msgSize = static_cast<int>(i_oMessage.ByteSizeLong());
    const std::optional msgBeginIt = WriteHeader(bufBegin, bufferSize, msgSize);
    if (!msgBeginIt) {
        this->m_pDriver->Log("bridge send error: failed to write header");
        return;
    }
    if (!i_oMessage.SerializeToArray(&(**msgBeginIt), msgSize)) {
        this->m_pDriver->Log("bridge send error: failed to serialize");
        return;
    }
    int bytesToSend = static_cast<int>(std::distance(bufBegin, *msgBeginIt + msgSize));
    if (bytesToSend <= 0) {
        this->m_pDriver->Log("bridge send error: empty message");
        return;
    }
    if (bytesToSend > bufferSize) {
        this->m_pDriver->Log("bridge send error: message too big");
        return;
    }
    try {
        client.Send(bufBegin, bytesToSend);
    } catch (const std::exception& e) {
        this->setBridgeError();
        this->m_pDriver->Log("bridge send error: " + std::string(e.what()));
        return;
    }
}

bool SlimeVRDriver::Bridge::fetchNextBridgeMessage(messages::ProtobufMessage &i_oMessage) {
    if (!client.IsOpen()) return false;

    int bytesRecv = 0;
    try {
        bytesRecv = client.RecvOnce(byteBuffer.begin(), HEADER_SIZE);
    } catch (const std::exception& e) {
        client.Close();
        this->m_pDriver->Log("bridge send error: " + std::string(e.what()));
        return false;
    }
    if (bytesRecv == 0) return false; // no message waiting

    int msgSize = 0;
    const std::optional msgBeginIt = ReadHeader(byteBuffer.begin(), bytesRecv, msgSize);
    if (!msgBeginIt) {
        this->m_pDriver->Log("bridge recv error: invalid message header or size");
        return false;
    }
    if (msgSize <= 0) {
        this->m_pDriver->Log("bridge recv error: empty message");
        return false;
    }
    try {
        if (!client.RecvAll(*msgBeginIt, msgSize)) {
            this->m_pDriver->Log("bridge recv error: client closed");
            return false;
        }
    } catch (const std::exception& e) {
        client.Close();
        this->m_pDriver->Log("bridge send error: " + std::string(e.what()));
        return false;
    }
    if (!i_oMessage.ParseFromArray(&(**msgBeginIt), msgSize)) {
        this->m_pDriver->Log("bridge recv error: failed to parse");
        return false;
    }

    return true;
}

void SlimeVRDriver::Bridge::setBridgeError() {
    this->m_eState = EState::BRIDGE_ERROR;
}

void SlimeVRDriver::Bridge::resetPipe() {
    try {
       client.Close();
       client.Open(SOCKET_PATH);
       this->m_pDriver->Log("Pipe was reset");
    } catch (const std::exception& e) {
        this->m_pDriver->Log("bridge error: " + std::string(e.what()));
        setBridgeError();
    }

}
void SlimeVRDriver::Bridge::attemptPipeConnect() {
   // Open pipe only called the first time
   this->resetPipe();
}


#endif // linux
