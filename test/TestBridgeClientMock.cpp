#include <catch2/catch_test_macros.hpp>

#include "common/TestBridgeClient.hpp"
#include "BridgeServerMock.hpp"

TEST_CASE("IO with a mock server", "[Bridge]") {
    using namespace std::chrono;

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
                testLogTrackerAdded(logger, message);
            } else if (message.has_tracker_status()) {
                testLogTrackerStatus(logger, message);
            } else if (message.has_position()) {
                messages::Position pos = message.position();
                if (!lastLoggedPosition) logger->Log("... tracker positions response");
                lastLoggedPosition = true;
                positions++;

                messages::ProtobufMessage* serverMessage = google::protobuf::Arena::CreateMessage<messages::ProtobufMessage>(&arena);

                if (!trackersSent) {
                    std::map<int, std::pair<TrackerRole, const char*>> serials = {
                        { 3, {TrackerRole::WAIST, "human://WAIST"} },
                        { 4, {TrackerRole::LEFT_KNEE, "human://LEFT_KNEE"} },
                        { 5, {TrackerRole::RIGHT_KNEE, "human://RIGHT_KNEE"} },
                        { 6, {TrackerRole::LEFT_FOOT, "human://LEFT_FOOT"} },
                        { 7, {TrackerRole::RIGHT_FOOT, "human://RIGHT_FOOT"} },
                    };

                    for (int32_t id = 3; id <= 7; id++) {
                        messages::TrackerAdded* trackerAdded = google::protobuf::Arena::CreateMessage<messages::TrackerAdded>(&arena);
                        serverMessage->set_allocated_tracker_added(trackerAdded);
                        trackerAdded->set_tracker_id(id);
                        trackerAdded->set_tracker_role(serials[id].first);
                        trackerAdded->set_tracker_serial(serials[id].second);
                        trackerAdded->set_tracker_name(serials[id].second);
                        serverMock->sendBridgeMessage(*serverMessage);

                        messages::TrackerStatus* trackerStatus = google::protobuf::Arena::CreateMessage<messages::TrackerStatus>(&arena);
                        serverMessage->set_allocated_tracker_status(trackerStatus);
                        trackerStatus->set_tracker_id(id);
                        trackerStatus->set_status(messages::TrackerStatus_Status::TrackerStatus_Status_OK);
                        serverMock->sendBridgeMessage(*serverMessage);
                    }

                    trackersSent = true;
                }
                
                for (int32_t id = 3; id <= 7; id++) {
                    messages::Position* trackerPosition = google::protobuf::Arena::CreateMessage<messages::Position>(&arena);
                    serverMessage->set_allocated_position(trackerPosition);
                    trackerPosition->set_tracker_id(id);
                    trackerPosition->set_data_source(messages::Position_DataSource_FULL);
                    trackerPosition->set_x(0);
                    trackerPosition->set_y(0);
                    trackerPosition->set_z(0);
                    trackerPosition->set_qx(0);
                    trackerPosition->set_qy(0);
                    trackerPosition->set_qz(0);
                    trackerPosition->set_qw(0);
                    serverMock->sendBridgeMessage(*serverMessage);
                }
            } else {
                invalidMessages++;
            }

            if (!message.has_position()) {
                lastLoggedPosition = false;
            }
        }
    );

    serverMock->start();
    std::this_thread::sleep_for(10ms);
    testBridgeClient();
    serverMock->stop();

    if (invalidMessages) FAIL("Invalid messages received");
}