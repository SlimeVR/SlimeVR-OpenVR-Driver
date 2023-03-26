#include "TestBridgeClient.hpp"

void testLogTrackerAdded(std::shared_ptr<Logger> logger, const messages::ProtobufMessage& message) {
    if (!message.has_tracker_added()) return;
    messages::TrackerAdded ta = message.tracker_added();
    logger->Log("tracker added id %i name %s role %i serial %s",
        ta.tracker_id(),
        ta.tracker_name().c_str(),
        ta.tracker_role(),
        ta.tracker_serial().c_str()
    );
}

void testLogTrackerStatus(std::shared_ptr<Logger> logger, const messages::ProtobufMessage& message) {
    if (!message.has_tracker_status()) return;
    messages::TrackerStatus status = message.tracker_status();
    if (status.status() == messages::TrackerStatus_Status_OK) {
        logger->Log("tracker status id %i status %s", status.tracker_id(), "OK");
    } else if (status.status() == messages::TrackerStatus_Status_DISCONNECTED) {
        logger->Log("tracker status id %i status %s", status.tracker_id(), "DISCONNECTED");
    } else if (status.status() == messages::TrackerStatus_Status_ERROR) {
        logger->Log("tracker status id %i status %s", status.tracker_id(), "ERROR");
    } else if (status.status() == messages::TrackerStatus_Status_BUSY) {
        logger->Log("tracker status id %i status %s", status.tracker_id(), "BUSY");
    }
}

void testBridgeClient() {
    using namespace std::chrono;

    std::atomic<bool> readyToBench = false;
    std::atomic<steady_clock::time_point> positionRequestedAt = steady_clock::now();
    std::map<int, double> latencyNanosSum;
    std::map<int, int> latencyNanosCount;

    int invalidMessages = 0;
    int trackers = 0;
    int positions = 0;

    bool lastLoggedPosition = false;

    auto logger = std::static_pointer_cast<Logger>(std::make_shared<ConsoleLogger>("Bridge"));
    auto bridge = std::make_shared<BridgeClient>(
        logger,
        [&](const messages::ProtobufMessage& message) {
            if (message.has_tracker_added()) {
                trackers++;
                testLogTrackerAdded(logger, message);
            } else if (message.has_tracker_status()) {
                testLogTrackerStatus(logger, message);
            } else if (message.has_position()) {
                messages::Position pos = message.position();
                if (!lastLoggedPosition) logger->Log("... tracker positions");
                lastLoggedPosition = true;
                positions++;

                if (!readyToBench) return;

                auto id = pos.tracker_id();
                auto dt = duration_cast<nanoseconds>(steady_clock::now() - positionRequestedAt.load());
                latencyNanosCount[id]++;
                latencyNanosSum[id] += dt.count();
            } else {
                invalidMessages++;
            }

            if (!message.has_position()) {
                lastLoggedPosition = false;
            }
        }
    );

    bridge->start();

    for (int i = 0; i < 20; i++) {
        if (bridge->isConnected()) break;
        std::this_thread::sleep_for(milliseconds(100));
    }

    if (!bridge->isConnected()) {
        FAIL("Connection attempt timed out");
        bridge->stop();
        return;
    }

    google::protobuf::Arena arena;
    messages::ProtobufMessage* message = google::protobuf::Arena::CreateMessage<messages::ProtobufMessage>(&arena);

    messages::TrackerAdded* trackerAdded = google::protobuf::Arena::CreateMessage<messages::TrackerAdded>(&arena);
    message->set_allocated_tracker_added(trackerAdded);
    trackerAdded->set_tracker_id(0);
    trackerAdded->set_tracker_role(TrackerRole::HMD);
    trackerAdded->set_tracker_serial("HMD");
    trackerAdded->set_tracker_name("HMD");
    bridge->sendBridgeMessage(*message);

    messages::TrackerStatus* trackerStatus = google::protobuf::Arena::CreateMessage<messages::TrackerStatus>(&arena);
    message->set_allocated_tracker_status(trackerStatus);
    trackerStatus->set_tracker_id(0);
    trackerStatus->set_status(messages::TrackerStatus_Status::TrackerStatus_Status_OK);
    bridge->sendBridgeMessage(*message);

    readyToBench = true;

    for (int i = 0; i < 50; i++) {
        messages::Position* hmdPosition = google::protobuf::Arena::CreateMessage<messages::Position>(&arena);
        message->set_allocated_position(hmdPosition);
        hmdPosition->set_tracker_id(0);
        hmdPosition->set_data_source(messages::Position_DataSource_FULL);
        hmdPosition->set_x(0);
        hmdPosition->set_y(0);
        hmdPosition->set_z(0);
        hmdPosition->set_qx(0);
        hmdPosition->set_qy(0);
        hmdPosition->set_qz(0);
        hmdPosition->set_qw(0);

        positionRequestedAt = steady_clock::now();
        bridge->sendBridgeMessage(*message);
        std::this_thread::sleep_for(milliseconds(10));
    }

    bridge->stop();

    for (const auto& [id, sum] : latencyNanosSum) {
        auto avgLatencyNanos = static_cast<int>(latencyNanosCount[id] ? sum / latencyNanosCount[id] : -1);
        auto avgLatencyMs = duration_cast<duration<double, std::milli>>(nanoseconds(avgLatencyNanos));
        logger->Log("Avg latency for tracker %i: %.3fms", id, avgLatencyMs.count());
    }

    if (invalidMessages) FAIL("Invalid messages received");
    if (!trackers) FAIL("No trackers received");
    if (!positions) FAIL("No tracker positions received");
}
