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
#include "BridgeServerMock.hpp"

using namespace std::literals::chrono_literals;

void BridgeServerMock::CreateConnection() {
    logger_->Log("listening");

    server_handle_ = GetLoop()->resource<uvw::pipe_handle>(false);
    server_handle_->on<uvw::listen_event>([this](const uvw::listen_event &event, uvw::pipe_handle &) {
        logger_->Log("new client");
        ResetBuffers();

        /* ipc = false -> pipe will be used for handle passing between processes? no */
        connection_handle_ = GetLoop()->resource<uvw::pipe_handle>(false);

        connection_handle_->on<uvw::end_event>([this](const uvw::end_event &, uvw::pipe_handle &) {
            logger_->Log("disconnected");
            StopAsync();
        });
        connection_handle_->on<uvw::data_event>([this](const uvw::data_event &event, uvw::pipe_handle &) {
            OnRecv(event);
        });
        connection_handle_->on<uvw::error_event>([this](const uvw::error_event &event, uvw::pipe_handle &) {
            logger_->Log("pipe error: %s", event.what());
            StopAsync();
        });
        
        server_handle_->accept(*connection_handle_);
        connection_handle_->read();
        logger_->Log("connected");
        connected_ = true;
    });
    server_handle_->on<uvw::error_event>([this](const uvw::error_event &event, uvw::pipe_handle &) {
        logger_->Log("bind %s error: %s", path_.c_str(), event.what());
        StopAsync();
    });

    server_handle_->bind(path_);
    server_handle_->listen();
}

void BridgeServerMock::ResetConnection() {
    CloseConnectionHandles();
}

void BridgeServerMock::CloseConnectionHandles() {
    if (server_handle_) server_handle_->close();
    if (connection_handle_) connection_handle_->close();
    connected_ = false;
}
