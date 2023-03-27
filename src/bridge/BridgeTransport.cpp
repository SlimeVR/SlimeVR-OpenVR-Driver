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

void BridgeTransport::Start() {
    thread_ = std::make_unique<std::thread>(&BridgeTransport::RunThread, this);
}

void BridgeTransport::Stop() {
    if (!thread_ || !thread_->joinable()) return;
    StopAsync();
    logger_->Log("stopping");
    thread_->join();
    thread_.reset();
}

void BridgeTransport::StopAsync() {
    if (!stop_signal_handle_ || stop_signal_handle_->closing()) return;
    stop_signal_handle_->send();
}

void BridgeTransport::RunThread() {
    logger_->Log("thread started");
    loop_ = uvw::Loop::create();
    stop_signal_handle_ = GetLoop()->resource<uvw::AsyncHandle>();
    write_signal_handle_ = GetLoop()->resource<uvw::AsyncHandle>();

    stop_signal_handle_->on<uvw::AsyncEvent>([this](const uvw::AsyncEvent&, uvw::AsyncHandle& handle) {
        logger_->Log("closing handles");
        CloseConnectionHandles();
        write_signal_handle_->close();
        stop_signal_handle_->close();
    });
    
    write_signal_handle_->on<uvw::AsyncEvent>([this](const uvw::AsyncEvent&, uvw::AsyncHandle& handle) {
        SendWrites();
    });
    
    CreateConnection();
    GetLoop()->run();
    GetLoop()->close();
    logger_->Log("thread exited");
}

void BridgeTransport::ResetBuffers() {
    recv_buf_.Clear();
    send_buf_.Clear();
}

void BridgeTransport::OnRecv(const uvw::DataEvent& event) {
    if (!recv_buf_.Push(event.data.get(), event.length)) {
        logger_->Log("recv_buf_.Push %i failed", event.length);
        ResetConnection();
        return;
    }

    size_t available;
    while (available = recv_buf_.BytesAvailable()) {
        if (available < 4) return;

        char len_buf[4];
        recv_buf_.Peek(len_buf, 4);
        uint32_t size = LE32_TO_NATIVE(*reinterpret_cast<uint32_t*>(len_buf));

        if (size > VRBRIDGE_MAX_MESSAGE_SIZE) {
            logger_->Log("message size overflow");
            ResetConnection();
            return;
        }

        auto unwrapped_size = size - 4;
        if (available < unwrapped_size) return;

        auto message_buf = std::make_unique<char[]>(size);
        if (!recv_buf_.Skip(4) || !recv_buf_.Pop(message_buf.get(), unwrapped_size)) {
            logger_->Log("recv_buf_.Pop %i failed", size);
            ResetConnection();
            return;
        }

        messages::ProtobufMessage message;
        if (message.ParseFromArray(message_buf.get(), unwrapped_size)) {
            message_callback_(message);
        } else {
            logger_->Log("receivedMessage.ParseFromArray failed");
            ResetConnection();
            return;
        }
    }
}

void BridgeTransport::SendBridgeMessage(const messages::ProtobufMessage& message) {
    if (!IsConnected()) return;

    uint32_t size = static_cast<uint32_t>(message.ByteSizeLong());
    uint32_t wrapped_size = size + 4;
    
    auto message_buf = std::make_unique<char[]>(wrapped_size);
    *reinterpret_cast<uint32_t*>(message_buf.get()) = NATIVE_TO_LE32(wrapped_size);
    message.SerializeToArray(message_buf.get() + 4, size);
    if (!send_buf_.Push(message_buf.get(), wrapped_size)) {
        ResetConnection();
        return;
    }

    write_signal_handle_->send();
}

void BridgeTransport::SendWrites() {
    if (!IsConnected()) return;
    
    auto available = send_buf_.BytesAvailable();
    if (!available) return;
    
    auto write_buf = std::make_unique<char[]>(available);
    send_buf_.Pop(write_buf.get(), available);
    connection_handle_->write(write_buf.get(), static_cast<unsigned int>(available));
}