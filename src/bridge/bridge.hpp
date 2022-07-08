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
 * Header file for cross-platform handling of IPC between SteamVR driver/app
 * and SlimeVR server
 */
#pragma once

#define PIPE_NAME "\\\\.\\pipe\\SlimeVRDriver"

#include <variant>
#include <optional>
#include "ProtobufMessages.pb.h"
#include <thread>
#include <mutex>
#include <queue>

namespace SlimeVRDriver {
    class VRDriver;

    class Bridge {
    public:
        enum class EState {
            BRIDGE_DISCONNECTED = 0,
            BRIDGE_CONNECTED = 1,
            BRIDGE_ERROR = 2
        };

        Bridge( VRDriver* i_pDriver);
        ~Bridge();
    private:
        VRDriver* m_pDriver;
        void *m_pPipe;
        char *m_pBuffer;
        EState m_eState;
        std::recursive_mutex m_oMutex;
        std::thread m_oThread;
        std::queue<messages::ProtobufMessage>	m_aSendQueue;
        std::queue<messages::ProtobufMessage>	m_aRecvQueue;
        bool m_bStop;
    private:
        void setBridgeError();
        void resetPipe();
        void attemptPipeConnect();
        void run();
        bool fetchNextBridgeMessage(messages::ProtobufMessage& i_oMessage);
        void sendBridgeMessageFromQueue(messages::ProtobufMessage& i_oMessage);
    public:
        __inline EState state() {
            return this->m_eState;
        }
    public:
        void start();
        void stop();
    public:
        bool getNextBridgeMessage(messages::ProtobufMessage& i_oMessage) {
            std::lock_guard<std::recursive_mutex> lk(this->m_oMutex);

            if (m_aRecvQueue.empty()) {
                return false;
            }

            i_oMessage = std::move(this->m_aRecvQueue.front());
            this->m_aRecvQueue.pop();
            return true;
        }
        void sendBridgeMessage(messages::ProtobufMessage& i_oMessage) {
            std::lock_guard<std::recursive_mutex> lk(this->m_oMutex);
            this->m_aSendQueue.push(std::move(i_oMessage));
        }
    };
}
