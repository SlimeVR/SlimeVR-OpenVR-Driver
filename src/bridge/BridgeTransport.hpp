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

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #define LE32_TO_NATIVE(x) (x)
#else
    #define LE32_TO_NATIVE(x) ( \
        ((uint32_t)(x) << 24) | \
        (((uint32_t)(x) << 8) & 0x00FF0000) | \
        (((uint32_t)(x) >> 8) & 0x0000FF00) | \
        ((uint32_t)(x) >> 24) \
    )
#endif
#define NATIVE_TO_LE32 LE32_TO_NATIVE

#define VRBRIDGE_MAX_MESSAGE_SIZE 1024
#define VRBRIDGE_BUFFERS_SIZE 8192

namespace fs = std::filesystem;

#define WINDOWS_PIPE_NAME "\\\\.\\pipe\\SlimeVRDriver"
#define UNIX_TMP_DIR "/tmp"
#define UNIX_SOCKET_NAME "SlimeVRDriver"

static std::string GetBridgePath() {
#ifdef __linux__
    if (const char* ptr = std::getenv("XDG_RUNTIME_DIR")) {
        const fs::path xdg_runtime = ptr;
        return (xdg_runtime / UNIX_SOCKET_NAME).string();
    } else {
        return (fs::path(UNIX_TMP_DIR) / UNIX_SOCKET_NAME).string();
    }
#else 
    return WINDOWS_PIPE_NAME;
#endif
}

/**
 * @brief Abstract implementation for passing messages between SlimeVR Server and SteamVR Driver using pipes.
 * 
 * Client or Server connection handling is implemented by extending this class.
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
class BridgeTransport {
public:
    BridgeTransport(std::shared_ptr<Logger> logger, std::function<void(const messages::ProtobufMessage&)> on_message_received) :
        logger_(logger),
        message_callback_(on_message_received),
        path_(GetBridgePath()),
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
     * This method queues the message in the send buffer to be sent over the pipe.
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
    void OnRecv(const uvw::DataEvent& event);
    auto GetLoop() {
        return loop_;
    }

    std::shared_ptr<Logger> logger_;
    const std::string path_;
    std::atomic<bool> connected_ = false;
    std::shared_ptr<uvw::PipeHandle> connection_handle_ = nullptr;
    
private:
    void RunThread();
    void SendWrites();

    CircularBuffer send_buf_;
    CircularBuffer recv_buf_;
    std::shared_ptr<uvw::AsyncHandle> stop_signal_handle_ = nullptr;
    std::shared_ptr<uvw::AsyncHandle> write_signal_handle_ = nullptr;
    std::unique_ptr<std::thread> thread_ = nullptr;
    std::shared_ptr<uvw::Loop> loop_ = nullptr;
    const std::function<void(const messages::ProtobufMessage&)> message_callback_;
};