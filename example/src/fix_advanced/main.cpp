#include "ccapi_cpp/ccapi_session.h"

namespace ccapi {

Logger* Logger::logger = nullptr;  // This line is needed.

class MyEventHandler : public EventHandler {
 public:
  MyEventHandler(const std::string& fixSubscriptionCorrelationId) : fixSubscriptionCorrelationId(fixSubscriptionCorrelationId) {}

  void processEvent(const Event& event, Session* sessionPtr) override {
    if (event.getType() == Event::Type::AUTHORIZATION_STATUS) {
      std::cout << "Received an event of type AUTHORIZATION_STATUS:\n" + event.toPrettyString(2, 2) << std::endl;
      auto message = event.getMessageList().at(0);
      if (message.getType() == Message::Type::AUTHORIZATION_SUCCESS) {
        Request request(Request::Operation::FIX, "binance");
        request.appendFixParam({
            {35, "D"},
            {11, "6d4eb0fb-2229-469f-873e-557dd78ac11e"},
            {55, "BTC-USD"},
            {54, "1"},
            {44, "20000"},
            {38, "0.001"},
            {40, "2"},
            {59, "1"},
        });
        sessionPtr->sendRequestByFix(this->fixSubscriptionCorrelationId, request);
      }
    } else if (event.getType() == Event::Type::FIX) {
      std::cout << "Received an event of type FIX:\n" + event.toPrettyString(2, 2) << std::endl;
    }
  }

 private:
  std::string fixSubscriptionCorrelationId;
};

} /* namespace ccapi */

using ::ccapi::MyEventHandler;
using ::ccapi::Session;
using ::ccapi::SessionConfigs;
using ::ccapi::SessionOptions;
using ::ccapi::Subscription;
using ::ccapi::UtilSystem;

int main(int argc, char** argv) {
  if (UtilSystem::getEnvAsString("BINANCE_FIX_API_KEY").empty()) {
    std::cerr << "Please set environment variable BINANCE_FIX_API_KEY" << std::endl;
    return EXIT_FAILURE;
  }
  if (UtilSystem::getEnvAsString("BINANCE_FIX_API_PRIVATE_KEY_PATH").empty()) {
    std::cerr << "Please set environment variable BINANCE_FIX_API_PRIVATE_KEY_PATH" << std::endl;
    return EXIT_FAILURE;
  }
  SessionOptions sessionOptions;
  sessionOptions.heartbeatFixIntervalMilliseconds = 30000;  // Note the unit is millisecond
  sessionOptions.heartbeatFixTimeoutMilliseconds = 5000;    // Note the unit is millisecond, should be less than heartbeatFixIntervalMilliseconds
  SessionConfigs sessionConfigs;
  std::string fixSubscriptionCorrelationId("any");
  MyEventHandler eventHandler(fixSubscriptionCorrelationId);
  Session session(sessionOptions, sessionConfigs, &eventHandler);
  Subscription subscription("binance", "", "FIX", "8013=S&9406=Y",
                            fixSubscriptionCorrelationId);  // https://developers.binance.com/docs/binance-spot-api-docs/fix-api#logon-a
  session.subscribe(subscription);
  std::this_thread::sleep_for(std::chrono::seconds(10));
  session.stop();
  return EXIT_SUCCESS;
}
