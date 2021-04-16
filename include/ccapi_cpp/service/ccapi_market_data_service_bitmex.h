#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_BITMEX_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_BITMEX_H_
#ifdef CCAPI_ENABLE_SERVICE_MARKET_DATA
#ifdef CCAPI_ENABLE_EXCHANGE_BITMEX
#include <regex>
#include "ccapi_cpp/service/ccapi_market_data_service.h"
namespace ccapi {
class MarketDataServiceBitmex CCAPI_FINAL : public MarketDataService {
 public:
  MarketDataServiceBitmex(std::function<void(Event& event)> wsEventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                          std::shared_ptr<ServiceContext> serviceContextPtr)
      : MarketDataService(wsEventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    this->exchangeName = CCAPI_EXCHANGE_NAME_BITMEX;
    this->baseUrl = sessionConfigs.getUrlWebsocketBase().at(this->exchangeName);
    this->baseUrlRest = this->sessionConfigs.getUrlRestBase().at(this->exchangeName);
    this->setHostFromUrl(this->baseUrlRest);
    this->getRecentTradesTarget = "/api/v1/trade";
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
        if (channelId == CCAPI_WEBSOCKET_BITMEX_CHANNEL_QUOTE || channelId == CCAPI_WEBSOCKET_BITMEX_CHANNEL_ORDER_BOOK_10) {
          this->l2UpdateIsReplaceByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = true;
        }
        std::string exchangeSubscriptionId = channelId + ":" + symbolId;
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_CHANNEL_ID] = channelId;
        this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap[wsConnection.id][exchangeSubscriptionId][CCAPI_SYMBOL_ID] = symbolId;
        args.PushBack(rj::Value(exchangeSubscriptionId.c_str(), allocator).Move(), allocator);
      }
    }
    CCAPI_LOGGER_TRACE("this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap = " +
                       toString(this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap));
    document.AddMember("args", args, allocator);
    rj::StringBuffer stringBuffer;
    rj::Writer<rj::StringBuffer> writer(stringBuffer);
    document.Accept(writer);
    std::string requestString = stringBuffer.GetString();
    requestStringList.push_back(requestString);
    return requestStringList;
  }
  void onClose(wspp::connection_hdl hdl) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    this->priceByConnectionIdChannelIdSymbolIdPriceIdMap.erase(wsConnection.id);
    // this->previousTradeTimeByConnectionIdSymbolIdMap.erase(wsConnection.id);
    // this->previousTradeIdByConnectionIdSymbolIdMap.erase(wsConnection.id);
    MarketDataService::onClose(hdl);
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  std::vector<MarketDataMessage> processTextMessage(WsConnection& wsConnection, wspp::connection_hdl hdl, const std::string& textMessage,
                                                    const TimePoint& timeReceived) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    std::vector<MarketDataMessage> marketDataMessageList;
    if (textMessage != "pong") {
      rj::Document document;
      std::string quotedTextMessage = std::regex_replace(textMessage, std::regex("(\\[|,|\":)(-?\\d+\\.?\\d*)"), "$1\"$2\"");
      CCAPI_LOGGER_TRACE("quotedTextMessage = " + quotedTextMessage);
      document.Parse(quotedTextMessage.c_str());
      if (document.IsObject() && document.HasMember("table")) {
        std::string channelId = document["table"].GetString();
        if (channelId == CCAPI_WEBSOCKET_BITMEX_CHANNEL_ORDER_BOOK_10 || channelId == CCAPI_WEBSOCKET_BITMEX_CHANNEL_QUOTE) {
          std::string action = document["action"].GetString();
          MarketDataMessage::RecapType recapType;
          if (action == "partial") {
            recapType = MarketDataMessage::RecapType::SOLICITED;
          } else {
            recapType = MarketDataMessage::RecapType::NONE;
          }
          int i = 0;
          std::string symbolId;
          std::string exchangeSubscriptionId;
          for (const auto& x : document["data"].GetArray()) {
            if (i == 0) {
              symbolId = x["symbol"].GetString();
              exchangeSubscriptionId = channelId + ":" + symbolId;
            }
            MarketDataMessage marketDataMessage;
            marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
            marketDataMessage.recapType = recapType;
            marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
            marketDataMessage.tp = UtilTime::parse(std::string(x["timestamp"].GetString()));
            if (channelId == CCAPI_WEBSOCKET_BITMEX_CHANNEL_QUOTE) {
              MarketDataMessage::TypeForDataPoint dataPointBid;
              dataPointBid.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(x["bidPrice"].GetString())});
              dataPointBid.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(x["bidSize"].GetString())});
              marketDataMessage.data[MarketDataMessage::DataType::BID].push_back(std::move(dataPointBid));
              MarketDataMessage::TypeForDataPoint dataPointAsk;
              dataPointAsk.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(x["askPrice"].GetString())});
              dataPointAsk.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(x["askSize"].GetString())});
              marketDataMessage.data[MarketDataMessage::DataType::ASK].push_back(std::move(dataPointAsk));
            } else {
              for (const auto& y : x["bids"].GetArray()) {
                MarketDataMessage::TypeForDataPoint dataPoint;
                dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(y[0].GetString())});
                dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(y[1].GetString())});
                marketDataMessage.data[MarketDataMessage::DataType::BID].push_back(std::move(dataPoint));
              }
              for (const auto& y : x["asks"].GetArray()) {
                MarketDataMessage::TypeForDataPoint dataPoint;
                dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(y[0].GetString())});
                dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(y[1].GetString())});
                marketDataMessage.data[MarketDataMessage::DataType::ASK].push_back(std::move(dataPoint));
              }
            }
            marketDataMessageList.push_back(std::move(marketDataMessage));
            ++i;
          }
        } else if (channelId == CCAPI_WEBSOCKET_BITMEX_CHANNEL_ORDER_BOOK_L2 || channelId == CCAPI_WEBSOCKET_BITMEX_CHANNEL_ORDER_BOOK_L2_25) {
          std::string action = document["action"].GetString();
          MarketDataMessage marketDataMessage;
          marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
          marketDataMessage.tp = timeReceived;
          marketDataMessage.recapType = action == "partial" ? MarketDataMessage::RecapType::SOLICITED : MarketDataMessage::RecapType::NONE;
          int i = 0;
          std::string symbolId;
          std::string exchangeSubscriptionId;
          for (const auto& x : document["data"].GetArray()) {
            if (i == 0) {
              symbolId = x["symbol"].GetString();
              exchangeSubscriptionId = channelId + ":" + symbolId;
              marketDataMessage.exchangeSubscriptionId = exchangeSubscriptionId;
            }
            MarketDataMessage::TypeForDataPoint dataPoint;
            std::string price;
            std::string size;
            std::string priceId = x["id"].GetString();
            if (action == "insert" || action == "partial") {
              price = UtilString::normalizeDecimalString(x["price"].GetString());
              size = UtilString::normalizeDecimalString(x["size"].GetString());
              this->priceByConnectionIdChannelIdSymbolIdPriceIdMap[wsConnection.id][channelId][symbolId][priceId] = price;
            } else {
              price = this->priceByConnectionIdChannelIdSymbolIdPriceIdMap[wsConnection.id][channelId][symbolId][priceId];
              if (price.empty()) {
                this->onIncorrectStatesFound(wsConnection, hdl, textMessage, timeReceived, exchangeSubscriptionId,
                                             "bitmex update for missing item came through on wsConnection = " + toString(wsConnection) +
                                                 ", channelId = " + channelId + ", symbolId = " + symbolId + ", priceId = " + priceId + ". Data: " +
                                                 toString(this->priceByConnectionIdChannelIdSymbolIdPriceIdMap[wsConnection.id][channelId][symbolId]));
              }
              if (action == "update") {
                size = UtilString::normalizeDecimalString(x["size"].GetString());
              } else if (action == "delete") {
                size = "0";
              }
            }
            std::string side = x["side"].GetString();
            dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, price});
            dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, size});
            marketDataMessage.data[side == "Buy" ? MarketDataMessage::DataType::BID : MarketDataMessage::DataType::ASK].push_back(std::move(dataPoint));
            ++i;
          }
          if (i > 0) {
            marketDataMessageList.push_back(std::move(marketDataMessage));
          }
        } else if (channelId == CCAPI_WEBSOCKET_BITMEX_CHANNEL_TRADE) {
          std::string action = document["action"].GetString();
          for (const auto& x : document["data"].GetArray()) {
            MarketDataMessage marketDataMessage;
            marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
            marketDataMessage.tp = UtilTime::parse(std::string(x["timestamp"].GetString()));
            marketDataMessage.recapType = action == "partial" ? MarketDataMessage::RecapType::SOLICITED : MarketDataMessage::RecapType::NONE;
            std::string symbolId = x["symbol"].GetString();
            marketDataMessage.exchangeSubscriptionId = channelId + ":" + symbolId;
            MarketDataMessage::TypeForDataPoint dataPoint;
            dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(std::string(x["price"].GetString()))});
            dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(std::string(x["size"].GetString()))});
            auto timePair = UtilTime::divide(marketDataMessage.tp);
            // std::stringstream ss;
            // ss << std::setw(9) << std::setfill('0') << timePair.second;
            // int64_t tradeId = std::stoll(std::to_string(timePair.first) + ss.str());
            // if (this->previousTradeTimeByConnectionIdSymbolIdMap.find(wsConnection.id) != this->previousTradeTimeByConnectionIdSymbolIdMap.end() &&
            //     this->previousTradeTimeByConnectionIdSymbolIdMap.at(wsConnection.id).find(symbolId) !=
            //         this->previousTradeTimeByConnectionIdSymbolIdMap.at(wsConnection.id).end() &&
            //     marketDataMessage.tp == this->previousTradeTimeByConnectionIdSymbolIdMap.at(wsConnection.id).at(symbolId)) {
            //   tradeId = this->previousTradeIdByConnectionIdSymbolIdMap[wsConnection.id][symbolId] + 1;
            // } else {
            //   this->previousTradeTimeByConnectionIdSymbolIdMap[wsConnection.id][symbolId] = marketDataMessage.tp;
            // }
            // this->previousTradeIdByConnectionIdSymbolIdMap[wsConnection.id][symbolId] = tradeId;
            // dataPoint.insert({MarketDataMessage::DataFieldType::TRADE_ID, std::to_string(tradeId)});
            dataPoint.insert({MarketDataMessage::DataFieldType::TRADE_ID, std::string(x["trdMatchID"].GetString())});
            dataPoint.insert({MarketDataMessage::DataFieldType::IS_BUYER_MAKER, std::string(x["side"].GetString()) == "Sell" ? "1" : "0"});
            marketDataMessage.data[MarketDataMessage::DataType::TRADE].push_back(std::move(dataPoint));
            marketDataMessageList.push_back(std::move(marketDataMessage));
          }
        }
      } else if (document.IsObject() && document.HasMember("subscribe") && document["success"].GetBool()) {
        // TODO(cryptochassis): implement
      }
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
        std::string queryString;
        const std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        this->appendParam(queryString, param, {{CCAPI_LIMIT, "count"}, {"reverse", "true"}});
        this->appendSymbolId(queryString, symbolId, "symbol");
        req.target(target + "?" + queryString);
      } break;
      default:
        CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
    }
  }
  void processSuccessfulTextMessage(const Request& request, const std::string& textMessage, const TimePoint& timeReceived) override {
    std::string quotedTextMessage = std::regex_replace(textMessage, std::regex("(\\[|,|\":)(-?\\d+\\.?\\d*)"), "$1\"$2\"");
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
        for (const auto& x : document.GetArray()) {
          MarketDataMessage marketDataMessage;
          marketDataMessage.type = MarketDataMessage::Type::MARKET_DATA_EVENTS;
          marketDataMessage.tp = UtilTime::parse(std::string(x["timestamp"].GetString()));
          MarketDataMessage::TypeForDataPoint dataPoint;
          dataPoint.insert({MarketDataMessage::DataFieldType::PRICE, UtilString::normalizeDecimalString(std::string(x["price"].GetString()))});
          dataPoint.insert({MarketDataMessage::DataFieldType::SIZE, UtilString::normalizeDecimalString(std::string(x["size"].GetString()))});
          dataPoint.insert({MarketDataMessage::DataFieldType::TRADE_ID, std::string(x["trdMatchID"].GetString())});
          dataPoint.insert({MarketDataMessage::DataFieldType::IS_BUYER_MAKER, std::string(x["side"].GetString()) == "Sell" ? "1" : "0"});
          marketDataMessage.data[MarketDataMessage::DataType::TRADE].push_back(std::move(dataPoint));
          marketDataMessageList.push_back(std::move(marketDataMessage));
        }
      } break;
      default:
        CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
    }
    return marketDataMessageList;
  }
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<std::string, std::string> > > > priceByConnectionIdChannelIdSymbolIdPriceIdMap;
  // std::map<std::string, std::map<std::string, TimePoint> > previousTradeTimeByConnectionIdSymbolIdMap;
  // std::map<std::string, std::map<std::string, int64_t> > previousTradeIdByConnectionIdSymbolIdMap;
};
} /* namespace ccapi */
#endif
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_BITMEX_H_
