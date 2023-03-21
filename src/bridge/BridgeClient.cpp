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
#include "BridgeClient.hpp"
#include <sstream>

namespace fs = std::filesystem;
using namespace std::literals::chrono_literals;

#define WINDOWS_PIPE_NAME "\\\\.\\pipe\\SlimeVRDriver"
#define UNIX_TMP_DIR "/tmp"
#define UNIX_SOCKET_NAME "SlimeVRDriver"

std::string BridgeClient::makeBridgePath() {
#ifdef __linux__
    if (const char* ptr = std::getenv("XDG_RUNTIME_DIR")) {
        const fs::path xdg_runtime = ptr;
        return (xdg_runtime / UNIX_SOCKET_NAME).string();
    } else {
        return (fs::path(UNIX_TMP_DIR) / UNIX_SOCKET_NAME).string();
    }
#else 
    return WINDOWS_PIPE_NAME;
#endif
}

void BridgeClient::start() {
    if (running) return;
    running = true;
    thread = std::thread(&BridgeClient::runThread, this);
}

void BridgeClient::stop() {
    if (!running) return;
    Log("Bridge: stopping");
    running = false;
    stopHandle->send();
    thread.join();
}

void BridgeClient::runThread() {
    Log("Bridge: thread started");
    stopHandle = uvw::Loop::getDefault()->resource<uvw::AsyncHandle>();
    writeHandle = uvw::Loop::getDefault()->resource<uvw::AsyncHandle>();

    stopHandle->on<uvw::AsyncEvent>([&](const uvw::AsyncEvent&, uvw::AsyncHandle& handle) {
        Log("Bridge: closing handles");
        pipeHandle->close();
        writeHandle->close();
        handle.close();
    });
    
    writeHandle->on<uvw::AsyncEvent>([&](const uvw::AsyncEvent&, uvw::AsyncHandle& handle) {
        sendWrites();
    });
    
    connect();
    uvw::Loop::getDefault()->run();
    Log("Bridge: thread stopped");
}

void BridgeClient::connect() {
    Log("Bridge: connecting");
    recvBuf.clear();
    sendBuf.clear();

    /* ipc = false -> pipe will be used for handle passing between processes? no */
    pipeHandle = uvw::Loop::getDefault()->resource<uvw::PipeHandle>(false);

    pipeHandle->on<uvw::ConnectEvent>([&](const uvw::ConnectEvent &, uvw::PipeHandle &) {
        pipeHandle->read();
        Log("Bridge: connecting");
        connected = true;
    });

    pipeHandle->on<uvw::EndEvent>([&](const uvw::EndEvent &, uvw::PipeHandle &) {
        Log("Bridge: disconnected");
        disconnect();
    });

    pipeHandle->on<uvw::DataEvent>([&](const uvw::DataEvent &event, uvw::PipeHandle &) {
        onRecv(event);
    });

    pipeHandle->on<uvw::ErrorEvent>([&](const uvw::ErrorEvent &event, uvw::PipeHandle &) {
        Log("Bridge: Pipe error: %s", event.what());
        disconnect();
    });

    pipeHandle->connect(path);
}

void BridgeClient::disconnect() {
    pipeHandle->close();
    connected = false;
    std::this_thread::sleep_for(1000ms);
    connect();
}

void BridgeClient::onRecv(const uvw::DataEvent &event) {
    if (!recvBuf.push(event.data.get(), event.length)) {
        Log("recvBuf.push %i failed", event.length);
        disconnect();
        return;
    }

    size_t available;
    while (available = recvBuf.bytes_available()) {
        if (available < 4) return;

        char lenBuf[4];
        recvBuf.peek(lenBuf, 4);
        uint32_t size = LE32_TO_NATIVE(*reinterpret_cast<uint32_t*>(lenBuf));

        if (size > VRBRIDGE_MAX_MESSAGE_SIZE) {
            Log("Message size overflow");
            disconnect();
            return;
        }

        auto unwrappedSize = size - 4;
        if (available < unwrappedSize) return;

        auto messageBuf = std::make_unique<char[]>(size);
        if (!recvBuf.skip(4) || !recvBuf.pop(messageBuf.get(), unwrappedSize)) {
            Log("recvBuf.pop %i failed", size);
            disconnect();
            return;
        }

        messages::ProtobufMessage receivedMessage;
        if (receivedMessage.ParseFromArray(messageBuf.get(), unwrappedSize)) {
            messageCallback(receivedMessage);
        } else {
            Log("receivedMessage.ParseFromArray failed");
            disconnect();
            return;
        }
    }
}

void BridgeClient::sendBridgeMessage(messages::ProtobufMessage &message) {
    if (!isConnected()) return;

    uint32_t size = static_cast<uint32_t>(message.ByteSizeLong());
    uint32_t wrappedSize = size + 4;
    
    auto messageBuf = std::make_unique<char[]>(wrappedSize);
    *reinterpret_cast<uint32_t*>(messageBuf.get()) = NATIVE_TO_LE32(wrappedSize);
    message.SerializeToArray(messageBuf.get() + 4, size);
    if (!sendBuf.push(messageBuf.get(), wrappedSize)) {
        disconnect();
        return;
    }

    writeHandle->send();
}

void BridgeClient::sendWrites() {
    if (!isConnected()) return;
    
    auto available = sendBuf.bytes_available();
    if (!available) return;
    
    auto writeBuf = std::make_unique<char[]>(available);
    sendBuf.pop(writeBuf.get(), available);
    pipeHandle->write(writeBuf.get(), static_cast<unsigned int>(available));
}