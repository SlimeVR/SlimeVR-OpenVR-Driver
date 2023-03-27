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
#include "BridgeClient.hpp"

using namespace std::literals::chrono_literals;

void BridgeClient::CreateConnection() {
    ResetBuffers();

    if (!last_error_.has_value()) {
        logger_->Log("connecting");
    }

    /* ipc = false -> pipe will be used for handle passing between processes? no */
    connection_handle_ = GetLoop()->resource<uvw::PipeHandle>(false);
    connection_handle_->on<uvw::ConnectEvent>([this](const uvw::ConnectEvent&, uvw::PipeHandle&) {
        connection_handle_->read();
        logger_->Log("connected");
        connected_ = true;
        last_error_ = std::nullopt;
    });
    connection_handle_->on<uvw::EndEvent>([this](const uvw::EndEvent&, uvw::PipeHandle&) {
        logger_->Log("disconnected");
        Reconnect();
    });
    connection_handle_->on<uvw::DataEvent>([this](const uvw::DataEvent& event, uvw::PipeHandle&) {
        OnRecv(event);
    });
    connection_handle_->on<uvw::ErrorEvent>([this](const uvw::ErrorEvent& event, uvw::PipeHandle&) {
        if (!last_error_.has_value() || last_error_ != event.what()) {
            logger_->Log("Pipe error: %s", event.what());
            last_error_ = event.what();
        }
        Reconnect();
    });

    connection_handle_->connect(path_);
}

void BridgeClient::ResetConnection() {
    Reconnect();
}

void BridgeClient::Reconnect() {
    CloseConnectionHandles();
    reconnect_timeout_ = GetLoop()->resource<uvw::TimerHandle>();
    reconnect_timeout_->start(1000ms, 0ms);
    reconnect_timeout_->once<uvw::TimerEvent>([this](const uvw::TimerEvent&, uvw::TimerHandle& handle) {
        CreateConnection();
        handle.close();
    });
}

void BridgeClient::CloseConnectionHandles() {
    if (connection_handle_) connection_handle_->close();
    if (reconnect_timeout_) reconnect_timeout_->close();
    connected_ = false;
}
