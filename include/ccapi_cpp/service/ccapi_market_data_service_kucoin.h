#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_KUCOIN_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_KUCOIN_H_
#ifdef CCAPI_ENABLE_SERVICE_MARKET_DATA
#if defined(CCAPI_ENABLE_EXCHANGE_KUCOIN)
#include "ccapi_cpp/service/ccapi_market_data_service.h"
namespace ccapi {
class MarketDataServiceKucoin CCAPI_FINAL : public MarketDataService {
 public:
  MarketDataServiceKucoin(std::function<void(Event& event)> wsEventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                          std::shared_ptr<ServiceContext> serviceContextPtr)
      : MarketDataService(wsEventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    this->exchangeName = CCAPI_EXCHANGE_NAME_KUCOIN;
    this->baseUrl = sessionConfigs.getUrlWebsocketBase().at(this->exchangeName);
    this->baseUrlRest = this->sessionConfigs.getUrlRestBase().at(this->exchangeName);
    this->setHostFromUrl(this->baseUrlRest);
    this->getRecentTradesTarget = "/api/v1/market/histories";
  }
  virtual ~MarketDataServiceKucoin() {}

 private:
  void pingOnApplicationLevel(wspp::connection_hdl hdl, ErrorCode& ec) override {
    auto now = UtilTime::now();
    this->send(hdl, "{\"id\":\"" + std::to_string(UtilTime::getUnixTimestamp(now)) + "\",\"type\":\"ping\"}", wspp::frame::opcode::text, ec);
  }
  void onOpen(wspp::connection_hdl hdl) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    MarketDataService::onOpen(hdl);
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    this->pingIntervalMilliSecondsByMethodMap[PingPongMethod::WEBSOCKET_APPLICATION_LEVEL] =
        std::stol(this->extraPropertyByConnectionIdMap.at(wsConnection.id).at("pingInterval"));
    this->pongTimeoutMilliSecondsByMethodMap[PingPongMethod::WEBSOCKET_APPLICATION_LEVEL] =
        std::stol(this->extraPropertyByConnectionIdMap.at(wsConnection.id).at("pingTimeout"));
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  std::vector<std::string> createRequestStringList(const WsConnection& wsConnection) override {
    std::vector<std::string> requestStringList;
    std::map<std::string, std::vector<std::string>> symbolListByTopicMap;
    for (const auto& subscriptionListByChannelIdSymbolId : this->subscriptionListByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id)) {
      auto channelId = subscriptionListByChannelIdSymbolId.first;
      for (const auto& subscriptionListByInstrument : subscriptionListByChannelIdSymbolId.second) {
        auto symbolId = subscriptionListByInstrument.first;
        auto exchangeSubscriptionId = channelId + ":" + symbolId;
        if (channelId == CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_TICKER || channelId == CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_LEVEL2DEPTH5 ||
            channelId == CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_LEVEL2DEPTH50) {
          this->l2UpdateIsReplaceByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = true;
        }
        symbolListByTopicMap[channelId].push_back(symbolId);
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_CHANNEL_ID] = channelId;
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_SYMBOL_ID] = symbolId;
        CCAPI_LOGGER_TRACE("this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap = " +
                           toString(this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap));
      }
    }
    for (const auto& x : symbolListByTopicMap) {
      auto topic = x.first;
      auto symbolList = x.second;
      rj::Document document;
      document.SetObject();
      rj::Document::AllocatorType& allocator = document.GetAllocator();
      std::string requestId = std::to_string(this->nextRequestIdByConnectionIdMap[wsConnection.id]);
      this->nextRequestIdByConnectionIdMap[wsConnection.id] += 1;
      document.AddMember("id", rj::Value(requestId.c_str(), allocator).Move(), allocator);
      document.AddMember("type", rj::Value("subscribe").Move(), allocator);
      document.AddMember("privateChannel", false, allocator);
      document.AddMember("response", true, allocator);
      document.AddMember("topic", rj::Value((topic + ":" + UtilString::join(symbolList, ",")).c_str(), allocator).Move(), allocator);
      rj::StringBuffer stringBuffer;
      rj::Writer<rj::StringBuffer> writer(stringBuffer);
      document.Accept(writer);
      std::string requestString = stringBuffer.GetString();
      requestStringList.push_back(requestString);
    }
    return requestStringList;
  }
  void onClose(wspp::connection_hdl hdl) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    this->nextRequestIdByConnectionIdMap.erase(wsConnection.id);
    MarketDataService::onClose(hdl);
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  std::vector<MarketDataMessage> processTextMessage(WsConnection& wsConnection, wspp::connection_hdl hdl, const std::string& textMessage,
                                                    const TimePoint& timeReceived) override {
    rj::Document document;
    document.Parse(textMessage.c_str());
    std::vector<MarketDataMessage> marketDataMessageList;
    if (document.IsObject() && std::string(document["type"].GetString()) == "pong") {
      auto now = UtilTime::now();
      WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
      this->lastPongTpByMethodByConnectionIdMap[wsConnection.id][PingPongMethod::WEBSOCKET_APPLICATION_LEVEL] = now;
    } else if (document.IsObject() && std::string(document["type"].GetString()) == "message" &&
               std::string(document["subject"].GetString()) == "trade.ticker") {
      MarketDataMessage marketDataMessage;
      std::string exchangeSubscriptionId = document["topic"].GetString();
      std::string channelId = this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_CHANNEL_ID];
      std::string symbolId = this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_SYMBOL_ID];
      const rj::Value& data = document["data"];
      marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
      marketDataMessage.recapType = this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]
                                        ? MarketDataMessage::RecapType::NONE
                                        : MarketDataMessage::RecapType::SOLICITED;
      marketDataMessage.tp = TimePoint(std::chrono::milliseconds(data["time"].GetInt64()));
      marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
      {
        MarketDataMessage::TypeForDataPoint dataPoint;
        dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(data["bestBid"].GetString())});
        dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(data["bestBidSize"].GetString())});
        marketDataMessage.data[MarketDataMessage::DataType::BID].push_back(std::move(dataPoint));
      }
      {
        MarketDataMessage::TypeForDataPoint dataPoint;
        dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(data["bestAsk"].GetString())});
        dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(data["bestAskSize"].GetString())});
        marketDataMessage.data[MarketDataMessage::DataType::ASK].push_back(std::move(dataPoint));
      }
      marketDataMessageList.push_back(std::move(marketDataMessage));
    } else if (document.IsObject() && std::string(document["type"].GetString()) == "message" && std::string(document["subject"].GetString()) == "level2") {
      MarketDataMessage marketDataMessage;
      std::string exchangeSubscriptionId = document["topic"].GetString();
      std::string channelId = this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_CHANNEL_ID];
      std::string symbolId = this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_SYMBOL_ID];
      auto optionMap = this->optionMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId];
      const rj::Value& data = document["data"];
      marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
      marketDataMessage.recapType = this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]
                                        ? MarketDataMessage::RecapType::NONE
                                        : MarketDataMessage::RecapType::SOLICITED;
      marketDataMessage.tp = TimePoint(std::chrono::milliseconds(data["timestamp"].GetInt64()));
      marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
      int maxMarketDepth = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
      const std::map<MarketDataMessage::DataType, const char*> bidAsk{{MarketDataMessage::DataType::BID, "bids"}, {MarketDataMessage::DataType::ASK, "asks"}};
      for (const auto& x : bidAsk) {
        int index = 0;
        for (const auto& y : data[x.second].GetArray()) {
          if (index >= maxMarketDepth) {
            break;
          }
          MarketDataMessage::TypeForDataPoint dataPoint;
          dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(y[0].GetString())});
          dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(y[1].GetString())});
          marketDataMessage.data[x.first].push_back(std::move(dataPoint));
          ++index;
        }
      }
      marketDataMessageList.push_back(std::move(marketDataMessage));
    } else if (document.IsObject() && std::string(document["type"].GetString()) == "message" &&
               std::string(document["subject"].GetString()) == "trade.l3match") {
      const rj::Value& data = document["data"];
      MarketDataMessage marketDataMessage;
      marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
      std::string exchangeSubscriptionId = document["topic"].GetString();
      marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
      std::string timeStr = data["time"].GetString();
      marketDataMessage.tp =
          UtilTime::makeTimePoint(std::make_pair(std::stoll(timeStr.substr(0, timeStr.length() - 9)), std::stoll(timeStr.substr(timeStr.length() - 9))));
      marketDataMessage.recapType = MarketDataMessage::RecapType::NONE;
      MarketDataMessage::TypeForDataPoint dataPoint;
      dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(std::string(data["price"].GetString()))});
      dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(std::string(data["size"].GetString()))});
      dataPoint.insert({MarketDataMessage::DataFieldType::TRADE_ID, std::string(data["sequence"].GetString())});
      dataPoint.insert({MarketDataMessage::DataFieldType::IS_BUYER_MAKER, std::string(data["side"].GetString()) == "sell" ? "1" : "0"});
      marketDataMessage.data[MarketDataMessage::DataType::TRADE].push_back(std::move(dataPoint));
      marketDataMessageList.push_back(std::move(marketDataMessage));
    }
    return marketDataMessageList;
  }
  void convertReq(http::request<http::string_body>& req, const Request& request, const Request::Operation operation, const TimePoint& now,
                  const std::string& symbolId, const std::map<std::string, std::string>& credential) override {
    switch (operation) {
      case Request::Operation::GET_RECENT_TRADES: {
        req.method(http::verb::get);
        auto target = this->getRecentTradesTarget;
        std::string queryString;
        const std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        this->appendSymbolId(queryString, symbolId, "symbol");
        req.target(target + "?" + queryString);
      } break;
      default:
        CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
    }
  }
  void processSuccessfulTextMessage(const Request& request, const std::string& textMessage, const TimePoint& timeReceived) override {
    const std::string& quotedTextMessage = std::regex_replace(textMessage, std::regex("(\\[|,|\":)\\s?(-?\\d+\\.?\\d*)"), "$1\"$2\"");
    CCAPI_LOGGER_TRACE("quotedTextMessage = " + quotedTextMessage);
    MarketDataService::processSuccessfulTextMessage(request, quotedTextMessage, timeReceived);
  }
  std::vector<MarketDataMessage> convertTextMessageToMarketDataMessage(const Request& request, const std::string& textMessage,
                                                                       const TimePoint& timeReceived) override {
    rj::Document document;
    document.Parse(textMessage.c_str());
    std::vector<MarketDataMessage> marketDataMessageList;
    auto operation = request.getOperation();
    switch (operation) {
      case Request::Operation::GET_RECENT_TRADES: {
        for (const auto& x : document["data"].GetArray()) {
          MarketDataMessage marketDataMessage;
          marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
          std::string timeStr = x["time"].GetString();
          marketDataMessage.tp =
              UtilTime::makeTimePoint(std::make_pair(std::stoll(timeStr.substr(0, timeStr.length() - 9)), std::stoll(timeStr.substr(timeStr.length() - 9))));
          MarketDataMessage::TypeForDataPoint dataPoint;
          dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(std::string(x["price"].GetString()))});
          dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(std::string(x["size"].GetString()))});
          dataPoint.insert({MarketDataMessage::DataFieldType::TRADE_ID, std::string(x["sequence"].GetString())});
          dataPoint.insert({MarketDataMessage::DataFieldType::IS_BUYER_MAKER, std::string(x["side"].GetString()) == "sell" ? "1" : "0"});
          marketDataMessage.data[MarketDataMessage::DataType::TRADE].push_back(std::move(dataPoint));
          marketDataMessageList.push_back(std::move(marketDataMessage));
        }
      } break;
      default:
        CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
    }
    return marketDataMessageList;
  }
  std::map<std::string, int> nextRequestIdByConnectionIdMap;
};
} /* namespace ccapi */
#endif
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_KUCOIN_H_
