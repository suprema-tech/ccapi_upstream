#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_FIX_SERVICE_BINANCE_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_FIX_SERVICE_BINANCE_H_
#ifdef CCAPI_ENABLE_SERVICE_FIX
#ifdef CCAPI_ENABLE_EXCHANGE_BINANCE
#include "ccapi_cpp/ccapi_hmac.h"
#include "ccapi_cpp/service/ccapi_fix_service.h"

namespace ccapi {

class FixServiceBinance : public FixService {
 public:
  FixServiceBinance(std::function<void(Event&, Queue<Event>*)> eventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                    ServiceContextPtr serviceContextPtr)
      : FixService(eventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    this->exchangeName = CCAPI_EXCHANGE_NAME_BINANCE;
    this->baseUrlFix = this->sessionConfigs.getUrlFixBase().at(this->exchangeName);
    this->baseUrlFixMarketData = sessionConfigs.getUrlFixMarketDataBase().at(this->exchangeName);
    this->setHostFixFromUrlFix(this->hostFix, this->portFix, this->baseUrlFix);
    this->setHostFixFromUrlFix(this->hostFixMarketData, this->portFixMarketData, this->baseUrlFixMarketData);
    this->fixApiKeyName = CCAPI_BINANCE_FIX_API_KEY;
    this->fixApiPrivateKeyPathName = CCAPI_BINANCE_FIX_API_PRIVATE_KEY_PATH;
    this->fixApiPrivateKeyPasswordName = CCAPI_BINANCE_FIX_API_PRIVATE_KEY_PASSWORD;
    this->setupCredential({this->fixApiKeyName, this->fixApiPrivateKeyPathName, this->fixApiPrivateKeyPasswordName});
    this->protocolVersion = CCAPI_FIX_PROTOCOL_VERSION_BINANCE;
    this->targetCompID = "SPOT";
  }

  virtual ~FixServiceBinance() {}
#ifndef CCAPI_EXPOSE_INTERNAL

 protected:
#endif
  virtual std::vector<std::pair<int, std::string>> createCommonParam(std::shared_ptr<FixConnection> fixConnectionPtr, const std::string& nowFixTimeStr) {
    return {
        // {hff::tag::SenderCompID, mapGetWithDefault(this->credentialByConnectionIdMap[connectionId], this->apiKeyName)},
        {hff::tag::TargetCompID, this->targetCompID},
        {hff::tag::MsgSeqNum, std::to_string(++this->fixMsgSeqNumByConnectionIdMap[fixConnectionPtr->id])},
        {hff::tag::SendingTime, nowFixTimeStr},
    };
  }

  virtual std::vector<std::pair<int, std::string>> createLogonParam(std::shared_ptr<FixConnection> fixConnectionPtr, const std::string& nowFixTimeStr,
                                                                    const std::map<int, std::string> logonOptionMap = {}) {
    std::vector<std::pair<int, std::string>> param;
    auto msgType = "A";
    param.push_back({hff::tag::MsgType, msgType});
    param.push_back({hff::tag::EncryptMethod, "0"});
    param.push_back({hff::tag::HeartBtInt, std::to_string(this->sessionOptions.heartbeatFixIntervalMilliseconds / 1000)});
    // auto credential = this->credentialByConnectionIdMap[connectionId];
    // auto apiPassphrase = mapGetWithDefault(credential, this->apiPassphraseName);
    // param.push_back({hff::tag::Password, apiPassphrase});
    // auto msgSeqNum = std::to_string(1);
    // auto senderCompID = mapGetWithDefault(credential, this->apiKeyName);
    // auto targetCompID = this->targetCompID;
    // std::vector<std::string> prehashFieldList{nowFixTimeStr, msgType, msgSeqNum, senderCompID, targetCompID, apiPassphrase};
    // auto prehashStr = UtilString::join(prehashFieldList, "\x01");
    // auto apiSecret = mapGetWithDefault(credential, this->apiSecretName);
    // auto rawData = UtilAlgorithm::base64Encode(Hmac::hmac(Hmac::ShaVersion::SHA256, UtilAlgorithm::base64Decode(apiSecret), prehashStr));
    // param.push_back({hff::tag::RawData, rawData});
    // for (const auto& x : logonOptionMap) {
    //   param.push_back({x.first, x.second});
    // }
    return param;
  }
};

} /* namespace ccapi */
#endif
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_FIX_SERVICE_BINANCE_H_
