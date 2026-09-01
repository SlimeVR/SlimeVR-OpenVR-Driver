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
#include "bridge/BridgeTransport.hpp"

#include <system_error>

using namespace std::literals::chrono_literals;
namespace fs = std::filesystem;

void BridgeClient::CreateConnection() {
    fs::path path = GetSocketPath();

    Socket fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == InvalidSocket) {
        int err = GetLastSocketError();
        throw std::system_error(err, std::system_category(), "socket() failed");
    }

    logger_->Log("Trying to connect to socket {}", path.string());

    struct sockaddr_un addr{
        .sun_family = AF_UNIX,
    };

    const std::u8string path_str = path.u8string();
    if (path_str.size() > std::size(addr.sun_path) - 1) {
        CloseSocket(fd);
        throw std::runtime_error("Socket path too long to fit in sun_path");
    }
    memcpy(addr.sun_path, path_str.data(), path_str.size() + 1);

    int ret = connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    if (ret == SocketError) {
        int err = GetLastSocketError();
        CloseSocket(fd);
        throw std::system_error(err, std::system_category(), "connect() failed");
    }

    logger_->Log("Connected to {}", path.string());

    // Set it to non-blocking mode so accept() doesn't block.
    ret = SetNonBlocking(fd);
    if (ret == SocketError) {
        int err = GetLastSocketError();
        logger_->Log("Failed to set socket into non-blocking mode: {}", std::error_code(err, std::system_category()).message());
    }

    {
        fd_ = fd;
        cv_.notify_all();
    }

    OnConnect();
}
