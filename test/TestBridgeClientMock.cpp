#include <catch2/catch_test_macros.hpp>

#include "common/TestBridgeClient.hpp"
#include "BridgeServerMock.hpp"

TEST_CASE("IO with a mock server", "[Bridge]") {
    using namespace std::chrono;

    std::map<int, std::pair<TrackerRole, const char*>> serials = {
        { 3, { TrackerRole::WAIST, "human://WAIST" } },
        { 4, { TrackerRole::LEFT_FOOT, "human://LEFT_FOOT" } },
        { 5, { TrackerRole::RIGHT_FOOT, "human://RIGHT_FOOT" } },
        { 6, { TrackerRole::LEFT_KNEE, "human://LEFT_KNEE" } },
        { 7, { TrackerRole::RIGHT_KNEE, "human://RIGHT_KNEE" } },
    };

    int positions = 0;
    int invalidMessages = 0;

    bool lastLoggedPosition = false;
    bool trackersSent = false;

    google::protobuf::Arena arena;

    auto logger = std::static_pointer_cast<Logger>(std::make_shared<ConsoleLogger>("ServerMock"));
    
    std::shared_ptr<BridgeServerMock> serverMock;
    serverMock = std::make_shared<BridgeServerMock>(
        logger,
        [&](const messages::ProtobufMessage& message) {
            if (message.has_tracker_added()) {
                TestLogTrackerAdded(logger, message);
            } else if (message.has_tracker_status()) {
                TestLogTrackerStatus(logger, message);
            } else if (message.has_position()) {
                messages::Position pos = message.position();
                if (!lastLoggedPosition) logger->Log("... tracker positions response");
                lastLoggedPosition = true;
                positions++;

                messages::ProtobufMessage* server_message = google::protobuf::Arena::CreateMessage<messages::ProtobufMessage>(&arena);

                if (!trackersSent) {
                    for (int32_t id = 3; id <= 7; id++) {
                        messages::TrackerAdded* tracker_added = google::protobuf::Arena::CreateMessage<messages::TrackerAdded>(&arena);
                        server_message->set_allocated_tracker_added(tracker_added);
                        tracker_added->set_tracker_id(id);
                        tracker_added->set_tracker_role(serials[id].first);
                        tracker_added->set_tracker_serial(serials[id].second);
                        tracker_added->set_tracker_name(serials[id].second);
                        serverMock->SendBridgeMessage(*server_message);

                        messages::TrackerStatus* tracker_status = google::protobuf::Arena::CreateMessage<messages::TrackerStatus>(&arena);
                        server_message->set_allocated_tracker_status(tracker_status);
                        tracker_status->set_tracker_id(id);
                        tracker_status->set_status(messages::TrackerStatus_Status::TrackerStatus_Status_OK);
                        serverMock->SendBridgeMessage(*server_message);
                    }

                    trackersSent = true;
                }
                
                for (int32_t id = 3; id <= 7; id++) {
                    messages::Position* trackerPosition = google::protobuf::Arena::CreateMessage<messages::Position>(&arena);
                    server_message->set_allocated_position(trackerPosition);
                    trackerPosition->set_tracker_id(id);
                    trackerPosition->set_data_source(messages::Position_DataSource_FULL);
                    trackerPosition->set_x(0);
                    trackerPosition->set_y(0);
                    trackerPosition->set_z(0);
                    trackerPosition->set_qx(0);
                    trackerPosition->set_qy(0);
                    trackerPosition->set_qz(0);
                    trackerPosition->set_qw(0);
                    serverMock->SendBridgeMessage(*server_message);
                }
            } else {
                invalidMessages++;
            }

            if (!message.has_position()) {
                lastLoggedPosition = false;
            }
        }
    );

    serverMock->Start();
    std::this_thread::sleep_for(10ms);
    TestBridgeClient();
    serverMock->Stop();

    if (invalidMessages) FAIL("Invalid messages received");
}