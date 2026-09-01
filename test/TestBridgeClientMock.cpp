// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include <catch2/catch_test_macros.hpp>

#include "BridgeServerMock.hpp"
#include "common/TestBridgeClient.hpp"

#include <solarxr_protocol/generated/all_generated.h>

TEST_CASE("IO with a mock server", "[Bridge]") {
    using namespace std::chrono;
    using namespace std::chrono_literals;

    const uint16_t hmd_id = 1;
    int positions = 0;
    int invalid_messages = 0;

    bool last_logged_position = false;

    auto logger = std::static_pointer_cast<Logger>(std::make_shared<ConsoleLogger>("ServerMock"));

    std::shared_ptr<BridgeServerMock> server_mock = std::make_shared<BridgeServerMock>(
        logger,
        [&](BridgeTransport::MessageHeader&& message) {
            std::visit(overloaded{
                           [&](const solarxr_protocol::data_feed::DataFeedMessageHeader*) {
                               invalid_messages++;
                           },
                           [&](const solarxr_protocol::rpc::RpcMessageHeader*) { invalid_messages++; },
                           [&](const solarxr_protocol::driver_protocol::DriverMessageHeader* msg) {
                               using namespace solarxr_protocol::driver_protocol;

                               DriverMessage type = msg->message_type();
                               switch (type) {
                               case DriverMessage::HandshakeRequest: {
                                   logger->Log("Sending HandshakeResponse");

                                   flatbuffers::FlatBufferBuilder fbb;
                                   auto handshake_response_msg = CreateHandshakeResponse(fbb, HandshakeStatus::ACCEPTED);
                                   auto msg_header = CreateDriverMessageHeader(fbb, 0, msg->tx_id(), DriverMessage::HandshakeResponse, handshake_response_msg.Union());
                                   auto bundle = solarxr_protocol::CreateMessageBundle(fbb, 0, 0, fbb.CreateVector({ msg_header }));
                                   fbb.Finish(bundle);
                                   server_mock->SendMessage(fbb);
                                   break;
                               }
                               case DriverMessage::AddTrackerRequest: {
                                   using namespace solarxr_protocol::datatypes;
                                   auto add_msg = msg->message_as_AddTrackerRequest();
                                   auto hardware_identifier = add_msg->hardware_identifier();
                                   auto display_name = add_msg->display_name();
                                   auto manufacturer = add_msg->manufacturer();
                                   BodyPart body_part = add_msg->body_part();
                                   logger->Log("Got AddTrackerRequest with hardware_identifier={} display_name={} manufacturer={} body_part={}",
                                               hardware_identifier ? hardware_identifier->c_str() : "null",
                                               display_name ? display_name->c_str() : "null",
                                               manufacturer ? manufacturer->c_str() : "null",
                                               solarxr_protocol::datatypes::EnumNameBodyPart(body_part));

                                   flatbuffers::FlatBufferBuilder fbb;
                                   auto add_tracker_resp_msg = CreateAddTrackerResponse(fbb,
                                                                                        body_part == BodyPart::HEAD ? AddTrackerStatus::CREATED : AddTrackerStatus::ERROR,
                                                                                        body_part == BodyPart::HEAD ? hmd_id : 0);
                                   auto msg_header = CreateDriverMessageHeader(fbb, 0, msg->tx_id(), DriverMessage::AddTrackerResponse, add_tracker_resp_msg.Union());
                                   auto bundle = solarxr_protocol::CreateMessageBundle(fbb, 0, 0, fbb.CreateVector({ msg_header }));
                                   fbb.Finish(bundle);
                                   server_mock->SendMessage(fbb);
                                   break;
                               }
                               case DriverMessage::UpdateTrackerStatus:
                               case DriverMessage::UpdateTrackerBattery:
                                   // ignore
                                   break;
                               case DriverMessage::UpdateTrackerPosition: {
                                   using namespace solarxr_protocol::datatypes;
                                   auto pos_msg = msg->message_as_UpdateTrackerPosition();
                                   if (pos_msg->tracker_id() != hmd_id)
                                       FAIL("Got UpdateTrackerPosition for non-HMD tracker");

                                   if (!last_logged_position) {
                                       logger->Log("... tracker position update");
                                       last_logged_position = true;
                                   }
                                   positions++;

                                   flatbuffers::FlatBufferBuilder fbb;
                                   math::Quat rot(0.f, 0.f, 0.f, 1.f);
                                   math::Vec3f pos(0.f, 0.f, 0.f);
                                   auto bone = CreateBone(fbb, BodyPart::UPPER_CHEST, &rot, &rot, 1.f, &pos);
                                   auto bones = fbb.CreateVector({ bone });
                                   auto skeleton_update_msg = CreateSkeletonUpdate(fbb, bones);
                                   auto msg_header = CreateDriverMessageHeader(fbb, 0, 0, DriverMessage::SkeletonUpdate, skeleton_update_msg.Union());
                                   auto bundle = solarxr_protocol::CreateMessageBundle(fbb, 0, 0, fbb.CreateVector({ msg_header }));
                                   fbb.Finish(bundle);
                                   server_mock->SendMessage(fbb);
                                   break;
                               }
                               default:
                                   invalid_messages++;
                                   break;
                               }

                               if (type != DriverMessage::UpdateTrackerPosition)
                                   last_logged_position = false;
                           } },
                       message);
        },
        [&] {
            using namespace solarxr_protocol::driver_protocol;
            logger->Log("Sending HandshakeAvailable");

            flatbuffers::FlatBufferBuilder fbb;
            auto handshake_available_msg = CreateHandshakeAvailable(fbb);
            auto msg_header = CreateDriverMessageHeader(fbb, 0, 0, DriverMessage::HandshakeAvailable, handshake_available_msg.Union());
            auto bundle = solarxr_protocol::CreateMessageBundle(fbb, 0, 0, fbb.CreateVector({ msg_header }));
            fbb.Finish(bundle);
            server_mock->SendMessage(fbb);
        });

    server_mock->Start();
    std::this_thread::sleep_for(10ms);
    TestBridgeClient();
    server_mock->Stop();

    if (invalid_messages)
        FAIL("Invalid messages received");
}