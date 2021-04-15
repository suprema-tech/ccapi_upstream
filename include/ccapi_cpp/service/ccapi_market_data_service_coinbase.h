#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_COINBASE_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_COINBASE_H_
#ifdef CCAPI_ENABLE_SERVICE_MARKET_DATA
#ifdef CCAPI_ENABLE_EXCHANGE_COINBASE
#include "ccapi_cpp/service/ccapi_market_data_service.h"
namespace ccapi {
class MarketDataServiceCoinbase CCAPI_FINAL : public MarketDataService {
 public:
  MarketDataServiceCoinbase(std::function<void(Event& event)> wsEventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                            std::shared_ptr<ServiceContext> serviceContextPtr)
      : MarketDataService(wsEventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    this->exchangeName = CCAPI_EXCHANGE_NAME_COINBASE;
    this->baseUrl = sessionConfigs.getUrlWebsocketBase().at(this->exchangeName);
    this->baseUrlRest = this->sessionConfigs.getUrlRestBase().at(this->exchangeName);
    this->setHostFromUrl(this->baseUrlRest);
    this->getRecentTradesTarget = "/products/<product-id>/trades";
  }

 private:
  std::vector<std::string> createRequestStringList(const WsConnection& wsConnection) override {
    std::vector<std::string> requestStringList;
    rj::Document document;
    document.SetObject();
    rj::Document::AllocatorType& allocator = document.GetAllocator();
    document.AddMember("type", rj::Value("subscribe").Move(), allocator);
    rj::Value channels(rj::kArrayType);
    for (const auto& subscriptionListByChannelIdSymbolId : this->subscriptionListByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id)) {
      auto channelId = subscriptionListByChannelIdSymbolId.first;
      rj::Value channel(rj::kObjectType);
      rj::Value symbolIds(rj::kArrayType);
      for (const auto& subscriptionListBySymbolId : subscriptionListByChannelIdSymbolId.second) {
        std::string symbolId = subscriptionListBySymbolId.first;
        symbolIds.PushBack(rj::Value(symbolId.c_str(), allocator).Move(), allocator);
        std::string exchangeSubscriptionId = channelId + "|" + symbolId;
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_CHANNEL_ID] = channelId;
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_SYMBOL_ID] = symbolId;
      }
      channel.AddMember("name", rj::Value(channelId.c_str(), allocator).Move(), allocator);
      channel.AddMember("product_ids", symbolIds, allocator);
      channels.PushBack(channel, allocator);
      rj::Value heartbeatChannel(rj::kObjectType);
      heartbeatChannel.AddMember("name", rj::Value("heartbeat").Move(), allocator);
      rj::Value heartbeatSymbolIds(rj::kArrayType);
      for (const auto& subscriptionListBySymbolId : subscriptionListByChannelIdSymbolId.second) {
        heartbeatSymbolIds.PushBack(rj::Value(subscriptionListBySymbolId.first.c_str(), allocator).Move(), allocator);
      }
      heartbeatChannel.AddMember("product_ids", heartbeatSymbolIds, allocator);
      channels.PushBack(heartbeatChannel, allocator);
    }
    document.AddMember("channels", channels, allocator);
    rj::StringBuffer stringBuffer;
    rj::Writer<rj::StringBuffer> writer(stringBuffer);
    document.Accept(writer);
    std::string requestString = stringBuffer.GetString();
    requestStringList.push_back(requestString);
    return requestStringList;
  }
  std::vector<MarketDataMessage> processTextMessage(WsConnection& wsConnection, wspp::connection_hdl hdl, const std::string& textMessage,
                                                    const TimePoint& timeReceived) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    rj::Document document;
    document.Parse(textMessage.c_str());
    std::vector<MarketDataMessage> marketDataMessageList;
    auto type = std::string(document["type"].GetString());
    CCAPI_LOGGER_TRACE("type = " + type);
    if (type == "l2update") {
      auto symbolId = std::string(document["product_id"].GetString());
      auto exchangeSubscriptionId = std::string(CCAPI_WEBSOCKET_COINBASE_CHANNEL_LEVEL2) + "|" + symbolId;
      MarketDataMessage marketDataMessage;
      marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
      marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
      marketDataMessage.tp = UtilTime::parse(std::string(document["time"].GetString()));
      marketDataMessage.recapType = MarketDataMessage::RecapType::NONE;
      const rj::Value& changes = document["changes"];
      for (auto& change : changes.GetArray()) {
        auto side = std::string(change[0].GetString());
        MarketDataMessage::TypeForDataPoint dataPoint;
        dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(change[1].GetString())});
        dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(change[2].GetString())});
        if (side == "buy") {
          marketDataMessage.data[MarketDataMessage::DataType::BID].push_back(std::move(dataPoint));
        } else {
          marketDataMessage.data[MarketDataMessage::DataType::ASK].push_back(std::move(dataPoint));
        }
      }
      marketDataMessageList.push_back(std::move(marketDataMessage));
    } else if (type == "match") {
      auto symbolId = std::string(document["product_id"].GetString());
      auto exchangeSubscriptionId = std::string(CCAPI_WEBSOCKET_COINBASE_CHANNEL_MATCH) + "|" + symbolId;
      MarketDataMessage marketDataMessage;
      marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
      marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
      marketDataMessage.tp = UtilTime::parse(std::string(document["time"].GetString()));
      marketDataMessage.recapType = MarketDataMessage::RecapType::NONE;
      MarketDataMessage::TypeForDataPoint dataPoint;
      dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(std::string(document["price"].GetString()))});
      dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(std::string(document["size"].GetString()))});
      dataPoint.insert({MarketDataMessage::DataFieldType::TRADE_ID, std::to_string(document["trade_id"].GetInt64())});
      dataPoint.insert({MarketDataMessage::DataFieldType::IS_BUYER_MAKER, std::string(document["side"].GetString()) == "buy" ? "1" : "0"});
      marketDataMessage.data[MarketDataMessage::DataType::TRADE].push_back(std::move(dataPoint));
      marketDataMessageList.push_back(std::move(marketDataMessage));
    } else if (type == "heartbeat") {
      // TODO(cryptochassis): implement
    } else if (type == "snapshot") {
      auto symbolId = std::string(document["product_id"].GetString());
      auto exchangeSubscriptionId = std::string(CCAPI_WEBSOCKET_COINBASE_CHANNEL_LEVEL2) + "|" + symbolId;
      MarketDataMessage marketDataMessage;
      marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
      marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
      marketDataMessage.recapType = MarketDataMessage::RecapType::SOLICITED;
      marketDataMessage.tp = timeReceived;
      const rj::Value& bids = document["bids"];
      for (auto& x : bids.GetArray()) {
        MarketDataMessage::TypeForDataPoint dataPoint;
        dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(x[0].GetString())});
        dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(x[1].GetString())});
        marketDataMessage.data[MarketDataMessage::DataType::BID].push_back(std::move(dataPoint));
      }
      const rj::Value& asks = document["asks"];
      for (auto& x : asks.GetArray()) {
        MarketDataMessage::TypeForDataPoint dataPoint;
        dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(x[0].GetString())});
        dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(x[1].GetString())});
        marketDataMessage.data[MarketDataMessage::DataType::ASK].push_back(std::move(dataPoint));
      }
      marketDataMessageList.push_back(std::move(marketDataMessage));
    } else if (type == "subscriptions") {
      // TODO(cryptochassis): implement
    } else if (type == "error") {
      // TODO(cryptochassis): implement
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
    return marketDataMessageList;
  }
  void convertReq(http::request<http::string_body>& req, const Request& request, const Request::Operation operation, const TimePoint& now,
                  const std::string& symbolId, const std::map<std::string, std::string>& credential) override {
    switch (operation) {
      case Request::Operation::GET_RECENT_TRADES: {
        req.method(http::verb::get);
        auto target = this->getRecentTradesTarget;
        this->substituteParam(target, {{"<product-id>", symbolId}});
        std::string queryString;
        const std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        this->appendParam(queryString, param, {{CCAPI_LIMIT, "limit"}});
        req.target(target + "?" + queryString);
      } break;
      default:
        CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
    }
  }
  std::vector<MarketDataMessage> convertTextMessageToMarketDataMessage(const Request& request, const std::string& textMessage,
                                                                       const TimePoint& timeReceived) override {
    rj::Document document;
    document.Parse(textMessage.c_str());
    std::vector<MarketDataMessage> marketDataMessageList;
    auto operation = request.getOperation();
    switch (operation) {
      case Request::Operation::GET_RECENT_TRADES: {
        for (const auto& x : document.GetArray()) {
          MarketDataMessage marketDataMessage;
          marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
          marketDataMessage.tp = UtilTime::parse(std::string(x["time"].GetString()));
          MarketDataMessage::TypeForDataPoint dataPoint;
          dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(std::string(x["price"].GetString()))});
          dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(std::string(x["size"].GetString()))});
          dataPoint.insert({MarketDataMessage::DataFieldType::TRADE_ID, std::to_string(x["trade_id"].GetInt64())});
          dataPoint.insert({MarketDataMessage::DataFieldType::IS_BUYER_MAKER, std::string(x["side"].GetString()) == "buy" ? "1" : "0"});
          marketDataMessage.data[MarketDataMessage::DataType::TRADE].push_back(std::move(dataPoint));
          marketDataMessageList.push_back(std::move(marketDataMessage));
        }
      } break;
      default:
        CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
    }
    return marketDataMessageList;
  }
};
} /* namespace ccapi */
#endif
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_COINBASE_H_
