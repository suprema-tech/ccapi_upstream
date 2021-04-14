#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_OKEX_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_OKEX_H_
#ifdef CCAPI_ENABLE_SERVICE_MARKET_DATA
#ifdef CCAPI_ENABLE_EXCHANGE_OKEX
#include "ccapi_cpp/service/ccapi_market_data_service.h"
namespace ccapi {
class MarketDataServiceOkex CCAPI_FINAL : public MarketDataService {
 public:
  MarketDataServiceOkex(std::function<void(Event& event)> wsEventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                        std::shared_ptr<ServiceContext> serviceContextPtr)
      : MarketDataService(wsEventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    this->exchangeName = CCAPI_EXCHANGE_NAME_OKEX;
    this->baseUrl = sessionConfigs.getUrlWebsocketBase().at(this->exchangeName) + "/public";
    ErrorCode ec = this->inflater.init(false);
    if (ec) {
      CCAPI_LOGGER_FATAL(ec.message());
    }
    this->baseUrlRest = this->sessionConfigs.getUrlRestBase().at(this->exchangeName);
    this->setHostFromUrl(this->baseUrlRest);
    this->getRecentTradesTarget = "/api/v5/market/trades";
  }

 private:
  void pingOnApplicationLevel(wspp::connection_hdl hdl, ErrorCode& ec) override { this->send(hdl, "ping", wspp::frame::opcode::text, ec); }
  std::vector<std::string> createRequestStringList(const WsConnection& wsConnection) override {
    std::vector<std::string> requestStringList;
    rj::Document document;
    document.SetObject();
    rj::Document::AllocatorType& allocator = document.GetAllocator();
    document.AddMember("op", rj::Value("subscribe").Move(), allocator);
    rj::Value args(rj::kArrayType);
    for (const auto& subscriptionListByChannelIdSymbolId : this->subscriptionListByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id)) {
      auto channelId = subscriptionListByChannelIdSymbolId.first;
      for (const auto& subscriptionListBySymbolId : subscriptionListByChannelIdSymbolId.second) {
        std::string symbolId = subscriptionListBySymbolId.first;
        if (channelId == CCAPI_WEBSOCKET_OKEX_CHANNEL_PUBLIC_DEPTH5) {
          this->l2UpdateIsReplaceByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = true;
        }
        std::string exchangeSubscriptionId = UtilString::split(channelId, "?").at(0) + ":" + symbolId;
        rj::Value arg(rj::kObjectType);
        arg.AddMember("channel", rj::Value(channelId.c_str(), allocator).Move(), allocator);
        arg.AddMember("instId", rj::Value(symbolId.c_str(), allocator).Move(), allocator);
        args.PushBack(arg, allocator);
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_CHANNEL_ID] = channelId;
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_SYMBOL_ID] = symbolId;
      }
    }
    document.AddMember("args", args, allocator);
    rj::StringBuffer stringBuffer;
    rj::Writer<rj::StringBuffer> writer(stringBuffer);
    document.Accept(writer);
    std::string requestString = stringBuffer.GetString();
    requestStringList.push_back(requestString);
    return requestStringList;
  }
  std::vector<MarketDataMessage> processTextMessage(WsConnection& wsConnection, wspp::connection_hdl hdl, const std::string& textMessage,
                                                    const TimePoint& timeReceived) override {
    // WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    std::vector<MarketDataMessage> marketDataMessageList;
    if (textMessage.at(0) == '{') {
      rj::Document document;
      document.Parse(textMessage.c_str());
      if (document.IsObject() && document.HasMember("arg")) {
        const rj::Value& arg = document["arg"];
        std::string channelId = arg["channel"].GetString();
        std::string symbolId = arg["instId"].GetString();
        std::string exchangeSubscriptionId = channelId + ":" + symbolId;
        if (channelId == CCAPI_WEBSOCKET_OKEX_CHANNEL_PUBLIC_DEPTH5 || channelId == CCAPI_WEBSOCKET_OKEX_CHANNEL_PUBLIC_DEPTH400) {
          std::string action = channelId == CCAPI_WEBSOCKET_OKEX_CHANNEL_PUBLIC_DEPTH5 ? "" : document["action"].GetString();
          CCAPI_LOGGER_TRACE("action = " + toString(action));
          for (const auto& datum : document["data"].GetArray()) {
            MarketDataMessage marketDataMessage;
            marketDataMessage.tp = TimePoint(std::chrono::milliseconds(std::stoll(datum["ts"].GetString())));
            CCAPI_LOGGER_TRACE("marketDataMessage.tp = " + toString(marketDataMessage.tp));
            marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
            marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
            if (channelId == CCAPI_WEBSOCKET_OKEX_CHANNEL_PUBLIC_DEPTH5) {
              if (this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
                marketDataMessage.recapType = MarketDataMessage::RecapType::NONE;
              } else {
                marketDataMessage.recapType = MarketDataMessage::RecapType::SOLICITED;
              }
            } else {
              marketDataMessage.recapType = action == "update" ? MarketDataMessage::RecapType::NONE : MarketDataMessage::RecapType::SOLICITED;
            }
            for (const auto& x : datum["bids"].GetArray()) {
              MarketDataMessage::TypeForDataPoint dataPoint;
              dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(x[0].GetString())});
              dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(x[1].GetString())});
              marketDataMessage.data[MarketDataMessage::DataType::BID].push_back(std::move(dataPoint));
            }
            for (const auto& x : datum["asks"].GetArray()) {
              MarketDataMessage::TypeForDataPoint dataPoint;
              dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(x[0].GetString())});
              dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(x[1].GetString())});
              marketDataMessage.data[MarketDataMessage::DataType::ASK].push_back(std::move(dataPoint));
            }
            marketDataMessageList.push_back(std::move(marketDataMessage));
          }
        } else if (channelId == CCAPI_WEBSOCKET_OKEX_CHANNEL_TRADE) {
          for (const auto& datum : document["data"].GetArray()) {
            MarketDataMessage marketDataMessage;
            marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
            marketDataMessage.recapType = MarketDataMessage::RecapType::NONE;
            marketDataMessage.tp = TimePoint(std::chrono::milliseconds(std::stoll(datum["ts"].GetString())));
            marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
            MarketDataMessage::TypeForDataPoint dataPoint;
            dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(datum["px"].GetString())});
            dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(datum["sz"].GetString())});
            dataPoint.insert({MarketDataMessage::DataFieldType::TRADE_ID, datum["tradeId"].GetString()});
            dataPoint.insert({MarketDataMessage::DataFieldType::IS_BUYER_MAKER, std::string(datum["side"].GetString()) == "sell" ? "1" : "0"});
            marketDataMessage.data[MarketDataMessage::DataType::TRADE].push_back(std::move(dataPoint));
            marketDataMessageList.push_back(std::move(marketDataMessage));
          }
        }
      }
    } else {
      CCAPI_LOGGER_DEBUG("textMessage = " + textMessage);
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
        this->appendParam(queryString, param, {{CCAPI_LIMIT, "limit"}});
        this->appendSymbolId(queryString, symbolId, "instId");
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
        for (const auto& datum : document["data"].GetArray()) {
          MarketDataMessage marketDataMessage;
          marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
          marketDataMessage.tp = TimePoint(std::chrono::milliseconds(std::stoll(datum["ts"].GetString())));
          MarketDataMessage::TypeForDataPoint dataPoint;
          dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(datum["px"].GetString())});
          dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(datum["sz"].GetString())});
          dataPoint.insert({MarketDataMessage::DataFieldType::TRADE_ID, datum["tradeId"].GetString()});
          dataPoint.insert({MarketDataMessage::DataFieldType::IS_BUYER_MAKER, std::string(datum["side"].GetString()) == "sell" ? "1" : "0"});
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
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_OKEX_H_
