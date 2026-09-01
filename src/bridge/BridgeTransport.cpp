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
#include "Endianness.hpp"

#include <solarxr_protocol/generated/all_generated.h>
#include <system_error>

using namespace std::chrono_literals;
namespace fs = std::filesystem;

#define UNIX_XDG_DATA_HOME_DEFAULT ".local/share/"
#define SLIMEVR_IDENTIFIER "dev.slimevr.SlimeVR"
#define UNIX_DEFAULT_TMP_DIR "/tmp"
#define SOCKET_NAME "SlimeVRRpc"

void BridgeTransport::Stop() {
    logger_->Log("stopping");
    StopAsync();
    thread_ = std::jthread();
    reconnect_thread_ = std::jthread();
}

void BridgeTransport::StopAsync() {
    thread_.request_stop();
    reconnect_thread_.request_stop();
    cv_.notify_all();
}

fs::path BridgeTransport::GetSocketPath() {
#if defined(__linux__)
    std::vector<fs::path> paths = {};
    if (const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR")) {
        paths.push_back(fs::path(xdg_runtime) / SOCKET_NAME);
    }

    if (const char* xdg_data = std::getenv("XDG_DATA_HOME")) {
        paths.push_back(fs::path(xdg_data) / SLIMEVR_IDENTIFIER / SOCKET_NAME);
    }

    if (const char* home = std::getenv("HOME")) {
        paths.push_back(fs::path(home) / UNIX_XDG_DATA_HOME_DEFAULT / SLIMEVR_IDENTIFIER / SOCKET_NAME);
    }

    for (auto path : paths) {
        if (fs::exists(path)) {
            return path;
        }
    }

    return fs::path(UNIX_DEFAULT_TMP_DIR) / SOCKET_NAME;
#elif defined(_WIN32)
    // This should work as long as the user's machine does not have
    // the java.io.tmpdir system property overriden.
    WCHAR tmp_dir[MAX_PATH + 1];
    GetTempPathW(std::size(tmp_dir), tmp_dir);
    return fs::path(tmp_dir) / SOCKET_NAME;
#else
#error "Unsupported platform"
#endif
}

void BridgeTransport::OnRecv(std::span<uint8_t> event) {
    auto bundle = flatbuffers::GetRoot<solarxr_protocol::MessageBundle>(event.data());

    if (auto data_feed_msgs = bundle->data_feed_msgs()) {
        for (auto msg : *data_feed_msgs) {
            // logger_->Log("Got message DataFeedMessage::{}", EnumNameDataFeedMessage(msg->message_type()));
            message_callback_(msg);
        }
    }
    if (auto rpc_msgs = bundle->rpc_msgs()) {
        for (auto msg : *rpc_msgs) {
            logger_->Log("Got message RpcMessage::{}", EnumNameRpcMessage(msg->message_type()));
            message_callback_(msg);
        }
    }
    if (auto driver_msgs = bundle->driver_msgs()) {
        for (auto msg : *driver_msgs) {
            // logger_->Log("Got message DriverMessage::{}", EnumNameDriverMessage(msg->message_type()));
            message_callback_(msg);
        }
    }
}

void BridgeTransport::Start() {
    thread_ = std::jthread([this](std::stop_token stop) { return RunThread(stop); });
}

void BridgeTransport::RunThread(std::stop_token stop) {
    std::vector<uint8_t> data;
    data.reserve(0x10000);

    // Kick off connection.
    ResetConnection();

    while (!stop.stop_requested()) {
        std::shared_lock fd_lock(fd_mutex_);

        if (fd_ == InvalidSocket) [[unlikely]] {
            cv_.wait(fd_lock, stop, [this] { return fd_ != InvalidSocket; });
            if (stop.stop_requested())
                break;
        }

        ptrdiff_t ret = Poll(fd_, 1ms);
        if (ret == SocketError) [[unlikely]] {
            int err = GetLastSocketError();
            logger_->Log("Error when polling socket: {}", std::error_code(err, std::system_category()).message());

            fd_lock.unlock();
            ResetConnection();
            continue;
        } else if (ret == 0) [[likely]] {
            // No data on the socket after timeout
            continue;
        }

        try {
            data.clear();

            uint32_t msg_len;
            ret = ReadFully(fd_, stop, &msg_len, sizeof(msg_len));
            if (ret == SocketError) [[unlikely]] {
                int err = GetLastSocketError();
                throw std::system_error(err, std::system_category(), "recv() for length failed");
            }
            if (ret == 0) [[unlikely]] {
                throw std::runtime_error("EOF");
            }

            msg_len = ConvertEndianness<std::endian::little>(msg_len);
            // If we get something bigger than this, something's probably up
            if (msg_len > 0x40000) [[unlikely]] {
                throw std::runtime_error(std::format("Got too large message ({} bytes)", msg_len));
            }

            const uint32_t unwrapped_len = msg_len - 4;
            data.reserve(unwrapped_len);

            ret = ReadFully(fd_, stop, data.data(), unwrapped_len);
            if (ret == SocketError) [[unlikely]] {
                int err = GetLastSocketError();
                throw std::system_error(err, std::system_category(), "recv() failed");
            }
            if (ret == 0) [[unlikely]] {
                throw std::runtime_error("EOF");
            }

            OnRecv({ data.data(), unwrapped_len });
        } catch (Cancelled&) {
            continue;
        } catch (std::exception& e) {
            logger_->Log("Error on socket: {}", e.what());
            fd_lock.unlock();
            ResetConnection();
        }
    }

    CloseConnectionHandles();
}

void BridgeTransport::ResetConnection() {
    CloseConnectionHandles();

    reconnect_thread_ = std::jthread([this](std::stop_token stop) {
        while (!stop.stop_requested()) {
            try {
                std::unique_lock fd_lock(fd_mutex_);
                CreateConnection();
                assert(fd_ != InvalidSocket);

                cv_.notify_all();

                // connect callback may call a function that needs to block on the fd
                fd_lock.unlock();
                OnConnect();

                return;
            } catch (std::system_error& e) {
                if (last_error_ != e.code()) {
                    logger_->Log("Error when trying to connect: {}", e.what());
                    last_error_ = e.code();
                }
            } catch (std::exception& e) {
                logger_->Log("Error when trying to connect: {}", e.what());
            }

            std::this_thread::sleep_for(1000ms);
        }
    });
}

void BridgeTransport::CloseConnectionHandles() {
    std::unique_lock fd_lock(fd_mutex_);
    if (fd_ == InvalidSocket)
        return;

    CloseSocket(fd_);
    OnDisconnect();
    fd_ = InvalidSocket;
}

void BridgeTransport::SendMessage(const flatbuffers::FlatBufferBuilder& fbb) {
    std::shared_lock fd_lock(fd_mutex_);
    if (fd_ == InvalidSocket) [[unlikely]]
        return;

    std::lock_guard write_lock(write_mutex_);

    if (fbb.GetSize() + 4 > std::numeric_limits<uint32_t>::max()) [[unlikely]] {
        logger_->Log("Skipping send of message (wrapped size larger than 32-bit unsigned integer limit)");
        return;
    }

    const uint32_t size = static_cast<uint32_t>(fbb.GetSize());
    const uint32_t le_wrapped_size = ConvertEndianness<std::endian::little>(size + 4);

#ifdef __linux__
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif

    // Send size
    int ret = WriteFully(fd_, &le_wrapped_size, sizeof(le_wrapped_size));
    if (ret == SocketError) [[unlikely]] {
        int err = GetLastSocketError();
        logger_->Log("send() failed: {}", std::error_code(err, std::system_category()).message());
        return;
    }

    // Send data
    ret = WriteFully(fd_, fbb.GetBufferPointer(), size);
    if (ret == SocketError) [[unlikely]] {
        int err = GetLastSocketError();
        logger_->Log("send() failed: {}", std::error_code(err, std::system_category()).message());
        return;
    }
}
