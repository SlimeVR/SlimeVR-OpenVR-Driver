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

void BridgeServerMock::createConnection() {
    logger->Log("listening");

    serverHandle = getLoop()->resource<uvw::PipeHandle>(false);
    serverHandle->once<uvw::ListenEvent>([this](const uvw::ListenEvent &event, uvw::PipeHandle &) {
        logger->Log("new client");
        resetBuffers();

        /* ipc = false -> pipe will be used for handle passing between processes? no */
        connectionHandle = getLoop()->resource<uvw::PipeHandle>(false);

        connectionHandle->on<uvw::EndEvent>([this](const uvw::EndEvent &, uvw::PipeHandle &) {
            logger->Log("disconnected");
            stopAsync();
        });
        connectionHandle->on<uvw::DataEvent>([this](const uvw::DataEvent &event, uvw::PipeHandle &) {
            onRecv(event);
        });
        connectionHandle->on<uvw::ErrorEvent>([this](const uvw::ErrorEvent &event, uvw::PipeHandle &) {
            logger->Log("Pipe error: %s", event.what());
            stopAsync();
        });
        
        serverHandle->accept(*connectionHandle);
        connectionHandle->read();
        logger->Log("connected");
        connected = true;
    });
    serverHandle->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &event, uvw::PipeHandle &) {
        logger->Log("Bind '%s' error: %s", path, event.what());
        stopAsync();
    });

    serverHandle->bind(path);
    serverHandle->listen();
}

void BridgeServerMock::resetConnection() {
    closeConnectionHandles();
}

void BridgeServerMock::closeConnectionHandles() {
    if (serverHandle) serverHandle->close();
    if (connectionHandle) connectionHandle->close();
    connected = false;
}
