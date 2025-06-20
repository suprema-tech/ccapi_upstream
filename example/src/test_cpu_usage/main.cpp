#include "ccapi_cpp/ccapi_session.h"

namespace ccapi {

class MyLogger final : public Logger {
 public:
  void logMessage(const std::string& severity, const std::string& threadId, const std::string& timeISO, const std::string& fileName,
                  const std::string& lineNumber, const std::string& message) override {
    std::lock_guard<std::mutex> lock(m);
    std::cout << threadId << ": [" << timeISO << "] {" << fileName << ":" << lineNumber << "} " << severity << std::string(8, ' ') << message << std::endl;
  }

 private:
  std::mutex m;
};

MyLogger myLogger;
Logger* Logger::logger = &myLogger;

class MyEventHandler : public EventHandler {
 public:
  MyEventHandler(size_t subscriptionDataEventPrintCount) : subscriptionDataEventPrintCount(subscriptionDataEventPrintCount) {}

  void processEvent(const Event& event, Session* sessionPtr) override {
    const auto& eventType = event.getType();
    if (eventType == Event::Type::SESSION_STATUS || eventType == Event::Type::SUBSCRIPTION_STATUS) {
      std::cout << "Received an event:\n" + event.toStringPretty(2, 2) << std::endl;
    } else if (eventType == Event::Type::SUBSCRIPTION_DATA) {
      ++subscriptionDataEventCount;
      if (subscriptionDataEventCount % this->subscriptionDataEventPrintCount == 0) {
        std::cout << "Received " << subscriptionDataEventCount << " SUBSCRIPTION_DATA events." << std::endl;
      }
    }
  }

 private:
  size_t subscriptionDataEventPrintCount{};
  size_t subscriptionDataEventCount{};
};

} /* namespace ccapi */

using ::ccapi::Event;
using ::ccapi::MyEventHandler;
using ::ccapi::Queue;
using ::ccapi::Request;
using ::ccapi::Session;
using ::ccapi::SessionConfigs;
using ::ccapi::SessionOptions;
using ::ccapi::Subscription;
using ::ccapi::UtilSystem;

int main(int argc, char** argv) {
  const auto& numSymbols = UtilSystem::getEnvAsInt("NUM_SYMBOLS", 25);
  bool subscribeMarketDepth = UtilSystem::getEnvAsBool("SUBSCRIBE_MARKET_DEPTH");
  bool subscribeMarketDepth50 = UtilSystem::getEnvAsBool("SUBSCRIBE_MARKET_DEPTH_50");
  bool subscribeMarketDepth400 = UtilSystem::getEnvAsBool("SUBSCRIBE_MARKET_DEPTH_400");
  bool subscribeTrade = UtilSystem::getEnvAsBool("SUBSCRIBE_TRADE");
  size_t subscriptionDataEventPrintCount = UtilSystem::getEnvAsInt("SUBSCRIPTION_DATA_EVENT_PRINT_COUNT", 10000);
  SessionOptions sessionOptions;
  SessionConfigs sessionConfigs;
  MyEventHandler eventHandler(subscriptionDataEventPrintCount);
  Session session(sessionOptions, sessionConfigs, &eventHandler);
  std::string exchange = "okx";
  Request request(Request::Operation::GET_INSTRUMENTS, exchange);
  request.appendParam({
      {"INSTRUMENT_TYPE", "SPOT"},
  });
  Queue<Event> eventQueue;
  session.sendRequest(request, &eventQueue);
  std::vector<Event> eventList = eventQueue.purge();
  const auto& message = eventList.front().getMessageList().front();
  std::vector<Subscription> subscriptionList;
  int i = 0;
  for (const auto& element : message.getElementList()) {
    if (i >= numSymbols) {
      break;
    }
    const auto& instrument = element.getValue("INSTRUMENT");
    if (subscribeMarketDepth) {
      Subscription subscription(exchange, instrument, "MARKET_DEPTH");
      subscriptionList.push_back(subscription);
    }
    if (subscribeMarketDepth50) {
      Subscription subscription(exchange, instrument, "MARKET_DEPTH", "MARKET_DEPTH_MAX=50");
      subscriptionList.push_back(subscription);
    }
    if (subscribeMarketDepth400) {
      Subscription subscription(exchange, instrument, "MARKET_DEPTH", "MARKET_DEPTH_MAX=400");
      subscriptionList.push_back(subscription);
    }
    if (subscribeTrade) {
      Subscription subscription(exchange, instrument, "TRADE");
      subscriptionList.push_back(subscription);
    }
    ++i;
  }
  session.subscribe(subscriptionList);
  std::this_thread::sleep_for(std::chrono::seconds(UtilSystem::getEnvAsInt("STOP_TIME_IN_SECONDS", INT_MAX)));
  session.stop();
  std::cout << "Bye" << std::endl;
  return EXIT_SUCCESS;
}
