#include <catch2/catch_test_macros.hpp>

#include "common/TestBridgeClient.hpp"
#include "BridgeServerMock.hpp"

TEST_CASE("IO with a mock server", "[Bridge]") {
    using namespace std::chrono;

    std::unordered_map<int, std::pair<TrackerRole, const char*>> serials = {
        { 3, { TrackerRole::WAIST, "human://WAIST" } },
        { 4, { TrackerRole::LEFT_FOOT, "human://LEFT_FOOT" } },
        { 5, { TrackerRole::RIGHT_FOOT, "human://RIGHT_FOOT" } },
        { 6, { TrackerRole::LEFT_KNEE, "human://LEFT_KNEE" } },
        { 7, { TrackerRole::RIGHT_KNEE, "human://RIGHT_KNEE" } },
    };

    int positions = 0;
    int invalid_messages = 0;

    bool last_logged_position = false;
    bool trackers_sent = false;

    google::protobuf::Arena arena;

    auto logger = std::static_pointer_cast<Logger>(std::make_shared<ConsoleLogger>("ServerMock"));
    
    std::shared_ptr<BridgeServerMock> server_mock;
    server_mock = std::make_shared<BridgeServerMock>(
        logger,
        [&](const messages::ProtobufMessage& message) {
            if (message.has_tracker_added()) {
                TestLogTrackerAdded(logger, message);
            } else if (message.has_tracker_status()) {
                TestLogTrackerStatus(logger, message);
            } else if (message.has_position()) {
                messages::Position pos = message.position();
                if (!last_logged_position) logger->Log("... tracker positions response");
                last_logged_position = true;
                positions++;

                messages::ProtobufMessage* server_message = google::protobuf::Arena::CreateMessage<messages::ProtobufMessage>(&arena);

                if (!trackers_sent) {
                    for (int32_t id = 3; id <= 7; id++) {
                        messages::TrackerAdded* tracker_added = google::protobuf::Arena::CreateMessage<messages::TrackerAdded>(&arena);
                        server_message->set_allocated_tracker_added(tracker_added);
                        tracker_added->set_tracker_id(id);
                        tracker_added->set_tracker_role(serials[id].first);
                        tracker_added->set_tracker_serial(serials[id].second);
                        tracker_added->set_tracker_name(serials[id].second);
                        server_mock->SendBridgeMessage(*server_message);

                        messages::TrackerStatus* tracker_status = google::protobuf::Arena::CreateMessage<messages::TrackerStatus>(&arena);
                        server_message->set_allocated_tracker_status(tracker_status);
                        tracker_status->set_tracker_id(id);
                        tracker_status->set_status(messages::TrackerStatus_Status::TrackerStatus_Status_OK);
                        server_mock->SendBridgeMessage(*server_message);
                    }

                    trackers_sent = true;
                }
                
                for (int32_t id = 3; id <= 7; id++) {
                    messages::Position* tracker_position = google::protobuf::Arena::CreateMessage<messages::Position>(&arena);
                    server_message->set_allocated_position(tracker_position);
                    tracker_position->set_tracker_id(id);
                    tracker_position->set_data_source(messages::Position_DataSource_FULL);
                    tracker_position->set_x(0);
                    tracker_position->set_y(0);
                    tracker_position->set_z(0);
                    tracker_position->set_qx(0);
                    tracker_position->set_qy(0);
                    tracker_position->set_qz(0);
                    tracker_position->set_qw(0);
                    server_mock->SendBridgeMessage(*server_message);
                }
            } else if(message.has_version()) {
                TestLogVersion(logger, message);
            }
            else {
                invalid_messages++;
            }

            if (!message.has_position()) {
                last_logged_position = false;
            }
        }
    );

    server_mock->Start();
    std::this_thread::sleep_for(10ms);
    TestBridgeClient();
    server_mock->Stop();

    if (invalid_messages) FAIL("Invalid messages received");
}