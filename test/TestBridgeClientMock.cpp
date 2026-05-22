#include <catch2/catch_test_macros.hpp>

#include "BridgeServerMock.hpp"
#include "VRDriver.hpp"
#include "common/TestBridgeClient.hpp"
#include "flatbuffers/flatbuffer_builder.h"
#include "solarxr_protocol/generated/all_generated.h"

using namespace solarxr_protocol;

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

TEST_CASE("IO with a mock server", "[Bridge]") {
    using namespace std::chrono;

    int invalid_messages = 0;
    bool last_logged_position = false;

    auto logger = std::static_pointer_cast<Logger>(std::make_shared<ConsoleLogger>("ServerMock"));

    std::shared_ptr<BridgeServerMock> server_mock = std::make_shared<BridgeServerMock>(
        logger,
        [&](std::variant<const data_feed::DataFeedMessageHeader*, const rpc::RpcMessageHeader*>&& message) {
            flatbuffers::FlatBufferBuilder fbb(1024);

            std::visit(overloaded{
                           [&](const data_feed::DataFeedMessageHeader* data_feed_msg) {
                               using solarxr_protocol::data_feed::DataFeedMessage;
                               switch (data_feed_msg->message_type()) {
                               case DataFeedMessage::StartDataFeed:
                                   break;
                               default:
                                   invalid_messages++;
                                   break;
                               }
                           },
                           [&](const rpc::RpcMessageHeader* rpc_msg) {
                               using solarxr_protocol::rpc::RpcMessage;
                               switch (rpc_msg->message_type()) {
                               case RpcMessage::AddTrackerRequest: {
                                   last_logged_position = false;
                                   auto add_msg = rpc_msg->message_as<rpc::AddTrackerRequest>();
                                   logger->Log("Tracker added with name {} (display name {}) role BodyPart::{}",
                                               flatbuffers::GetString(add_msg->name()),
                                               flatbuffers::GetString(add_msg->display_name()),
                                               datatypes::EnumNameBodyPart(add_msg->role_hint().value_or(datatypes::BodyPart::NONE)));

                                   auto add_response = rpc::CreateAddTrackerResponse(fbb, SlimeVRDriver::TrackerIdT(std::nullopt, 1).create(fbb));
                                   auto rpc_msg_header = rpc::CreateRpcMessageHeader(fbb, rpc_msg->tx_id(), RpcMessage::AddTrackerResponse, add_response.Union());
                                   auto rpc_msgs = fbb.CreateVector({ rpc_msg_header });
                                   auto bundle = CreateMessageBundle(fbb, 0, rpc_msgs, 0);
                                   fbb.Finish(bundle);

                                   server_mock->SendMessage(fbb);
                                   break;
                               }
                               case RpcMessage::UpdateTrackerStatus: {
                                   last_logged_position = false;
                                   auto status_msg = rpc_msg->message_as<rpc::UpdateTrackerStatus>();
                                   logger->Log("Tracker {} changed status to {}", to_string(SlimeVRDriver::TrackerIdT(status_msg->tracker_id())), datatypes::EnumNameTrackerStatus(status_msg->status()));
                                   break;
                               }
                               case RpcMessage::UpdateTrackerPose: {
                                   if (!last_logged_position) {
                                       logger->Log("... tracker positions response");
                                       last_logged_position = true;
                                   }

                                   using solarxr_protocol::datatypes::BodyPart;
                                   constexpr std::array body_parts{
                                       BodyPart::UPPER_CHEST,
                                       BodyPart::LEFT_UPPER_ARM,
                                       BodyPart::RIGHT_UPPER_ARM,
                                       BodyPart::HIP,
                                       BodyPart::LEFT_UPPER_LEG,
                                       BodyPart::RIGHT_UPPER_LEG,
                                       BodyPart::LEFT_FOOT,
                                       BodyPart::RIGHT_FOOT,
                                   };
                                   std::array<flatbuffers::Offset<data_feed::tracker::TrackerData>, std::size(body_parts)> synthetic_trackers{};
                                   datatypes::math::Quat rot{};
                                   datatypes::math::Vec3f pos{};
                                   for (size_t i = 0; i < body_parts.size(); i++) {
                                       synthetic_trackers[i] = data_feed::tracker::CreateTrackerData(fbb,
                                                                                                     0,
                                                                                                     data_feed::tracker::CreateTrackerInfo(fbb, datatypes::hardware_info::ImuType::Other, body_parts[i], nullptr, nullptr, false, true),
                                                                                                     datatypes::TrackerStatus::OK,
                                                                                                     &rot,
                                                                                                     &pos);
                                   }
                                   auto feed_update_msg = data_feed::CreateDataFeedUpdate(fbb, 0, fbb.CreateVector(synthetic_trackers.data(), synthetic_trackers.size()), 0, 0, 0, 0);
                                   auto feed_msg_header = data_feed::CreateDataFeedMessageHeader(fbb, data_feed::DataFeedMessage::DataFeedUpdate, feed_update_msg.Union());
                                   auto msgs = fbb.CreateVector({ feed_msg_header });
                                   auto bundle = CreateMessageBundle(fbb, msgs, 0, 0);
                                   fbb.Finish(bundle);
                                   server_mock->SendMessage(fbb);
                                   break;
                               }
                               default:
                                   last_logged_position = false;
                                   invalid_messages++;
                                   break;
                               }
                           } },
                       message);
        });

    server_mock->Start();
    std::this_thread::sleep_for(10ms);
    TestBridgeClient();
    server_mock->Stop();

    if (invalid_messages)
        FAIL("Invalid messages received");
}