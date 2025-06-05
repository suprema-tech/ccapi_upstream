#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_EXECUTION_MANAGEMENT_SERVICE_BINANCE_USDS_FUTURES_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_EXECUTION_MANAGEMENT_SERVICE_BINANCE_USDS_FUTURES_H_
#ifdef CCAPI_ENABLE_SERVICE_EXECUTION_MANAGEMENT
#ifdef CCAPI_ENABLE_EXCHANGE_BINANCE_USDS_FUTURES
#include "ccapi_cpp/service/ccapi_execution_management_service_binance_derivatives_base.h"

namespace ccapi {
class ExecutionManagementServiceBinanceUsdsFutures : public ExecutionManagementServiceBinanceDerivativesBase {
 public:
  ExecutionManagementServiceBinanceUsdsFutures(std::function<void(Event&, Queue<Event>*)> eventHandler, SessionOptions sessionOptions,
                                               SessionConfigs sessionConfigs, ServiceContextPtr serviceContextPtr)
      : ExecutionManagementServiceBinanceDerivativesBase(eventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    this->exchangeName = CCAPI_EXCHANGE_NAME_BINANCE_USDS_FUTURES;
    this->baseUrlWs = sessionConfigs.getUrlWebsocketBase().at(this->exchangeName) + "/ws";
    this->baseUrlWsOrderEntry = sessionConfigs.getUrlWebsocketOrderEntryBase().at(this->exchangeName) + CCAPI_BINANCE_USDS_FUTURES_WS_ORDER_ENTRY_PATH;
    this->baseUrlRest = sessionConfigs.getUrlRestBase().at(this->exchangeName);
    this->setHostRestFromUrlRest(this->baseUrlRest);
    this->setHostWsFromUrlWs(this->baseUrlWs);
    this->setHostWsFromUrlWsOrderEntry(this->baseUrlWsOrderEntry);
    this->apiKeyName = CCAPI_BINANCE_USDS_FUTURES_API_KEY;
    this->apiSecretName = CCAPI_BINANCE_USDS_FUTURES_API_SECRET;
    this->apiPrivateKeyPathName = CCAPI_BINANCE_USDS_FUTURES_API_PRIVATE_KEY_PATH;
    this->apiPrivateKeyPasswordName = CCAPI_BINANCE_USDS_FUTURES_API_PRIVATE_KEY_PASSWORD;
    this->setupCredential({this->apiKeyName, this->apiSecretName, this->apiPrivateKeyPathName, this->apiPrivateKeyPasswordName});
    this->createOrderTarget = CCAPI_BINANCE_USDS_FUTURES_CREATE_ORDER_PATH;
    this->cancelOrderTarget = "/fapi/v1/order";
    this->getOrderTarget = "/fapi/v1/order";
    this->getOpenOrdersTarget = "/fapi/v1/openOrders";
    this->cancelOpenOrdersTarget = "/fapi/v1/allOpenOrders";
    this->isDerivatives = true;
    this->listenKeyTarget = CCAPI_BINANCE_USDS_FUTURES_LISTEN_KEY_PATH;
    this->getAccountBalancesTarget = "/fapi/v2/account";
    this->getAccountPositionsTarget = "/fapi/v2/positionRisk";
  }

  virtual ~ExecutionManagementServiceBinanceUsdsFutures() {}

#ifndef CCAPI_EXPOSE_INTERNAL

 protected:
#endif
  std::vector<std::string> createSendStringListFromSubscription(const WsConnection& wsConnection, const Subscription& subscription, const TimePoint& now,
                                                                const std::map<std::string, std::string>& credential) override {
    if (wsConnection.host == CCAPI_BINANCE_USDS_FUTURES_HOST_WS_ORDER_ENTRY) {
      auto it = credential.find(this->apiPrivateKeyPathName);
      if (it == credential.end()) {
        throw std::runtime_error("Missing credential: " + this->apiPrivateKeyPathName);
      }
      rj::Document document;
      document.SetObject();
      rj::Document::AllocatorType& allocator = document.GetAllocator();

      document.AddMember("id", "session.logon", allocator);
      document.AddMember("method", "session.logon", allocator);

      const auto& apiKey = credential.at(this->apiKeyName);
      const auto& timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

      std::map<std::string, std::string> paramsMap{
          {"apiKey", apiKey},
          {"timestamp", std::to_string(timestamp)},
      };

      rj::Value params(rj::kObjectType);
      std::string payload;
      int i = 0;
      for (const auto& [key, value] : paramsMap) {
        if (key == "timestamp") {
          params.AddMember("timestamp", rj::Value().SetInt64(std::stoll(value)), allocator);
        } else {
          params.AddMember("apiKey", rj::Value(value.c_str(), allocator).Move(), allocator);
        }
        payload += key;
        payload += "=";
        payload += value;

        if (i < paramsMap.size() - 1) {
          payload += "&";
        }
        ++i;
      }

      std::string password;
      if (auto it = credential.find(this->apiPrivateKeyPasswordName); it != credential.end()) {
        password = it->second;
      }
      EVP_PKEY* pkey = UtilAlgorithm::loadPrivateKey(it->second, password);
      std::string signature = UtilAlgorithm::signPayload(pkey, payload);
      params.AddMember("signature", rj::Value(signature.c_str(), allocator).Move(), allocator);

      document.AddMember("params", params, allocator);

      rj::StringBuffer buffer;
      rj::Writer<rj::StringBuffer> writer(buffer);
      document.Accept(writer);

      return {buffer.GetString()};

    } else {
      return ExecutionManagementServiceBinanceDerivativesBase::createSendStringListFromSubscription(wsConnection, subscription, now, credential);
    }
  }

  std::string apiPrivateKeyPathName;
  std::string apiPrivateKeyPasswordName;
};
} /* namespace ccapi */
#endif
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_EXECUTION_MANAGEMENT_SERVICE_BINANCE_USDS_FUTURES_H_
