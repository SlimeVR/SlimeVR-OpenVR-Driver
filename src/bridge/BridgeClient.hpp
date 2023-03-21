/*
    SlimeVR Code is placed under the MIT license
    Copyright (c) 2022 Eiren Rain and SlimeVR Contributors

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
#pragma once

#include <filesystem>
#include <uvw.hpp>
#include <thread>
#include <queue>
#include <stdint.h>
#include <openvr_driver.h>

#include "Logger.hpp"
#include "CircularBuffer.hpp"
#include "ProtobufMessages.pb.h"

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #define LE32_TO_NATIVE(x) (x)
#else
    #define LE32_TO_NATIVE(x) ( \
        ((uint32_t)(x) << 24) | \
        (((uint32_t)(x) << 8) & 0x00FF0000) | \
        (((uint32_t)(x) >> 8) & 0x0000FF00) | \
        ((uint32_t)(x) >> 24) \
    )
#endif
#define NATIVE_TO_LE32 LE32_TO_NATIVE

#define VRBRIDGE_MAX_MESSAGE_SIZE 1024

/**
 * @brief Cross-platform bridge client.
 *
 * Starts a thread with an event loop, which handles IPC.
 */
class BridgeClient {
public:
    /**
     * @param onMessageReceived Gets called from the event loop thread when a message is received and parsed.
    */
    BridgeClient(std::function<void(messages::ProtobufMessage &)> onMessageReceived) :
        messageCallback(onMessageReceived),
        path(makeBridgePath()),
        sendBuf(8192),
        recvBuf(8192)
        { }

    ~BridgeClient() {
        stop();
    }

    void start();
    void stop();
    void sendBridgeMessage(messages::ProtobufMessage &message);
    bool isConnected() {
        return connected;
    };

private:
    static std::string makeBridgePath();
    void runThread();
    void connect();
    void onRecv(const uvw::DataEvent &event);
    void disconnect();
    void sendWrites();

    CircularBuffer sendBuf;
    CircularBuffer recvBuf;
    std::string path;
    std::shared_ptr<uvw::PipeHandle> pipeHandle;
    std::shared_ptr<uvw::AsyncHandle> stopHandle;
    std::shared_ptr<uvw::AsyncHandle> writeHandle;
    std::thread thread;
    std::function<void(messages::ProtobufMessage &)> messageCallback;
    std::queue<std::string> messageQueue;
    std::atomic<bool> running = false;
    std::atomic<bool> connected = false;
};