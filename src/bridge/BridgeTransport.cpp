/*
    SlimeVR Code is placed under the MIT license
    Copyright (c) 2022 SlimeVR Contributors

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
#include "BridgeTransport.hpp"

void BridgeTransport::start() {
    thread = std::make_unique<std::thread>(&BridgeTransport::runThread, this);
}

void BridgeTransport::stop() {
    if (!thread || !thread->joinable()) return;
    stopAsync();
    logger->Log("stopping");
    thread->join();
    thread.reset();
}

void BridgeTransport::stopAsync() {
    if (!stopSignalHandle || stopSignalHandle->closing()) return;
    stopSignalHandle->send();
}

void BridgeTransport::runThread() {
    logger->Log("thread started");
    loop = uvw::Loop::create();
    stopSignalHandle = getLoop()->resource<uvw::AsyncHandle>();
    writeSignalHandle = getLoop()->resource<uvw::AsyncHandle>();

    stopSignalHandle->on<uvw::AsyncEvent>([this](const uvw::AsyncEvent&, uvw::AsyncHandle& handle) {
        logger->Log("closing handles");
        closeConnectionHandles();
        writeSignalHandle->close();
        stopSignalHandle->close();
    });
    
    writeSignalHandle->on<uvw::AsyncEvent>([this](const uvw::AsyncEvent&, uvw::AsyncHandle& handle) {
        sendWrites();
    });
    
    createConnection();
    getLoop()->run();
    getLoop()->close();
    logger->Log("thread exited");
}

void BridgeTransport::resetBuffers() {
    recvBuf.clear();
    sendBuf.clear();
}

void BridgeTransport::onRecv(const uvw::DataEvent& event) {
    if (!recvBuf.push(event.data.get(), event.length)) {
        logger->Log("recvBuf.push %i failed", event.length);
        resetConnection();
        return;
    }

    size_t available;
    while (available = recvBuf.bytes_available()) {
        if (available < 4) return;

        char lenBuf[4];
        recvBuf.peek(lenBuf, 4);
        uint32_t size = LE32_TO_NATIVE(*reinterpret_cast<uint32_t*>(lenBuf));

        if (size > VRBRIDGE_MAX_MESSAGE_SIZE) {
            logger->Log("message size overflow");
            resetConnection();
            return;
        }

        auto unwrappedSize = size - 4;
        if (available < unwrappedSize) return;

        auto messageBuf = std::make_unique<char[]>(size);
        if (!recvBuf.skip(4) || !recvBuf.pop(messageBuf.get(), unwrappedSize)) {
            logger->Log("recvBuf.pop %i failed", size);
            resetConnection();
            return;
        }

        messages::ProtobufMessage receivedMessage;
        if (receivedMessage.ParseFromArray(messageBuf.get(), unwrappedSize)) {
            messageCallback(receivedMessage);
        } else {
            logger->Log("receivedMessage.ParseFromArray failed");
            resetConnection();
            return;
        }
    }
}

void BridgeTransport::sendBridgeMessage(const messages::ProtobufMessage& message) {
    if (!isConnected()) return;

    uint32_t size = static_cast<uint32_t>(message.ByteSizeLong());
    uint32_t wrappedSize = size + 4;
    
    auto messageBuf = std::make_unique<char[]>(wrappedSize);
    *reinterpret_cast<uint32_t*>(messageBuf.get()) = NATIVE_TO_LE32(wrappedSize);
    message.SerializeToArray(messageBuf.get() + 4, size);
    if (!sendBuf.push(messageBuf.get(), wrappedSize)) {
        resetConnection();
        return;
    }

    writeSignalHandle->send();
}

void BridgeTransport::sendWrites() {
    if (!isConnected()) return;
    
    auto available = sendBuf.bytes_available();
    if (!available) return;
    
    auto writeBuf = std::make_unique<char[]>(available);
    sendBuf.pop(writeBuf.get(), available);
    connectionHandle->write(writeBuf.get(), static_cast<unsigned int>(available));
}