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

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <span>
#include <thread>
#include <variant>

// Needs to be before anything that includes Windows.h to avoid macro clashes
#include <solarxr_protocol/generated/all_generated.h>

#ifdef _WIN32
#include <Winsock2.h>
#include <afunix.h>
// Microsoft why
#undef ERROR
#undef SendMessage
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include "Logger.hpp"

/**
 * @brief Passes messages between SlimeVR Server and SteamVR Driver using Unix sockets.
 *
 * Client or Server connection handling is implemented by extending this class.
 *
 * This class provides a set of methods to start, stop an IO thread, and send messages over a Unix socket.
 *
 * When a message is received and parsed from the pipe, the on_message_received function passed in the constructor is called
 * from the bridge thread with the message as a parameter.
 *
 * @param logger A shared pointer to an Logger object to log messages from the transport.
 * @param on_message_received A function to be called from event loop thread when a message is received and parsed from the pipe.
 * @param on_connect A function to be called when the socket is connected to.
 * @param on_disconnect A function to be called when the connection is ended.
 */
class BridgeTransport {
public:
    using MessageHeader = std::variant<const solarxr_protocol::data_feed::DataFeedMessageHeader*, const solarxr_protocol::rpc::RpcMessageHeader*, const solarxr_protocol::driver_protocol::DriverMessageHeader*>;

#ifdef _WIN32
    using Socket = SOCKET;
#else
    using Socket = int;
#endif

#ifdef _WIN32
    constexpr static Socket InvalidSocket = INVALID_SOCKET;
#else
    constexpr static Socket InvalidSocket = -1;
#endif

#ifdef _WIN32
    constexpr static int SocketError = SOCKET_ERROR;
#else
    constexpr static int SocketError = -1;
#endif

    static inline int GetLastSocketError() {
#ifdef _WIN32
        return WSAGetLastError();
#else
        return errno;
#endif
    }

    static inline int CloseSocket(Socket fd) {
#ifdef _WIN32
        return closesocket(fd);
#else
        return close(fd);
#endif
    }

    static inline bool IsBlockingError(int err) {
#ifdef _WIN32
        return err == WSAEWOULDBLOCK;
#else
        return err == EAGAIN || err == EWOULDBLOCK;
#endif
    }

    static inline int SetNonBlocking(Socket fd) {
#ifdef _WIN32
        u_long mode = 1;
        return ioctlsocket(fd, FIONBIO, &mode);
#else
        return fcntl(fd, F_SETFL, O_NONBLOCK);
#endif
    }

    template <typename Rep, typename Period>
    static inline bool Poll(Socket fd, std::chrono::duration<Rep, Period> timeout) noexcept(false) {
        struct pollfd pollfd{
            .fd = fd,
            .events = POLLIN
        };

        int ret;

#ifdef _WIN32
        ret = WSAPoll(&pollfd, 1, std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
#else
        {
            auto s = std::chrono::duration_cast<std::chrono::seconds>(timeout);
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - s);
            struct timespec ts{
                .tv_sec = s.count(),
                .tv_nsec = ns.count(),
            };
            ret = ppoll(&pollfd, 1, timeout == timeout.zero() ? nullptr : &ts, nullptr);
        }
#endif

#ifdef _WIN32
        constexpr int InterruptedErrno = WSAEINTR;
#else
        constexpr int InterruptedErrno = EINTR;
#endif

        return ret != SocketError ? (pollfd.revents & (POLLERR | POLLHUP)) == 0 : GetLastSocketError() == InterruptedErrno;
    }

    BridgeTransport(std::shared_ptr<Logger> logger,
                    std::function<void(MessageHeader&&)> on_message_received,
                    std::function<void()> on_connect = {},
                    std::function<void()> on_disconnect = {})
        : logger_(logger)
        , connect_callback_(on_connect)
        , message_callback_(on_message_received)
        , disconnect_callback_(on_disconnect) {
#ifdef _WIN32
        WSADATA _ws_data;
        if (int ret = WSAStartup(MAKEWORD(2, 2), &_ws_data); ret != 0) {
            logger_->Log("WSAStartup failed with code {}", ret);
            return;
        }
#endif
    }

    virtual ~BridgeTransport() {
        Stop();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    /**
     * @brief Starts the channel by creating a thread.
     *
     * Connects and automatic reconnects with a timeout are implemented internally.
     */
    void Start();

    /**
     * @brief Stops the channel by stopping the thread and closing the connection handles.
     *
     * Blocks until the thread is stopped and the connection handles are closed.
     */
    void Stop();

    /**
     * @brief Stops the channel asynchronously and returns immediately.
     */
    void StopAsync();

    /**
     * @brief Sends a message over the channel.
     *
     * @param message The message to send.
     */
    void SendMessage(const flatbuffers::FlatBufferBuilder& fbb);

    /**
     * @brief Checks if the channel is connected.
     *
     * @return true if the channel is connected, false otherwise.
     */
    bool IsConnected() {
        return fd_ != InvalidSocket;
    };

protected:
    virtual void CreateConnection() = 0;
    void ResetConnection();

    void CloseConnectionHandles();

    void OnConnect() {
        if (connect_callback_)
            connect_callback_();
    }
    void OnRecv(std::span<uint8_t> event);
    void OnDisconnect() {
        if (disconnect_callback_)
            disconnect_callback_();
    }

    static std::filesystem::path GetSocketPath();

    std::shared_ptr<Logger> logger_;
    // used for the condition variable
    std::mutex m_;
    // cv is signaled on connection, or when we're exiting, in which case fd_ == InvalidSocket
    std::condition_variable cv_;

    std::atomic<Socket> fd_ = InvalidSocket;

private:
    void RunThread(std::stop_token stop);

    std::function<void()> connect_callback_;
    std::function<void(MessageHeader&&)> message_callback_;
    std::function<void()> disconnect_callback_;

    std::jthread thread_;
    std::jthread reconnect_thread_;
    std::optional<std::error_code> last_error_;
};
