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

#include <filesystem>
#include <uvw.hpp>
#include <thread>
#include <stdint.h>

#include "Logger.hpp"
#include "CircularBuffer.hpp"
#include "ProtobufMessages.pb.h"

#define VRBRIDGE_MAX_MESSAGE_SIZE 1024
#define VRBRIDGE_BUFFERS_SIZE 8192

namespace fs = std::filesystem;

#define WINDOWS_PIPE_NAME "\\\\.\\pipe\\SlimeVRDriver"
#define UNIX_XDG_DATA_DIR_DEFAULT ".local/share/"
#define UNIX_SLIMEVR_DIR "slimevr"
#define UNIX_TMP_DIR "/tmp"
#define UNIX_SOCKET_NAME "SlimeVRDriver"

/**
 * @brief Passes messages between SlimeVR Server and SteamVR Driver using pipes or unix sockets.
 * 
 * Client or Server connection handling is implemented by extending this class.
 * 
 * This class provides a set of methods to start, stop an IO thread, send messages over a named pipe or unix socket
 * and is abstracted through `libuv`.
 * 
 * When a message is received and parsed from the pipe, the messageCallback function passed in the constructor is called
 * from the libuv event loop thread with the message as a parameter.
 * 
 * @param logger A shared pointer to an Logger object to log messages from the transport.
 * @param on_message_received A function to be called from event loop thread when a message is received and parsed from the pipe.
 */
class BridgeTransport {
public:
    BridgeTransport(std::shared_ptr<Logger> logger, std::function<void(const messages::ProtobufMessage&)> on_message_received) :
        logger_(logger),
        message_callback_(on_message_received),
        send_buf_(VRBRIDGE_BUFFERS_SIZE),
        recv_buf_(VRBRIDGE_BUFFERS_SIZE)
        { }

    ~BridgeTransport() {
        Stop();
    }

    /**
     * @brief Starts the channel by creating a thread with an libuv event loop.
     * 
     * Connects and automatic reconnects with a timeout are implemented internally.
     */
    void Start();
    
    /**
     * @brief Stops the channel by stopping the libuv event loop and closing the connection handles.
     * 
     * Blocks until the event loop is stopped and the connection handles are closed.
     */
    void Stop();

    /**
     * @brief Stops the channel asynchronously by sending a signal to the libuv event loop to stop and returning immediately.
     * 
     * The `Stop()` function calls this method.
     */
    void StopAsync();

    /**
     * @brief Sends a message over the channel.
     * 
     * Queues the message to the send buffer to be sent over the pipe.
     * 
     * @param message The message to send.
     */
    void SendBridgeMessage(const messages::ProtobufMessage& message);

    /**
     * @brief Checks if the channel is connected.
     * 
     * @return true if the channel is connected, false otherwise.
     */
    bool IsConnected() {
        return connected_;
    };

protected:
    virtual void CreateConnection() = 0;
    virtual void ResetConnection() = 0;
    virtual void CloseConnectionHandles() = 0;
    void ResetBuffers();
    void OnRecv(const uvw::data_event& event);
    auto GetLoop() {
        return loop_;
    }

    static std::string GetBridgePath() {
#ifdef __linux__
        std::vector<std::string> paths = { };
        if (const char* ptr = std::getenv("XDG_RUNTIME_DIR")) {
            const fs::path xdg_runtime = ptr;
            paths.push_back((xdg_runtime / UNIX_SOCKET_NAME).string());
        }

        if (const char* ptr = std::getenv("XDG_DATA_DIR")) {
            const fs::path xdg_data = ptr;
            paths.push_back((xdg_data / UNIX_SLIMEVR_DIR / UNIX_SOCKET_NAME).string());
        }

        if (const char* ptr = std::getenv("HOME")) {
            const fs::path home = ptr;
            paths.push_back((home / UNIX_XDG_DATA_DIR_DEFAULT / UNIX_SLIMEVR_DIR / UNIX_SOCKET_NAME).string());
        }

        for (auto path : paths) {
            if (fs::exists(path)) {
                return path;
            }
        }

        return (fs::path(UNIX_TMP_DIR) / UNIX_SOCKET_NAME).string();
#else
        return WINDOWS_PIPE_NAME;
#endif
    }

    std::shared_ptr<Logger> logger_;
    std::atomic<bool> connected_ = false;
    std::shared_ptr<uvw::pipe_handle> connection_handle_ = nullptr;
    
private:
    void RunThread();
    void SendWrites();

    CircularBuffer send_buf_;
    CircularBuffer recv_buf_;
    std::shared_ptr<uvw::async_handle> stop_signal_handle_ = nullptr;
    std::shared_ptr<uvw::async_handle> write_signal_handle_ = nullptr;
    std::unique_ptr<std::thread> thread_ = nullptr;
    std::shared_ptr<uvw::loop> loop_ = nullptr;
    const std::function<void(const messages::ProtobufMessage&)> message_callback_;
};