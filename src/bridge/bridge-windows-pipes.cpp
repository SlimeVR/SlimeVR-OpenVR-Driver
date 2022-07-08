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
 * Windows specific IPC between SteamVR driver/app and SlimeVR server based
 * on named pipes
 */
#include "bridge.hpp"
#include "../VRDriver.hpp"
#include <windows.h>


SlimeVRDriver::Bridge::Bridge(VRDriver* i_pDriver)
    : m_pDriver(i_pDriver)
    , m_pPipe(INVALID_HANDLE_VALUE)
    , m_pBuffer(new char[1024])
    , m_eState(EState::BRIDGE_DISCONNECTED)
    , m_bStop(false)
{}

SlimeVRDriver::Bridge::~Bridge()
{
    this->stop();
    delete[] this->m_pBuffer;
}

void SlimeVRDriver::Bridge::run() {

    messages::ProtobufMessage oMsg;
    
    while(!this->m_bStop) {
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

        // Read all msg
        while (this->fetchNextBridgeMessage(oMsg)) {
            std::lock_guard<std::recursive_mutex> lk(this->m_oMutex);
            this->m_aRecvQueue.push(std::move(oMsg));
        };


        while (true)
        {
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

void SlimeVRDriver::Bridge::start()
{
    this->m_bStop = false;
    this->m_oThread = std::thread(&Bridge::run, this);
}

void SlimeVRDriver::Bridge::stop()
{
    if (this->m_oThread.joinable()) {
        this->m_oThread.join();
    }
}

bool SlimeVRDriver::Bridge::fetchNextBridgeMessage(messages::ProtobufMessage& i_oMessage) {
    if (this->m_eState != EState::BRIDGE_CONNECTED) {
        return false;
    }

    DWORD dwRead;
    DWORD dwAvailable;
    uint32_t messageLength;
    bool bNewMessage = false;

    if(PeekNamedPipe(this->m_pPipe, &messageLength, 4, &dwRead, &dwAvailable, NULL)) {
        if(dwRead == 4 && messageLength <= 1023 && dwAvailable >= messageLength) {
            if(ReadFile(this->m_pPipe, this->m_pBuffer, messageLength, &dwRead, NULL)) {
                bNewMessage = i_oMessage.ParseFromArray(this->m_pBuffer + 4, messageLength - 4);
            }
            else {
                setBridgeError();
            }
        }
    } else {
        setBridgeError();
    }


    return bNewMessage;
}

void SlimeVRDriver::Bridge::sendBridgeMessageFromQueue(messages::ProtobufMessage& i_oMessage) {
    if (this->m_eState != EState::BRIDGE_CONNECTED) {
        return;
    }

    uint32_t size = static_cast<uint32_t>(i_oMessage.ByteSizeLong());
    if(size > 1020) {
        this->m_pDriver->Log("Message too big");
        return;
    }

    i_oMessage.SerializeToArray(this->m_pBuffer + 4, size);
    size += 4;
    *reinterpret_cast<uint32_t*>(this->m_pBuffer) = size;
    if(!WriteFile(this->m_pPipe, this->m_pBuffer, size, NULL, NULL)) {
        setBridgeError();
        return;
    }
}

void SlimeVRDriver::Bridge::setBridgeError() {
    this->m_eState = EState::BRIDGE_ERROR;
    this->m_pDriver->Log("Bridge error: " + std::to_string(GetLastError()));
}

void SlimeVRDriver::Bridge::resetPipe() {
    if(this->m_pPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(this->m_pPipe);
        this->m_pPipe = INVALID_HANDLE_VALUE;

        this->m_eState = EState::BRIDGE_DISCONNECTED;
        this->m_pDriver->Log("Pipe was reset");
    }
}

void SlimeVRDriver::Bridge::attemptPipeConnect() {
    this->m_pPipe = CreateFileA(PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0, // TODO : Overlapped
        NULL);

    if(m_pPipe != INVALID_HANDLE_VALUE) {
        this->m_eState = EState::BRIDGE_CONNECTED;
        this->m_pDriver->Log("Pipe was connected");
        return;
    }
}