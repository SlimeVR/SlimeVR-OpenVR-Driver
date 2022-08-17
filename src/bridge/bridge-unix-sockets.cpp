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

#define SOCKET_PATH "/SlimeVRDriver"

namespace {

class BasicLocalServer {
public:
    void Open(std::string_view path) {
        mAcceptor = LocalAcceptorSocket(path, 1);
        mPoller.AddAcceptor(mAcceptor->GetDescriptor());
        assert(mPoller.GetSize() == 1);
    }
    void Close() {
        mAcceptor.reset();
        mConnector.reset();
        mPoller.Clear();
    }
    void CloseConnector() {
        mConnector.reset();
        if (mPoller.GetSize() == 2) mPoller.Remove(1);
    }

    void UpdateOnce() {
        assert(IsOpen());
        constexpr int timeoutMs = 0; // no block
        mPoller.Poll(timeoutMs);

        if (!mAcceptor->Update(mPoller.At(0))) {
            Close();
            return;
        }
        if (!IsConnected()) {
            TryOpenConnector();
            return;
        }
        if (!mConnector->Update(mPoller.At(1))) {
            CloseConnector();
            return;
        }
    }
    /// send a byte buffer
    /// @tparam TBufIt iterator to contiguous memory
    /// @return number of bytes sent or nullopt if blocking
    template <typename TBufIt>
    std::optional<int> TrySend(TBufIt bufBegin, int bytesToSend) {
        assert(IsConnected());
        return mConnector->TrySend(bufBegin, bytesToSend);
    }
    /// receive a byte buffer
    /// @tparam TBufIt iterator to contiguous memory
    /// @return number of bytes written to buffer or nullopt if blocking
    template <typename TBufIt>
    std::optional<int> TryRecv(TBufIt bufBegin, int bufSize) {
        assert(IsConnected());
        return mConnector->TryRecv(bufBegin, bufSize);
    }

    bool IsOpen() const { return mAcceptor.has_value(); }
    bool IsConnected() const { return IsOpen() && mConnector.has_value(); }

private:
    void TryOpenConnector() {
        mConnector = mAcceptor->Accept();
        if (!mConnector) return;
        mPoller.AddConnector(mConnector->GetDescriptor());
        assert(mPoller.GetSize() == 2);
    }

    std::optional<LocalAcceptorSocket> mAcceptor{};
    std::optional<LocalConnectorSocket> mConnector{};
    event::Poller mPoller; // index 0 is acceptor, 1 is connector if open
};

/// @return iterator after header
template <typename TBufIt>
std::optional<TBufIt> WriteHeader(TBufIt bufBegin, int bufSize, int msgSize) {
    static constexpr int headerSize = 4;
    if (bufSize < headerSize) return std::nullopt; // header won't fit

    const auto size = static_cast<uint32_t>(msgSize + headerSize); // include header bytes in total size
    TBufIt it = bufBegin;
    *(it++) = static_cast<uint8_t>(size);
    *(it++) = static_cast<uint8_t>(size >> 8U);
    *(it++) = static_cast<uint8_t>(size >> 16U);
    *(it++) = static_cast<uint8_t>(size >> 24U);
    return it;
}

/// @return iterator after header
template <typename TBufIt>
std::optional<TBufIt> ReadHeader(TBufIt bufBegin, int bytesRecv, int& outMsgSize) {
    static constexpr int headerSize = 4;
    if (bytesRecv < headerSize) return std::nullopt; // header won't fit

    uint32_t size = 0;
    TBufIt it = bufBegin;
    size = static_cast<uint32_t>(*(it++));
    size |= static_cast<uint32_t>(*(it++)) << 8U;
    size |= static_cast<uint32_t>(*(it++)) << 16U;
    size |= static_cast<uint32_t>(*(it++)) << 24U;

    const auto totalSize = static_cast<int>(size);
    // expecting the recv bytes to be exactly the message, as SOCK_SEQPACKET maintains message boundaries
    if (totalSize < headerSize || totalSize != bytesRecv) return std::nullopt;
    outMsgSize = totalSize - headerSize;
    return it;
}

BasicLocalServer server{};

inline constexpr int BUFFER_SIZE = 1024;
using ByteBuffer = std::array<uint8_t, BUFFER_SIZE>;
ByteBuffer byteBuffer;

}

bool getNextBridgeMessage(messages::ProtobufMessage& message, SlimeVRDriver::VRDriver& driver) {
    if (!server.IsConnected()) return false;
    try {
        const auto bufBegin = byteBuffer.begin();

        const int bufferSize = static_cast<int>(std::distance(bufBegin, byteBuffer.end()));
        const std::optional<int> bytesRecv = server.TryRecv(bufBegin, bufferSize);
        if (!bytesRecv) return false; // blocking, poll and try again
        if (*bytesRecv == 0) return false; // no message waiting

        int outMsgSize = 0;
        const std::optional msgBeginIt = ReadHeader(bufBegin, *bytesRecv, outMsgSize);
        if (!msgBeginIt) {
            driver.Log("bridge recv error: invalid message header or size");
            return false;
        }
        if (outMsgSize == 0) {
            driver.Log("bridge recv error: empty message");
            return false;
        }
        if (!message.ParseFromArray(&(**msgBeginIt), outMsgSize)) {
            driver.Log("bridge recv error: failed to parse");
            return false;
        }
        return true;
    } catch (const std::system_error& e) {
        server.CloseConnector();
        driver.Log("bridge recv error: " + std::string(e.what()));
        return false;
    }
}

bool sendBridgeMessage(messages::ProtobufMessage& message, SlimeVRDriver::VRDriver& driver) {
    if (!server.IsConnected()) return false;
    try {
        const auto bufBegin = byteBuffer.begin();
        const auto bufferSize = static_cast<int>(std::distance(bufBegin, byteBuffer.end()));
        const auto msgSize = static_cast<int>(message.ByteSizeLong());
        const std::optional msgBeginIt = WriteHeader(bufBegin, bufferSize, msgSize);
        if (!msgBeginIt) return false; // header couldn't fit
        if (!message.SerializeToArray(&(**msgBeginIt), msgSize)) {
            driver.Log("bridge send error: failed to serialize");
            return false;
        }
        int bytesToSend = static_cast<int>(std::distance(bufBegin, *msgBeginIt + msgSize));
        if (bytesToSend == 0) {
            driver.Log("bridge send error: empty message");
            return false;
        }
        if (bytesToSend > bufferSize) {
            driver.Log("bridge send error: message too big");
            return false;
        }
        auto msgIt = bufBegin;
        while (bytesToSend > 0) {
            std::optional<int> bytesSent = server.TrySend(msgIt, bytesToSend);
            if (!bytesSent) {
                // blocking, poll and try again
                server.UpdateOnce();
                if (!server.IsConnected()) return false;
            } else if (*bytesSent <= 0) {
                // very unlikely given the small amount of data, something about filling up the internal buffer?
                // handle it the same as a would block error, and hope eventually it'll resolve itself
                server.UpdateOnce();
                if (!server.IsConnected()) return false;
            } else if (*bytesSent > bytesToSend) {
                // probably guaranteed to not happen
                driver.Log("bridge send error: sent bytes > bytes to send");
                return false;
            } else {
                // SOCK_SEQPACKET means sending the message in parts is likely unecessary...
                bytesToSend -= *bytesSent;
                msgIt += *bytesSent;
            }
        }

        return true;
    } catch (const std::exception& e) {
        server.CloseConnector();
        driver.Log("bridge send error: " + std::string(e.what()));
        return false;
    }
}

BridgeStatus runBridgeFrame(SlimeVRDriver::VRDriver& driver) {
    try {
        if (!server.IsOpen()) server.Open(SOCKET_PATH);

        server.UpdateOnce();

        if (!server.IsConnected()) {
            return BRIDGE_DISCONNECTED;
        }
        return BRIDGE_CONNECTED;
    } catch (const std::exception& e) {
        server.Close();
        driver.Log("bridge error: " + std::string(e.what()));
        return BRIDGE_ERROR;
    }
}

#endif // linux
