#include "TestBridgeClient.hpp"

void TestLogTrackerAdded(std::shared_ptr<Logger> logger, const messages::ProtobufMessage& message) {
    if (!message.has_tracker_added()) return;
    messages::TrackerAdded tracker_added = message.tracker_added();
    logger->Log("tracker added id {} name {} role {} serial {}",
        tracker_added.tracker_id(),
        tracker_added.tracker_name(),
        tracker_added.tracker_role(),
        tracker_added.tracker_serial()
    );
}

void TestLogTrackerStatus(std::shared_ptr<Logger> logger, const messages::ProtobufMessage& message) {
    if (!message.has_tracker_status()) return;
    messages::TrackerStatus status = message.tracker_status();
    static const std::unordered_map<messages::TrackerStatus_Status, std::string> status_map = {
        { messages::TrackerStatus_Status_OK, "OK" },
        { messages::TrackerStatus_Status_DISCONNECTED, "DISCONNECTED" },
        { messages::TrackerStatus_Status_ERROR, "ERROR" },
        { messages::TrackerStatus_Status_BUSY, "BUSY" },
    };
    if (status_map.count(status.status())) {
        logger->Log("tracker status id {} status {}", status.tracker_id(), status_map.at(status.status()));
    }
}

void TestLogVersion(std::shared_ptr<Logger> logger, const messages::ProtobufMessage& message) {
    if (!message.has_version()) return;
    messages::Version version = message.version();
    logger->Log("protocol version {}", version.protocol_version());
}

void TestBridgeClient() {
    using namespace std::chrono;

    std::atomic<bool> ready_to_bench = false;
    std::atomic<steady_clock::time_point> position_requested_at = steady_clock::now();
    std::map<int, double> latency_nanos_sum;
    std::map<int, int> latency_nanos_count;

    int invalid_messages = 0;
    int trackers = 0;
    int positions = 0;

    bool last_logged_position = false;

    auto logger = std::static_pointer_cast<Logger>(std::make_shared<ConsoleLogger>("Test"));
    auto bridge = std::make_shared<BridgeClient>(
        logger,
        [&](const messages::ProtobufMessage& message) {
            if (message.has_tracker_added()) {
                trackers++;
                TestLogTrackerAdded(logger, message);
            } else if (message.has_tracker_status()) {
                TestLogTrackerStatus(logger, message);
            } else if (message.has_battery()) {
                TestLogTrackerStatus(logger, message);
            } else if (message.has_position()) {
                messages::Position pos = message.position();
                if (!last_logged_position) logger->Log("... tracker positions");
                last_logged_position = true;
                positions++;

                if (!ready_to_bench) return;

                auto id = pos.tracker_id();
                auto dt = duration_cast<nanoseconds>(steady_clock::now() - position_requested_at.load());
                latency_nanos_count[id]++;
                latency_nanos_sum[id] += dt.count();
            } else {
                invalid_messages++;
            }

            if (!message.has_position()) {
                last_logged_position = false;
            }
        }
    );

    bridge->Start();

    for (int i = 0; i < 20; i++) {
        if (bridge->IsConnected()) break;
        std::this_thread::sleep_for(milliseconds(100));
    }

    if (!bridge->IsConnected()) {
        FAIL("Connection attempt timed out");
        bridge->Stop();
        return;
    }

    google::protobuf::Arena arena;
    messages::ProtobufMessage* message = google::protobuf::Arena::CreateMessage<messages::ProtobufMessage>(&arena);

    messages::TrackerAdded* tracker_added = google::protobuf::Arena::CreateMessage<messages::TrackerAdded>(&arena);
    message->set_allocated_tracker_added(tracker_added);
    tracker_added->set_tracker_id(0);
    tracker_added->set_tracker_role(TrackerRole::HMD);
    tracker_added->set_tracker_serial("HMD");
    tracker_added->set_tracker_name("HMD");
    bridge->SendBridgeMessage(*message);

    messages::TrackerStatus* tracker_status = google::protobuf::Arena::CreateMessage<messages::TrackerStatus>(&arena);
    message->set_allocated_tracker_status(tracker_status);
    tracker_status->set_tracker_id(0);
    tracker_status->set_status(messages::TrackerStatus_Status::TrackerStatus_Status_OK);
    bridge->SendBridgeMessage(*message);

    ready_to_bench = true;

    for (int i = 0; i < 50; i++) {
        messages::Position* hmd_position = google::protobuf::Arena::CreateMessage<messages::Position>(&arena);
        message->set_allocated_position(hmd_position);
        hmd_position->set_tracker_id(0);
        hmd_position->set_data_source(messages::Position_DataSource_FULL);
        hmd_position->set_x(0);
        hmd_position->set_y(0);
        hmd_position->set_z(0);
        hmd_position->set_qx(0);
        hmd_position->set_qy(0);
        hmd_position->set_qz(0);
        hmd_position->set_qw(0);

        position_requested_at = steady_clock::now();
        bridge->SendBridgeMessage(*message);
        std::this_thread::sleep_for(milliseconds(10));
    }

    bridge->Stop();

    for (const auto& [id, sum] : latency_nanos_sum) {
        auto avg_latency_nanos = static_cast<int>(latency_nanos_count[id] ? sum / latency_nanos_count[id] : -1);
        auto avg_latency_ms = duration_cast<duration<double, std::milli>>(nanoseconds(avg_latency_nanos));
        logger->Log("avg latency for tracker {}: {:.3f}ms", id, avg_latency_ms.count());
    }

    if (invalid_messages) FAIL("Invalid messages received");
    if (!trackers) FAIL("No trackers received");
    if (!positions) FAIL("No tracker positions received");
}
