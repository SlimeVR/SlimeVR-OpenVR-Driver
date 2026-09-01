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
#include <system_error>

#ifndef _WIN32
#include <sys/fcntl.h>
#endif

using namespace std::chrono_literals;
namespace fs = std::filesystem;

BridgeServerMock::BridgeServerMock(std::shared_ptr<Logger> logger,
                                   std::function<void(MessageHeader&&)> on_message_received,
                                   std::function<void()> on_connect,
                                   std::function<void()> on_disconnect)
    : BridgeTransport(logger, on_message_received, on_connect, on_disconnect) {
    sock_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd_ == InvalidSocket) {
        int err = GetLastSocketError();
        throw std::system_error(err, std::system_category(), "socket() failed");
    }

    struct sockaddr_un addr{
        .sun_family = AF_UNIX,
    };

    const std::filesystem::path path = GetSocketPath();
    const std::u8string path_str = path.u8string();
    if (path_str.size() > std::size(addr.sun_path) - 1) {
        CloseSocket(sock_fd_);
        throw std::runtime_error("Socket path too long to fit in sun_path");
    }
    memcpy(addr.sun_path, path_str.data(), path_str.size() + 1);

    std::error_code ec;
    fs::remove(path, ec);

    int ret = bind(sock_fd_, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    if (ret == SocketError) {
        int err = GetLastSocketError();
        CloseSocket(sock_fd_);
        throw std::system_error(err, std::system_category(), "bind() failed");
    }

    ret = listen(sock_fd_, 5);
    if (ret == SocketError) {
        int err = GetLastSocketError();
        CloseSocket(sock_fd_);
        throw std::system_error(err, std::system_category(), "listen() failed");
    }

    // Set it to non-blocking mode so accept() doesn't block.
#ifdef _WIN32
    {
        u_long mode = 1;
        ret = ioctlsocket(sock_fd_, FIONBIO, &mode);
    }
#else
    ret = fcntl(sock_fd_, F_SETFL, O_NONBLOCK);
#endif

    if (ret == SocketError) {
        int err = GetLastSocketError();
        logger_->Log("Failed to set socket into non-blocking mode: {}", std::error_code(err, std::system_category()).message());
    }

    logger_->Log("Listening on socket {}", path.string());
}

BridgeServerMock::~BridgeServerMock() {
    if (sock_fd_ != InvalidSocket) {
        int ret = CloseSocket(sock_fd_);
        if (ret == SocketError) {
            int err = GetLastSocketError();
            logger_->Log("CloseSocket() failed: {}", std::error_code(err, std::system_category()).message());
        }

        std::error_code ec;
        fs::remove(sock_path_, ec);
    }
}

void BridgeServerMock::CreateConnection() {
    if (sock_fd_ == InvalidSocket)
        return;

    Socket fd = accept(sock_fd_, nullptr, nullptr);
    if (fd == InvalidSocket) {
        int err = GetLastSocketError();
        throw std::system_error(err, std::system_category(), "accept() failed");
    }

    fd_ = fd;
    cv_.notify_all();

    OnConnect();
}
