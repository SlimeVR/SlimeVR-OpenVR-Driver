#include "TestBridgeClient.hpp"
#include "VRDriver.hpp"
#include "bridge/BridgeClient.hpp"
#include "solarxr_protocol/generated/all_generated.h"
#include <memory>
using namespace solarxr_protocol;

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

void TestBridgeClient() {
    using namespace std::chrono;

    const datatypes::TransactionId tid(*reinterpret_cast<const uint32_t*>("svr\0"));
    SlimeVRDriver::TrackerIdT hmd_id;

    std::atomic<bool> ready_to_bench = false;
    std::atomic<steady_clock::time_point> position_requested_at = steady_clock::now();
    std::map<datatypes::BodyPart, double> latency_nanos_sum;
    std::map<datatypes::BodyPart, int> latency_nanos_count;

    int invalid_messages = 0;
    int positions = 0;

    bool last_logged_position = false;

    auto logger = std::static_pointer_cast<Logger>(std::make_shared<ConsoleLogger>("Test"));
    std::shared_ptr<BridgeClient> bridge = std::make_shared<BridgeClient>(
        logger,
        [&](const std::variant<const data_feed::DataFeedMessageHeader*, const rpc::RpcMessageHeader*>&& message) {
            std::visit(overloaded{
                           [&](const data_feed::DataFeedMessageHeader* data_feed_msg) {
                               auto* update = data_feed_msg->message_as<data_feed::DataFeedUpdate>();
                               if (!update) {
                                   invalid_messages++;
                                   last_logged_position = false;
                                   return;
                               }

                               if (!last_logged_position) {
                                   logger->Log("... tracker positions");
                                   last_logged_position = true;
                               }
                               positions++;

                               if (!ready_to_bench)
                                   return;

                               for (auto tracker : *update->synthetic_trackers()) {
                                   auto id = tracker->info()->body_part();
                                   auto dt = duration_cast<nanoseconds>(steady_clock::now() - position_requested_at.load());
                                   latency_nanos_count[id]++;
                                   latency_nanos_sum[id] += dt.count();
                               }
                           },
                           [&](const rpc::RpcMessageHeader* rpc_msg) {
                               last_logged_position = false;

                               using solarxr_protocol::rpc::RpcMessage;
                               switch (rpc_msg->message_type()) {
                               case RpcMessage::AddTrackerResponse: {
                                   auto added = rpc_msg->message_as<rpc::AddTrackerResponse>();
                                   auto tx_id = rpc_msg->tx_id();
                                   if (!tx_id) {
                                       logger->Log("Got AddTrackerResponse without tx_id");
                                       invalid_messages++;
                                       return;
                                   }

                                   auto id = added->tracker_id();
                                   if (tx_id->id() == tid.id()) {
                                       hmd_id = SlimeVRDriver::TrackerIdT(id);
                                   }

                                   logger->Log("Added tracker with ID {}", to_string(SlimeVRDriver::TrackerIdT(id)));
                                   ready_to_bench = true;
                                   ready_to_bench.notify_all();
                                   break;
                               }
                               case RpcMessage::TrackingChecklistResponse:
                                   break;
                               default:
                                   invalid_messages++;
                                   break;
                               }
                           },
                       },
                       message);
        },
        [&]() {
            flatbuffers::FlatBufferBuilder fbb(1024);
            std::vector<flatbuffers::Offset<data_feed::DataFeedMessageHeader>> data_feed_msgs{};
            std::vector<flatbuffers::Offset<rpc::RpcMessageHeader>> rpc_msgs{};

            {
                auto name = fbb.CreateString("HMD");
                auto add_msg = rpc::CreateAddTrackerRequest(fbb, name, name, 0, true, false, true, datatypes::BodyPart::HEAD, true);
                auto msg_header = rpc::CreateRpcMessageHeader(fbb, &tid, rpc::RpcMessage::AddTrackerRequest, add_msg.Union());
                rpc_msgs.push_back(msg_header);
            }

            {
                auto tracker_data_mask = data_feed::tracker::CreateTrackerDataMask(fbb,
                                                                                   true,  // info
                                                                                   true,  // status
                                                                                   true,  // rotation
                                                                                   true,  // position
                                                                                   false, // raw_angular_velocity
                                                                                   false, // raw_acceleration
                                                                                   false, // temp
                                                                                   false, // linear_acceleration
                                                                                   false, // rotation_reference_adjusted
                                                                                   false, // rotation_identity_adjusted
                                                                                   false, // tps
                                                                                   false, // raw_magnetic_vector
                                                                                   false  // stay_aligned
                );
                auto device_data_mask = data_feed::device_data::CreateDeviceDataMask(fbb, tracker_data_mask, true);
                auto data_feed_config = data_feed::CreateDataFeedConfig(fbb,
                                                                        uint16_t(1000 / 200), // minimum_time_since_last
                                                                        device_data_mask,
                                                                        tracker_data_mask,    // synthetic_trackers_mask
                                                                        false,                // bone_mask
                                                                        false,                // stay_aligned_pose_mask
                                                                        false                 // server_guards_mask
                );
                auto data_feed_start_msg = data_feed::CreateStartDataFeed(fbb, fbb.CreateVector({ data_feed_config }));
                auto msg_header = data_feed::CreateDataFeedMessageHeader(fbb, data_feed::DataFeedMessage::StartDataFeed, data_feed_start_msg.Union());
                data_feed_msgs.push_back(msg_header);
            }
            auto bundle = CreateMessageBundle(fbb, fbb.CreateVector(data_feed_msgs), fbb.CreateVector(rpc_msgs), 0);
            fbb.Finish(bundle);
            bridge->SendMessage(fbb);
        });

    bridge->Start();

    for (int i = 0; i < 20; i++) {
        if (bridge->IsConnected())
            break;
        std::this_thread::sleep_for(milliseconds(100));
    }

    if (!bridge->IsConnected()) {
        FAIL("Connection attempt timed out");
        bridge->Stop();
        return;
    }

    logger->Log("waiting for response");
    ready_to_bench.wait(false);

    flatbuffers::FlatBufferBuilder fbb(1024);

    {
        auto status_msg = rpc::CreateUpdateTrackerStatus(fbb, hmd_id.create(fbb), datatypes::TrackerStatus::OK);
        auto msg_header = rpc::CreateRpcMessageHeader(fbb, nullptr, rpc::RpcMessage::UpdateTrackerStatus, status_msg.Union());
        auto msgs = fbb.CreateVector({ msg_header });
        auto bundle = CreateMessageBundle(fbb, 0, msgs, 0);
        fbb.Finish(bundle);
        bridge->SendMessage(fbb);
        fbb.Clear();
    }

    for (int i = 0; i < 50; i++) {
        datatypes::math::Quat rot{};
        datatypes::math::Vec3f pos{};
        auto position_msg = rpc::CreateUpdateTrackerPose(fbb, hmd_id.create(fbb), &rot, &pos);
        auto msg_header = rpc::CreateRpcMessageHeader(fbb, nullptr, rpc::RpcMessage::UpdateTrackerPose, position_msg.Union());
        auto msgs = fbb.CreateVector({ msg_header });
        auto bundle = CreateMessageBundle(fbb, 0, msgs, 0);
        fbb.Finish(bundle);

        position_requested_at = steady_clock::now();
        bridge->SendMessage(fbb);
        fbb.Clear();
        std::this_thread::sleep_for(milliseconds(10));
    }

    bridge->Stop();

    for (const auto& [id, sum] : latency_nanos_sum) {
        auto avg_latency_nanos = static_cast<int>(latency_nanos_count[id] ? sum / latency_nanos_count[id] : -1);
        auto avg_latency_ms = duration_cast<duration<double, std::milli>>(nanoseconds(avg_latency_nanos));
        logger->Log("avg latency for tracker {}: {:.3f}ms", EnumNameBodyPart(id), avg_latency_ms.count());
    }

    if (invalid_messages)
        FAIL("Invalid messages received");
    if (!positions)
        FAIL("No tracker positions received");
}
