// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include "BridgeServerMock.hpp"
#include <system_error>

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

    ret = SetNonBlocking(sock_fd_);
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
    assert(sock_fd_ != InvalidSocket);

    Socket fd = accept(sock_fd_, nullptr, nullptr);
    if (fd == InvalidSocket) {
        int err = GetLastSocketError();
        throw std::system_error(err, std::system_category(), "accept() failed");
    }

    fd_ = fd;
}
