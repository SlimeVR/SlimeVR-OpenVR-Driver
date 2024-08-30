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
#pragma once

#include <optional>
#include <uvw.hpp>
#include <stdint.h>

#include "BridgeTransport.hpp"

/**
 * @brief Client implementation for communication with SlimeVR Server using pipes or unix sockets.
 * 
 * This class provides a set of methods to start, stop an IO thread, send messages over a named pipe or unix socket
 * and is abstracted through `libuv`.
 * 
 * When a message is received and parsed from the pipe, the messageCallback function passed in the constructor is called
 * from the event loop thread with the message as a parameter.
 * 
 * @param logger A shared pointer to an Logger object to log messages from the transport.
 * @param on_message_received A function to be called from event loop thread when a message is received and parsed from the pipe.
 */
class BridgeClient: public BridgeTransport {
public:
    using BridgeTransport::BridgeTransport;

private:
    void CreateConnection() override;
    void ResetConnection() override;
    void CloseConnectionHandles() override;
    void Reconnect();

    std::optional<std::string> last_error_;
    std::shared_ptr<uvw::timer_handle> reconnect_timeout_;
};