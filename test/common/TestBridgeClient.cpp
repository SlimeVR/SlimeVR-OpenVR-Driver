#include "TestBridgeClient.hpp"
#include "bridge/BridgeClient.hpp"

#include <atomic>
#include <solarxr_protocol/generated/all_generated.h>

void TestBridgeClient() {
    using namespace std::chrono;
    using namespace std::chrono_literals;

    const uint32_t tx_id = *reinterpret_cast<const uint32_t*>("svr");
    std::atomic_uint16_t hmd_id = 0;
    std::atomic<steady_clock::time_point> position_requested_at = steady_clock::now();
    double latency_nanos_sum;
    int latency_nanos_count;

    int invalid_messages = 0;
    int positions = 0;

    bool last_logged_position = false;

    auto logger = std::static_pointer_cast<Logger>(std::make_shared<ConsoleLogger>("Test"));
    std::shared_ptr<BridgeClient> bridge = std::make_shared<BridgeClient>(
        logger,
        [&](BridgeTransport::MessageHeader&& message) {
            std::visit(overloaded{
                           [&](const solarxr_protocol::data_feed::DataFeedMessageHeader*) {
                               invalid_messages++;
                           },
                           [&](const solarxr_protocol::rpc::RpcMessageHeader*) {
                               // ignore
                           },
                           [&](const solarxr_protocol::driver_protocol::DriverMessageHeader* msg) {
                               using namespace solarxr_protocol::driver_protocol;
                               using namespace solarxr_protocol::datatypes;

                               DriverMessage type = msg->message_type();
                               switch (type) {
                               case DriverMessage::HandshakeAvailable: {
                                   flatbuffers::FlatBufferBuilder fbb;
                                   auto handshake = CreateHandshakeRequest(fbb, fbb.CreateString("TestClient"), solarxr_protocol::datatypes::CreateBoneMask(fbb, true, true, false, true, true));
                                   auto msg_header = CreateDriverMessageHeader(fbb, 0, 0, DriverMessage::HandshakeRequest, handshake.Union());
                                   auto bundle = solarxr_protocol::CreateMessageBundle(fbb, 0, 0, fbb.CreateVector({ msg_header }));
                                   fbb.Finish(bundle);
                                   bridge->SendMessage(fbb);
                                   break;
                               }
                               case DriverMessage::HandshakeResponse: {
                                   if (msg->message_as_HandshakeResponse()->status() != HandshakeStatus::ACCEPTED)
                                       FAIL("Handshake was rejected");

                                   flatbuffers::FlatBufferBuilder fbb;
                                   auto add_tracker_msg = CreateAddTrackerRequest(fbb, fbb.CreateString("HMD"), fbb.CreateString("HMD"), 0, BodyPart::HEAD);
                                   auto msg_header = CreateDriverMessageHeader(fbb, tx_id, 0, DriverMessage::AddTrackerRequest, add_tracker_msg.Union());
                                   auto bundle = solarxr_protocol::CreateMessageBundle(fbb, 0, 0, fbb.CreateVector({ msg_header }));
                                   fbb.Finish(bundle);
                                   bridge->SendMessage(fbb);
                                   break;
                               }
                               case DriverMessage::AddTrackerResponse:
                                   if (msg->reply_to() == tx_id) {
                                       auto resp = msg->message_as_AddTrackerResponse();
                                       AddTrackerStatus status = resp->status();
                                       if (status == AddTrackerStatus::ERROR)
                                           FAIL("Error when adding HMD");

                                       uint16_t id = resp->tracker_id();
                                       flatbuffers::FlatBufferBuilder fbb;
                                       auto status_update = CreateUpdateTrackerStatus(fbb, id, TrackerStatus::OK);
                                       auto msg_header = CreateDriverMessageHeader(fbb, 0, 0, DriverMessage::UpdateTrackerStatus, status_update.Union());
                                       auto bundle = solarxr_protocol::CreateMessageBundle(fbb, 0, 0, fbb.CreateVector({ msg_header }));
                                       fbb.Finish(bundle);
                                       bridge->SendMessage(fbb);

                                       hmd_id.store(id);
                                       hmd_id.notify_all();
                                   }
                                   break;
                               case DriverMessage::UpdateTrackerStatus:
                               case DriverMessage::UpdateTrackerBattery:
                                   // ignore
                                   break;
                               case DriverMessage::SkeletonUpdate: {
                                   if (!last_logged_position) {
                                       logger->Log("... skeleton update");
                                       last_logged_position = true;
                                   }
                                   positions++;

                                   if (hmd_id == 0)
                                       return;
                                   auto dt = duration_cast<nanoseconds>(steady_clock::now() - position_requested_at.load());
                                   latency_nanos_count++;
                                   latency_nanos_sum += dt.count();
                                   break;
                               }
                               default:
                                   invalid_messages++;
                                   break;
                               }

                               if (type != DriverMessage::SkeletonUpdate)
                                   last_logged_position = false;
                           },
                       },
                       message);
        });

    bridge->Start();

    for (int i = 0; i < 20; i++) {
        if (bridge->IsConnected())
            break;
        std::this_thread::sleep_for(100ms);
    }

    if (!bridge->IsConnected()) {
        FAIL("Connection attempt timed out");
        bridge->Stop();
        return;
    }

    hmd_id.wait(0);

    flatbuffers::FlatBufferBuilder fbb;
    for (int i = 0; i < 50; i++) {
        using namespace solarxr_protocol;
        using namespace solarxr_protocol::driver_protocol;

        datatypes::math::Quat rot(0.f, 0.f, 0.f, 1.f);
        datatypes::math::Vec3f pos(0.f, 0.f, 0.f);
        auto update_pos_msg = driver_protocol::CreateUpdateTrackerPosition(fbb, hmd_id, &rot, &pos);
        auto msg_header = CreateDriverMessageHeader(fbb, 0, 0, DriverMessage::UpdateTrackerPosition, update_pos_msg.Union());
        auto bundle = solarxr_protocol::CreateMessageBundle(fbb, 0, 0, fbb.CreateVector({ msg_header }));
        fbb.Finish(bundle);

        position_requested_at = steady_clock::now();
        bridge->SendMessage(fbb);
        fbb.Clear();
        std::this_thread::sleep_for(10ms);
    }

    bridge->Stop();

    auto avg_latency_nanos = static_cast<int>(latency_nanos_count ? latency_nanos_sum / latency_nanos_count : -1);
    auto avg_latency_ms = duration_cast<duration<double, std::milli>>(nanoseconds(avg_latency_nanos));
    logger->Log("avg latency: {:.3f}ms", avg_latency_ms.count());

    if (invalid_messages)
        FAIL("Invalid messages received");
    if (!positions)
        FAIL("No tracker positions received");
}
