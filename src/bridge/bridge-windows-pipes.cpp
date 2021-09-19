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
#if defined(WIN32) && defined(BRIDGE_USE_PIPES)
#include <windows.h>

#define PIPE_NAME "\\\\.\\pipe\\SlimeVRDriver"

unsigned long lastReconnectFrame = 0;

HANDLE pipe = INVALID_HANDLE_VALUE;
BridgeStatus currentBridgeStatus = BRIDGE_DISCONNECTED;
char buffer[1024];

void updatePipe();
void resetPipe();
void attemptPipeConnect();

BridgeStatus runBridgeFrame() {
    switch(currentBridgeStatus) {
        case BRIDGE_DISCONNECTED:
            attemptPipeConnect();
        break;
        case BRIDGE_ERROR:
            resetPipe();
        break;
        case BRIDGE_CONNECTED:
            updatePipe();
        break;
    }

    return currentBridgeStatus;
}

bool getNextBridgeMessage(ProtobufMessage &message) {
    DWORD dwRead;
    DWORD dwAvailable;
    if(currentBridgeStatus == BRIDGE_CONNECTED) {
        if(PeekNamedPipe(pipe, buffer, 4, &dwRead, &dwAvailable, NULL)) {
            if(dwRead == 4) {
                uint32_t messageLength = *reinterpret_cast<uint32_t*>(buffer);
                if(messageLength > 1024) {
                    // TODO Buffer overflow
                }
                if(dwAvailable >= messageLength) {
                    if(ReadFile(pipe, buffer, messageLength, &dwRead, NULL)) {
                        if(message.ParseFromArray(buffer + 4, messageLength - 4))
                            return true;
                    } else {
                        currentBridgeStatus = BRIDGE_ERROR;
                    }
                }
            }
        } else {
            currentBridgeStatus = BRIDGE_ERROR;
        }
    }
    return false;
}

bool sendBridgeMEssage(ProtobufMessage &message) {
    if(currentBridgeStatus == BRIDGE_CONNECTED) {

    }
    return false;
}

void updatePipe() {
}

void resetPipe() {
    if(pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
        currentBridgeStatus = BRIDGE_DISCONNECTED;
    }
}

void attemptPipeConnect() {
    pipe = CreateFileA(PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0, // TODO : Overlapped
        NULL);
    if(pipe != INVALID_HANDLE_VALUE) {
        currentBridgeStatus = BRIDGE_CONNECTED;
        // Log connected
        return;
    }
}


#endif // PLATFORM_WINDOWS && BRIDGE_USE_PIPES