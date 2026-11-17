// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#pragma once

#include "bridge/BridgeTransport.hpp"

class BridgeServerMock : public BridgeTransport {
public:
    BridgeServerMock(std::shared_ptr<Logger> logger,
                     std::function<void(MessageHeader&&)> on_message_received,
                     std::function<void()> on_connect = {},
                     std::function<void()> on_disconnect = {});

    virtual ~BridgeServerMock();

private:
    void CreateConnection() override;

    std::filesystem::path sock_path_;
    Socket sock_fd_;
};