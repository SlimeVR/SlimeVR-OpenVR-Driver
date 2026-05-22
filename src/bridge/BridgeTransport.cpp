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
#include <bit>

void BridgeTransport::Start() {
    thread_ = std::make_unique<std::thread>(&BridgeTransport::RunThread, this);
}

void BridgeTransport::Stop() {
    if (!thread_ || !thread_->joinable())
        return;
    StopAsync();
    logger_->Log("stopping");
    thread_->join();
    thread_.reset();
}

void BridgeTransport::StopAsync() {
    if (!stop_signal_handle_ || stop_signal_handle_->closing())
        return;
    stop_signal_handle_->send();
}

void BridgeTransport::RunThread() {
    logger_->Log("thread started");
    loop_ = uvw::loop::create();
    stop_signal_handle_ = GetLoop()->resource<uvw::async_handle>();
    write_signal_handle_ = GetLoop()->resource<uvw::async_handle>();

    stop_signal_handle_->on<uvw::async_event>([this](const uvw::async_event&, uvw::async_handle& handle) {
        logger_->Log("closing handles");
        CloseConnectionHandles();
        write_signal_handle_->close();
        stop_signal_handle_->close();
    });

    write_signal_handle_->on<uvw::async_event>([this](const uvw::async_event&, uvw::async_handle& handle) {
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

void BridgeTransport::OnConnect() {
    if (connect_callback_)
        (*connect_callback_)();
}

void BridgeTransport::OnRecv(const uvw::data_event& event) {
    if (!recv_buf_.Push(event.data.get(), event.length)) {
        logger_->Log("recv_buf_.Push({}) failed", event.length);
        ResetConnection();
        return;
    }

    size_t available;
    while ((available = recv_buf_.BytesAvailable())) {
        if (available < 4)
            return;

        char len_buf[4];
        recv_buf_.Peek(len_buf, 4);
        uint32_t msg_len = *reinterpret_cast<uint32_t*>(&len_buf[0]);
        if constexpr (std::endian::native != std::endian::little) {
            msg_len = std::byteswap(msg_len);
        }

        if (available < msg_len)
            return;

        char message_buf[VRBRIDGE_MAX_MESSAGE_SIZE];
        if (msg_len > std::size(message_buf)) {
            logger_->Log(
                "message size overflow: {} > {}",
                msg_len, VRBRIDGE_MAX_MESSAGE_SIZE);
            ResetConnection();
            return;
        }

        auto unwrapped_size = msg_len - 4;
        if (!recv_buf_.Skip(4) || !recv_buf_.Pop(message_buf, unwrapped_size)) {
            logger_->Log("recv_buf_.Pop({}) failed", msg_len - 4);
            ResetConnection();
            return;
        }

        auto bundle = flatbuffers::GetRoot<solarxr_protocol::MessageBundle>(message_buf);

        if (auto data_feed_msgs = bundle->data_feed_msgs()) {
            for (auto msg : *data_feed_msgs) {
#ifndef NDEBUG
                logger_->Log("Got message DataFeedMessage::{}", EnumNameDataFeedMessage(msg->message_type()));
#endif
                message_callback_(msg);
            }
        }
        if (auto rpc_msgs = bundle->rpc_msgs()) {
            for (auto msg : *rpc_msgs) {
#ifndef NDEBUG
                logger_->Log("Got message RpcMessage::{}", EnumNameRpcMessage(msg->message_type()));
#endif
                message_callback_(msg);
            }
        }
    }
}

void BridgeTransport::SendMessage(const flatbuffers::FlatBufferBuilder& fbb) {
    if (!IsConnected())
        return;

    uint32_t size = static_cast<uint32_t>(fbb.GetSize());
    uint32_t wrapped_size = size + 4;

    char message_buf[VRBRIDGE_MAX_MESSAGE_SIZE];
    if (wrapped_size > std::size(message_buf)) {
        logger_->Log("Skipping message send because it's too large ({} > {})", wrapped_size, std::size(message_buf));
        return;
    }

    if constexpr (std::endian::native != std::endian::little) {
        uint32_t le_size = std::byteswap(wrapped_size);
        send_buf_.Push(reinterpret_cast<const char*>(&le_size), sizeof(le_size));
    } else {
        send_buf_.Push(reinterpret_cast<const char*>(&wrapped_size), sizeof(wrapped_size));
    }

    if (!send_buf_.Push(reinterpret_cast<const char*>(fbb.GetBufferPointer()), fbb.GetSize())) {
        ResetConnection();
        return;
    }

    write_signal_handle_->send();
}

void BridgeTransport::SendWrites() {
    if (!IsConnected())
        return;

    auto available = send_buf_.BytesAvailable();
    if (!available)
        return;

    char write_buf[VRBRIDGE_BUFFERS_SIZE];
    send_buf_.Pop(write_buf, available);
    connection_handle_->write(write_buf, static_cast<unsigned int>(available));
}
