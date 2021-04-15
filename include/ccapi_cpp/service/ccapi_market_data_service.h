#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_H_
#ifdef CCAPI_ENABLE_SERVICE_MARKET_DATA
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "ccapi_cpp/ccapi_decimal.h"
#include "ccapi_cpp/ccapi_logger.h"
#include "ccapi_cpp/ccapi_util_private.h"
#include "ccapi_cpp/service/ccapi_service.h"
namespace ccapi {
class MarketDataService : public Service {
 public:
  // enum class PingPongMethod { WEBSOCKET_PROTOCOL_LEVEL, WEBSOCKET_APPLICATION_LEVEL };
  // static std::string pingPongMethodToString(PingPongMethod pingPongMethod) {
  //   std::string output;
  //   switch (pingPongMethod) {
  //     case PingPongMethod::WEBSOCKET_PROTOCOL_LEVEL:
  //       output = "WEBSOCKET_PROTOCOL_LEVEL";
  //       break;
  //     case PingPongMethod::WEBSOCKET_APPLICATION_LEVEL:
  //       output = "WEBSOCKET_APPLICATION_LEVEL";
  //       break;
  //     default:
  //       CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
  //   }
  //   return output;
  // }
  MarketDataService(std::function<void(Event& event)> eventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                    std::shared_ptr<ServiceContext> serviceContextPtr)
      : Service(eventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    this->requestOperationToMessageTypeMap = {
        {Request::Operation::GET_RECENT_TRADES, Message::Type::GET_RECENT_TRADES},
    };
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  virtual ~MarketDataService() {
    for (const auto& x : this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap) {
      for (const auto& y : x.second) {
        for (const auto& z : y.second) {
          z.second->cancel();
        }
      }
    }
    // for (const auto& x : this->pingTimerByMethodByConnectionIdMap) {
    //   for (const auto& y : x.second) {
    //     y.second->cancel();
    //   }
    // }
    // for (const auto& x : this->pongTimeOutTimerByMethodByConnectionIdMap) {
    //   for (const auto& y : x.second) {
    //     y.second->cancel();
    //   }
    // }
    // for (const auto& x : this->connectRetryOnFailTimerByConnectionIdMap) {
    //   x.second->cancel();
    // }
  }
  // void stop() override {
  //   Service::stop();
  //   this->shouldContinue = false;
  //   for (const auto& x : this->wsConnectionMap) {
  //     auto wsConnection = x.second;
  //     ErrorCode ec;
  //     this->close(wsConnection, wsConnection.hdl, websocketpp::close::status::normal, "stop", ec);
  //     if (ec) {
  //       this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, ec, "shutdown");
  //     }
  //     this->shouldProcessRemainingMessageOnClosingByConnectionIdMap[wsConnection.id] = false;
  //   }
  // }
  void subscribe(const std::vector<Subscription>& subscriptionList) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    CCAPI_LOGGER_DEBUG("this->baseUrl = " + this->baseUrl);
    if (this->shouldContinue.load()) {
      for (const auto& x : this->groupSubscriptionListByInstrumentGroup(subscriptionList)) {
        auto instrumentGroup = x.first;
        auto subscriptionListGivenInstrumentGroup = x.second;
        wspp::lib::asio::post(this->serviceContextPtr->tlsClientPtr->get_io_service(), [that = shared_from_base<MarketDataService>(), instrumentGroup,
                                                                                        subscriptionListGivenInstrumentGroup]() {
          std::map<std::string, std::vector<std::string>> wsConnectionIdListByInstrumentGroupMap = invertMapMulti(that->instrumentGroupByWsConnectionIdMap);
          if (wsConnectionIdListByInstrumentGroupMap.find(instrumentGroup) != wsConnectionIdListByInstrumentGroupMap.end() &&
              that->subscriptionStatusByInstrumentGroupInstrumentMap.find(instrumentGroup) != that->subscriptionStatusByInstrumentGroupInstrumentMap.end()) {
            auto wsConnectionId = wsConnectionIdListByInstrumentGroupMap.at(instrumentGroup).at(0);
            auto wsConnection = that->wsConnectionMap.at(wsConnectionId);
            for (const auto& subscription : subscriptionListGivenInstrumentGroup) {
              auto instrument = subscription.getInstrument();
              if (that->subscriptionStatusByInstrumentGroupInstrumentMap[instrumentGroup].find(instrument) !=
                  that->subscriptionStatusByInstrumentGroupInstrumentMap[instrumentGroup].end()) {
                that->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::SUBSCRIPTION_FAILURE, "already subscribed: " + toString(subscription));
                return;
              }
              wsConnection.subscriptionList.push_back(subscription);
              that->subscriptionStatusByInstrumentGroupInstrumentMap[instrumentGroup][instrument] = Subscription::Status::SUBSCRIBING;
              that->prepareSubscription(wsConnection, subscription);
            }
            CCAPI_LOGGER_INFO("about to subscribe to exchange");
            that->subscribeToExchange(wsConnection);
          } else {
            auto url = UtilString::split(instrumentGroup, "|").at(0);
            WsConnection wsConnection(url, instrumentGroup, subscriptionListGivenInstrumentGroup);
            that->prepareConnect(wsConnection);
          }
        });
      }
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }

 protected:
  typedef wspp::lib::error_code ErrorCode;
  typedef wspp::lib::function<void(ErrorCode const&)> TimerHandler;
  std::map<std::string, std::vector<Subscription>> groupSubscriptionListByInstrumentGroup(const std::vector<Subscription>& subscriptionList) {
    std::map<std::string, std::vector<Subscription>> groups;
    for (const auto& subscription : subscriptionList) {
      std::string instrumentGroup = this->getInstrumentGroup(subscription);
      groups[instrumentGroup].push_back(subscription);
    }
    return groups;
  }
  virtual std::string getInstrumentGroup(const Subscription& subscription) {
    return this->baseUrl + "|" + subscription.getField() + "|" + subscription.getSerializedOptions();
  }
  // SslContextPtr onTlsInit(wspp::connection_hdl hdl) { return this->serviceContextPtr->sslContextPtr; }
  virtual void onOpen(wspp::connection_hdl hdl) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    auto now = UtilTime::now();
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    wsConnection.status = WsConnection::Status::OPEN;
    wsConnection.hdl = hdl;
    CCAPI_LOGGER_INFO("connection " + toString(wsConnection) + " established");
    this->connectNumRetryOnFailByConnectionUrlMap[wsConnection.url] = 0;
    Event event;
    event.setType(Event::Type::SESSION_STATUS);
    Message message;
    message.setTimeReceived(now);
    message.setType(Message::Type::SESSION_CONNECTION_UP);
    std::vector<std::string> correlationIdList;
    for (const auto& subscription : wsConnection.subscriptionList) {
      correlationIdList.push_back(subscription.getCorrelationId());
    }
    CCAPI_LOGGER_DEBUG("correlationIdList = " + toString(correlationIdList));
    message.setCorrelationIdList(correlationIdList);
    Element element;
    element.insert(CCAPI_CONNECTION, toString(wsConnection));
    message.setElementList({element});
    event.setMessageList({message});
    this->eventHandler(event);
    if (this->enableCheckPingPongWebsocketProtocolLevel) {
      this->setPingPongTimer(PingPongMethod::WEBSOCKET_PROTOCOL_LEVEL, wsConnection, hdl,
                             [that = shared_from_base<MarketDataService>()](wspp::connection_hdl hdl, ErrorCode& ec) { that->ping(hdl, "", ec); });
    }
    if (this->enableCheckPingPongWebsocketApplicationLevel) {
      this->setPingPongTimer(
          PingPongMethod::WEBSOCKET_APPLICATION_LEVEL, wsConnection, hdl,
          [that = shared_from_base<MarketDataService>()](wspp::connection_hdl hdl, ErrorCode& ec) { that->pingOnApplicationLevel(hdl, ec); });
    }
    auto instrumentGroup = wsConnection.group;
    for (const auto& subscription : wsConnection.subscriptionList) {
      auto instrument = subscription.getInstrument();
      this->subscriptionStatusByInstrumentGroupInstrumentMap[instrumentGroup][instrument] = Subscription::Status::SUBSCRIBING;
      this->prepareSubscription(wsConnection, subscription);
    }
    CCAPI_LOGGER_INFO("about to subscribe to exchange");
    this->subscribeToExchange(wsConnection);
  }
  // std::string convertInstrumentToWebsocketSymbolId(std::string instrument) {
  //   std::string symbolId = instrument;
  //   if (!instrument.empty()) {
  //     if (this->sessionConfigs.getExchangeInstrumentSymbolMap().find(this->exchangeName) != this->sessionConfigs.getExchangeInstrumentSymbolMap().end() &&
  //         this->sessionConfigs.getExchangeInstrumentSymbolMap().at(this->exchangeName).find(instrument) !=
  //             this->sessionConfigs.getExchangeInstrumentSymbolMap().at(this->exchangeName).end()) {
  //       symbolId = this->sessionConfigs.getExchangeInstrumentSymbolMap().at(this->exchangeName).at(instrument);
  //     }
  //   }
  //   return symbolId;
  // }
  void prepareSubscription(const WsConnection& wsConnection, const Subscription& subscription) {
    auto instrument = subscription.getInstrument();
    CCAPI_LOGGER_TRACE("instrument = " + instrument);
    auto symbolId = this->convertInstrumentToWebsocketSymbolId(instrument);
    CCAPI_LOGGER_TRACE("symbolId = " + symbolId);
    auto field = subscription.getField();
    CCAPI_LOGGER_TRACE("field = " + field);
    auto optionMap = subscription.getOptionMap();
    CCAPI_LOGGER_TRACE("optionMap = " + toString(optionMap));
    auto marketDepthRequested = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
    CCAPI_LOGGER_TRACE("marketDepthRequested = " + toString(marketDepthRequested));
    std::string channelId = this->sessionConfigs.getExchangeFieldWebsocketChannelMap().at(this->exchangeName).at(field);
    CCAPI_LOGGER_TRACE("channelId = " + channelId);
    CCAPI_LOGGER_TRACE("this->exchangeName = " + this->exchangeName);
    if (field == CCAPI_MARKET_DEPTH) {
      if (this->exchangeName == CCAPI_EXCHANGE_NAME_KRAKEN || this->exchangeName == CCAPI_EXCHANGE_NAME_BITFINEX ||
          this->exchangeName == CCAPI_EXCHANGE_NAME_BINANCE_US || this->exchangeName == CCAPI_EXCHANGE_NAME_BINANCE ||
          this->exchangeName == CCAPI_EXCHANGE_NAME_BINANCE_FUTURES) {
        int marketDepthSubscribedToExchange = 1;
        marketDepthSubscribedToExchange = this->calculateMarketDepthSubscribedToExchange(
            marketDepthRequested, this->sessionConfigs.getWebsocketAvailableMarketDepth().at(this->exchangeName));
        channelId += std::string("?") + CCAPI_MARKET_DEPTH_SUBSCRIBED_TO_EXCHANGE + "=" + std::to_string(marketDepthSubscribedToExchange);
        this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = marketDepthSubscribedToExchange;
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_GEMINI) {
        if (marketDepthRequested == 1) {
          int marketDepthSubscribedToExchange = 1;
          channelId += std::string("?") + CCAPI_MARKET_DEPTH_SUBSCRIBED_TO_EXCHANGE + "=" + std::to_string(marketDepthSubscribedToExchange);
          this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = marketDepthSubscribedToExchange;
        }
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_BITMEX) {
        if (marketDepthRequested == 1) {
          channelId = CCAPI_WEBSOCKET_BITMEX_CHANNEL_QUOTE;
        } else if (marketDepthRequested <= 10) {
          channelId = CCAPI_WEBSOCKET_BITMEX_CHANNEL_ORDER_BOOK_10;
        } else if (marketDepthRequested <= 25) {
          channelId = CCAPI_WEBSOCKET_BITMEX_CHANNEL_ORDER_BOOK_L2_25;
        }
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_HUOBI || this->exchangeName == CCAPI_EXCHANGE_NAME_HUOBI_USDT_SWAP) {
        if (marketDepthRequested == 1) {
          channelId = CCAPI_WEBSOCKET_HUOBI_CHANNEL_MARKET_BBO;
        }
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_OKEX) {
        if (marketDepthRequested <= 5) {
          channelId = CCAPI_WEBSOCKET_OKEX_CHANNEL_PUBLIC_DEPTH5;
        } else {
          channelId = CCAPI_WEBSOCKET_OKEX_CHANNEL_PUBLIC_DEPTH400;
        }
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_ERISX) {
        if (marketDepthRequested <= 20) {
          channelId = std::string(CCAPI_WEBSOCKET_ERISX_CHANNEL_TOP_OF_BOOK_MARKET_DATA_SUBSCRIBE) + "?" + CCAPI_MARKET_DEPTH_SUBSCRIBED_TO_EXCHANGE + "=" +
                      std::to_string(marketDepthRequested);
          this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = marketDepthRequested;
        } else {
          channelId += "|" + field;
        }
      } else if (this->exchangeName == CCAPI_EXCHANGE_NAME_KUCOIN) {
        if (marketDepthRequested == 1) {
          channelId = CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_TICKER;
        } else if (marketDepthRequested <= 5) {
          channelId = CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_LEVEL2DEPTH5;
        } else {
          channelId = CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_LEVEL2DEPTH50;
        }
      }
    } else if (field == CCAPI_TRADE) {
      if (this->exchangeName == CCAPI_EXCHANGE_NAME_ERISX) {
        channelId += "|" + field;
      }
    }
    CCAPI_LOGGER_TRACE("channelId = " + channelId);
    this->correlationIdListByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].push_back(subscription.getCorrelationId());
    this->subscriptionListByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].push_back(subscription);
    this->fieldByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = field;
    this->optionMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].insert(optionMap.begin(), optionMap.end());
    CCAPI_LOGGER_TRACE("this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap = " +
                       toString(this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap));
    CCAPI_LOGGER_TRACE("this->correlationIdListByConnectionIdChannelSymbolIdMap = " + toString(this->correlationIdListByConnectionIdChannelIdSymbolIdMap));
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void onFail_(WsConnection& wsConnection) {
    wsConnection.status = WsConnection::Status::FAILED;
    this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::SUBSCRIPTION_FAILURE, "connection " + toString(wsConnection) + " has failed before opening");
    WsConnection thisWsConnection = wsConnection;
    this->wsConnectionMap.erase(thisWsConnection.id);
    this->instrumentGroupByWsConnectionIdMap.erase(thisWsConnection.id);
    auto urlBase = UtilString::split(thisWsConnection.url, "?").at(0);
    long seconds = std::round(UtilAlgorithm::exponentialBackoff(1, 1, 2, std::min(this->connectNumRetryOnFailByConnectionUrlMap[thisWsConnection.url], 6)));
    CCAPI_LOGGER_INFO("about to set timer for " + toString(seconds) + " seconds");
    if (this->connectRetryOnFailTimerByConnectionIdMap.find(thisWsConnection.id) != this->connectRetryOnFailTimerByConnectionIdMap.end()) {
      this->connectRetryOnFailTimerByConnectionIdMap.at(thisWsConnection.id)->cancel();
    }
    this->connectRetryOnFailTimerByConnectionIdMap[thisWsConnection.id] = this->serviceContextPtr->tlsClientPtr->set_timer(
        seconds * 1000, [thisWsConnection, that = shared_from_base<MarketDataService>(), urlBase](ErrorCode const& ec) {
          if (that->wsConnectionMap.find(thisWsConnection.id) == that->wsConnectionMap.end()) {
            if (ec) {
              CCAPI_LOGGER_ERROR("wsConnection = " + toString(thisWsConnection) + ", connect retry on fail timer error: " + ec.message());
              that->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, ec, "timer");
            } else {
              CCAPI_LOGGER_INFO("about to retry");
              auto thatWsConnection = thisWsConnection;
              thatWsConnection.assignDummyId();
              that->prepareConnect(thatWsConnection);
              that->connectNumRetryOnFailByConnectionUrlMap[urlBase] += 1;
            }
          }
        });
  }
  virtual void onFail(wspp::connection_hdl hdl) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    this->clearStates(wsConnection);
    this->onFail_(wsConnection);
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void clearStates(WsConnection& wsConnection) {
    CCAPI_LOGGER_INFO("clear states for wsConnection " + toString(wsConnection));
    this->fieldByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->optionMapByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->subscriptionListByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->correlationIdListByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap.erase(wsConnection.id);
    this->snapshotBidByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->snapshotAskByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->processedInitialTradeByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->l2UpdateIsReplaceByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->shouldProcessRemainingMessageOnClosingByConnectionIdMap.erase(wsConnection.id);
    this->lastPongTpByMethodByConnectionIdMap.erase(wsConnection.id);
    this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    if (this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.find(wsConnection.id) != this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.end()) {
      for (const auto& x : this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id)) {
        for (const auto& y : x.second) {
          y.second->cancel();
        }
      }
      this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    }
    this->openByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->highByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->lowByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->closeByConnectionIdChannelIdSymbolIdMap.erase(wsConnection.id);
    this->extraPropertyByConnectionIdMap.erase(wsConnection.id);
    if (this->pingTimerByMethodByConnectionIdMap.find(wsConnection.id) != this->pingTimerByMethodByConnectionIdMap.end()) {
      for (const auto& x : this->pingTimerByMethodByConnectionIdMap.at(wsConnection.id)) {
        x.second->cancel();
      }
      this->pingTimerByMethodByConnectionIdMap.erase(wsConnection.id);
    }
    if (this->pongTimeOutTimerByMethodByConnectionIdMap.find(wsConnection.id) != this->pongTimeOutTimerByMethodByConnectionIdMap.end()) {
      for (const auto& x : this->pongTimeOutTimerByMethodByConnectionIdMap.at(wsConnection.id)) {
        x.second->cancel();
      }
      this->pongTimeOutTimerByMethodByConnectionIdMap.erase(wsConnection.id);
    }
    this->connectNumRetryOnFailByConnectionUrlMap.erase(wsConnection.url);
    if (this->connectRetryOnFailTimerByConnectionIdMap.find(wsConnection.id) != this->connectRetryOnFailTimerByConnectionIdMap.end()) {
      this->connectRetryOnFailTimerByConnectionIdMap.at(wsConnection.id)->cancel();
      this->connectRetryOnFailTimerByConnectionIdMap.erase(wsConnection.id);
    }
    this->orderBookChecksumByConnectionIdSymbolIdMap.erase(wsConnection.id);
  }
  virtual void onClose(wspp::connection_hdl hdl) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    auto now = UtilTime::now();
    TlsClient::connection_ptr con = this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl);
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(con);
    wsConnection.status = WsConnection::Status::CLOSED;
    CCAPI_LOGGER_INFO("connection " + toString(wsConnection) + " is closed");
    std::stringstream s;
    s << "close code: " << con->get_remote_close_code() << " (" << websocketpp::close::status::get_string(con->get_remote_close_code())
      << "), close reason: " << con->get_remote_close_reason();
    std::string reason = s.str();
    CCAPI_LOGGER_INFO("reason is " + reason);
    Event event;
    event.setType(Event::Type::SESSION_STATUS);
    Message message;
    message.setTimeReceived(now);
    message.setType(Message::Type::SESSION_CONNECTION_DOWN);
    Element element;
    element.insert(CCAPI_CONNECTION, toString(wsConnection));
    element.insert(CCAPI_REASON, reason);
    message.setElementList({element});
    event.setMessageList({message});
    this->eventHandler(event);
    CCAPI_LOGGER_INFO("connection " + toString(wsConnection) + " is closed");
    this->clearStates(wsConnection);
    WsConnection thisWsConnection = wsConnection;
    this->wsConnectionMap.erase(wsConnection.id);
    this->instrumentGroupByWsConnectionIdMap.erase(wsConnection.id);
    if (this->shouldContinue.load()) {
      thisWsConnection.assignDummyId();
      this->prepareConnect(thisWsConnection);
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void onMessage(wspp::connection_hdl hdl, TlsClient::message_ptr msg) {
    auto now = UtilTime::now();
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    CCAPI_LOGGER_DEBUG("received a message from connection " + toString(wsConnection));
    if (wsConnection.status == WsConnection::Status::CLOSING && !this->shouldProcessRemainingMessageOnClosingByConnectionIdMap[wsConnection.id]) {
      CCAPI_LOGGER_WARN("should not process remaining message on closing");
      return;
    }
    auto opcode = msg->get_opcode();
    CCAPI_LOGGER_DEBUG("opcode = " + toString(opcode));
    if (msg->get_opcode() == websocketpp::frame::opcode::text) {
      std::string textMessage = msg->get_payload();
      CCAPI_LOGGER_DEBUG("received a text message: " + textMessage);
      try {
        this->onTextMessage(hdl, textMessage, now);
      } catch (const std::exception& e) {
        CCAPI_LOGGER_ERROR("textMessage = " + textMessage);
        this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, e);
      }
    } else if (opcode == websocketpp::frame::opcode::binary) {
#if defined(CCAPI_ENABLE_EXCHANGE_HUOBI) || defined(CCAPI_ENABLE_EXCHANGE_HUOBI_USDT_SWAP) || defined(CCAPI_ENABLE_EXCHANGE_OKEX)
      if (this->exchangeName == CCAPI_EXCHANGE_NAME_HUOBI || this->exchangeName == CCAPI_EXCHANGE_NAME_HUOBI_USDT_SWAP ||
          this->exchangeName == CCAPI_EXCHANGE_NAME_OKEX) {
        std::string decompressed;
        std::string payload = msg->get_payload();
        try {
          ErrorCode ec = this->inflater.decompress(reinterpret_cast<const uint8_t*>(&payload[0]), payload.size(), decompressed);
          if (ec) {
            CCAPI_LOGGER_FATAL(ec.message());
          }
          CCAPI_LOGGER_DEBUG("decompressed = " + decompressed);
          this->onTextMessage(hdl, decompressed, now);
        } catch (const std::exception& e) {
          std::stringstream ss;
          ss << std::hex << std::setfill('0');
          for (int i = 0; i < payload.size(); ++i) {
            ss << std::setw(2) << static_cast<unsigned>(reinterpret_cast<const uint8_t*>(&payload[0])[i]);
          }
          CCAPI_LOGGER_ERROR("binaryMessage = " + ss.str());
          this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, e);
        }
        ErrorCode ec = this->inflater.inflate_reset();
        if (ec) {
          this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, ec, "decompress");
        }
      }
#endif
    }
  }
  void onPong(wspp::connection_hdl hdl, std::string payload) {
    auto now = UtilTime::now();
    this->onPongByMethod(PingPongMethod::WEBSOCKET_PROTOCOL_LEVEL, hdl, payload, now);
  }
  void onPongByMethod(PingPongMethod method, wspp::connection_hdl hdl, const std::string& textMessage, const TimePoint& timeReceived) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    CCAPI_LOGGER_TRACE(pingPongMethodToString(method) + ": received a pong from " + toString(wsConnection));
    this->lastPongTpByMethodByConnectionIdMap[wsConnection.id][method] = timeReceived;
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  bool onPing(wspp::connection_hdl hdl, std::string payload) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    CCAPI_LOGGER_TRACE("received a ping from " + toString(wsConnection));
    CCAPI_LOGGER_FUNCTION_EXIT;
    return true;
  }
  virtual void onTextMessage(wspp::connection_hdl hdl, const std::string& textMessage, const TimePoint& timeReceived) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    WsConnection& wsConnection = this->getWsConnectionFromConnectionPtr(this->serviceContextPtr->tlsClientPtr->get_con_from_hdl(hdl));
    const std::vector<MarketDataMessage>& marketDataMessageList = this->processTextMessage(wsConnection, hdl, textMessage, timeReceived);
    CCAPI_LOGGER_TRACE("websocketMessageList = " + toString(marketDataMessageList));
    if (!marketDataMessageList.empty()) {
      for (auto const& marketDataMessage : marketDataMessageList) {
        // TODO(cryptochassis): should make Event outside of this for-loop, but need to carefully study the implications
        Event event;
        bool shouldEmitEvent = true;
        if (marketDataMessage.type == MarketDataMessage::Type::MARKET_DATA_EVENTS) {
          if (marketDataMessage.recapType == MarketDataMessage::RecapType::NONE && this->sessionOptions.warnLateEventMaxMilliSeconds > 0 &&
              std::chrono::duration_cast<std::chrono::milliseconds>(timeReceived - marketDataMessage.tp).count() >
                  this->sessionOptions.warnLateEventMaxMilliSeconds) {
            CCAPI_LOGGER_WARN("late websocket message: timeReceived = " + toString(timeReceived) +
                              ", marketDataMessage.tp = " + toString(marketDataMessage.tp) + ", wsConnection = " + toString(wsConnection));
          }
          event.setType(Event::Type::SUBSCRIPTION_DATA);
          std::string exchangeSubscriptionId = marketDataMessage.exchangeSubscriptionId;
          std::string channelId =
              this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap.at(wsConnection.id).at(exchangeSubscriptionId).at(CCAPI_CHANNEL_ID);
          std::string symbolId =
              this->channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap.at(wsConnection.id).at(exchangeSubscriptionId).at(CCAPI_SYMBOL_ID);
          auto field = this->fieldByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
          CCAPI_LOGGER_TRACE("this->optionMapByConnectionIdChannelIdSymbolIdMap = " + toString(this->optionMapByConnectionIdChannelIdSymbolIdMap));
          CCAPI_LOGGER_TRACE("wsConnection = " + toString(wsConnection));
          CCAPI_LOGGER_TRACE("channelId = " + toString(channelId));
          CCAPI_LOGGER_TRACE("symbolId = " + toString(symbolId));
          auto optionMap = this->optionMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
          CCAPI_LOGGER_TRACE("optionMap = " + toString(optionMap));
          auto correlationIdList = this->correlationIdListByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
          CCAPI_LOGGER_TRACE("correlationIdList = " + toString(correlationIdList));
          if (marketDataMessage.data.find(MarketDataMessage::DataType::BID) != marketDataMessage.data.end() ||
              marketDataMessage.data.find(MarketDataMessage::DataType::ASK) != marketDataMessage.data.end()) {
            std::map<Decimal, std::string>& snapshotBid = this->snapshotBidByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId];
            std::map<Decimal, std::string>& snapshotAsk = this->snapshotAskByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId];
            if (this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] &&
                marketDataMessage.recapType == MarketDataMessage::RecapType::NONE) {
              this->processUpdateSnapshot(wsConnection, channelId, symbolId, event, shouldEmitEvent, marketDataMessage.tp, timeReceived, marketDataMessage.data,
                                          field, optionMap, correlationIdList, snapshotBid, snapshotAsk);
              if (this->sessionOptions.enableCheckOrderBookChecksum) {
                bool shouldProcessRemainingMessage = true;
                std::string receivedOrderBookChecksumStr = this->orderBookChecksumByConnectionIdSymbolIdMap[wsConnection.id][symbolId];
                if (!this->checkOrderBookChecksum(snapshotBid, snapshotAsk, receivedOrderBookChecksumStr, shouldProcessRemainingMessage)) {
                  CCAPI_LOGGER_ERROR("snapshotBid = " + toString(snapshotBid));
                  CCAPI_LOGGER_ERROR("snapshotAsk = " + toString(snapshotAsk));
                  this->onIncorrectStatesFound(wsConnection, hdl, textMessage, timeReceived, exchangeSubscriptionId, "order book incorrect checksum found");
                }
                if (!shouldProcessRemainingMessage) {
                  return;
                }
              }
              if (this->sessionOptions.enableCheckOrderBookCrossed) {
                bool shouldProcessRemainingMessage = true;
                if (!this->checkOrderBookCrossed(snapshotBid, snapshotAsk, shouldProcessRemainingMessage)) {
                  CCAPI_LOGGER_ERROR("lastNToString(snapshotBid, 1) = " + lastNToString(snapshotBid, 1));
                  CCAPI_LOGGER_ERROR("firstNToString(snapshotAsk, 1) = " + firstNToString(snapshotAsk, 1));
                  this->onIncorrectStatesFound(wsConnection, hdl, textMessage, timeReceived, exchangeSubscriptionId, "order book crossed market found");
                }
                if (!shouldProcessRemainingMessage) {
                  return;
                }
              }
            } else if (marketDataMessage.recapType == MarketDataMessage::RecapType::SOLICITED) {
              this->processInitialSnapshot(wsConnection, channelId, symbolId, event, shouldEmitEvent, marketDataMessage.tp, timeReceived,
                                           marketDataMessage.data, field, optionMap, correlationIdList, snapshotBid, snapshotAsk);
            }
            CCAPI_LOGGER_TRACE("snapshotBid.size() = " + toString(snapshotBid.size()));
            CCAPI_LOGGER_TRACE("snapshotAsk.size() = " + toString(snapshotAsk.size()));
          }
          if (marketDataMessage.data.find(MarketDataMessage::DataType::TRADE) != marketDataMessage.data.end()) {
            this->processTrade(wsConnection, channelId, symbolId, event, shouldEmitEvent, marketDataMessage.tp, timeReceived, marketDataMessage.data, field,
                               optionMap, correlationIdList);
          }
        } else {
          CCAPI_LOGGER_WARN("websocket event type is unknown!");
        }
        CCAPI_LOGGER_TRACE("event type is " + event.typeToString(event.getType()));
        if (event.getType() == Event::Type::UNKNOWN) {
          CCAPI_LOGGER_WARN("event type is unknown!");
        } else {
          if (event.getMessageList().empty()) {
            CCAPI_LOGGER_DEBUG("event has no messages!");
            shouldEmitEvent = false;
          }
          if (shouldEmitEvent) {
            this->eventHandler(event);
          }
        }
      }
    }
    this->onPongByMethod(PingPongMethod::WEBSOCKET_APPLICATION_LEVEL, hdl, textMessage, timeReceived);
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void updateOrderBook(std::map<Decimal, std::string>& snapshot, const Decimal& price, const std::string& size) {
    Decimal sizeDecimal(size);
    if (snapshot.find(price) == snapshot.end()) {
      if (sizeDecimal > Decimal("0")) {
        snapshot.insert(std::pair<Decimal, std::string>(price, size));
      }
    } else {
      if (sizeDecimal > Decimal("0")) {
        snapshot[price] = size;
      } else {
        snapshot.erase(price);
      }
    }
  }
  void updateElementListWithInitialMarketDepth(const std::string& field, const std::map<std::string, std::string>& optionMap,
                                               const std::map<Decimal, std::string>& snapshotBid, const std::map<Decimal, std::string>& snapshotAsk,
                                               std::vector<Element>& elementList) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    if (field == CCAPI_MARKET_DEPTH) {
      int maxMarketDepth = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
      int bidIndex = 0;
      for (auto iter = snapshotBid.rbegin(); iter != snapshotBid.rend(); iter++) {
        if (bidIndex < maxMarketDepth) {
          Element element;
          element.insert(CCAPI_BEST_BID_N_PRICE, iter->first.toString());
          element.insert(CCAPI_BEST_BID_N_SIZE, iter->second);
          elementList.push_back(std::move(element));
        }
        ++bidIndex;
      }
      if (snapshotBid.empty()) {
        Element element;
        element.insert(CCAPI_BEST_BID_N_PRICE, CCAPI_BEST_BID_N_PRICE_EMPTY);
        element.insert(CCAPI_BEST_BID_N_SIZE, CCAPI_BEST_BID_N_SIZE_EMPTY);
        elementList.push_back(std::move(element));
      }
      int askIndex = 0;
      for (auto iter = snapshotAsk.begin(); iter != snapshotAsk.end(); iter++) {
        if (askIndex < maxMarketDepth) {
          Element element;
          element.insert(CCAPI_BEST_ASK_N_PRICE, iter->first.toString());
          element.insert(CCAPI_BEST_ASK_N_SIZE, iter->second);
          elementList.push_back(std::move(element));
        }
        ++askIndex;
      }
      if (snapshotAsk.empty()) {
        Element element;
        element.insert(CCAPI_BEST_ASK_N_PRICE, CCAPI_BEST_ASK_N_PRICE_EMPTY);
        element.insert(CCAPI_BEST_ASK_N_SIZE, CCAPI_BEST_ASK_N_SIZE_EMPTY);
        elementList.push_back(std::move(element));
      }
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  std::map<Decimal, std::string> calculateMarketDepthUpdate(bool isBid, const std::map<Decimal, std::string>& c1, const std::map<Decimal, std::string>& c2,
                                                            int maxMarketDepth) {
    if (c1.empty()) {
      std::map<Decimal, std::string> output;
      for (const auto& x : c2) {
        output.insert(std::make_pair(x.first, "0"));
      }
      return output;
    } else if (c2.empty()) {
      return c1;
    }
    if (isBid) {
      auto it1 = c1.rbegin();
      int i1 = 0;
      auto it2 = c2.rbegin();
      int i2 = 0;
      std::map<Decimal, std::string> output;
      while (i1 < maxMarketDepth && i2 < maxMarketDepth && it1 != c1.rend() && it2 != c2.rend()) {
        if (it1->first < it2->first) {
          output.insert(std::make_pair(it1->first, it1->second));
          ++it1;
          ++i1;
        } else if (it1->first > it2->first) {
          output.insert(std::make_pair(it2->first, "0"));
          ++it2;
          ++i2;
        } else {
          if (it1->second != it2->second) {
            output.insert(std::make_pair(it1->first, it1->second));
          }
          ++it1;
          ++i1;
          ++it2;
          ++i2;
        }
      }
      while (i1 < maxMarketDepth && it1 != c1.rend()) {
        output.insert(std::make_pair(it1->first, it1->second));
        ++it1;
        ++i1;
      }
      while (i2 < maxMarketDepth && it2 != c2.rend()) {
        output.insert(std::make_pair(it2->first, "0"));
        ++it2;
        ++i2;
      }
      return output;
    } else {
      auto it1 = c1.begin();
      int i1 = 0;
      auto it2 = c2.begin();
      int i2 = 0;
      std::map<Decimal, std::string> output;
      while (i1 < maxMarketDepth && i2 < maxMarketDepth && it1 != c1.end() && it2 != c2.end()) {
        if (it1->first < it2->first) {
          output.insert(std::make_pair(it1->first, it1->second));
          ++it1;
          ++i1;
        } else if (it1->first > it2->first) {
          output.insert(std::make_pair(it2->first, "0"));
          ++it2;
          ++i2;
        } else {
          if (it1->second != it2->second) {
            output.insert(std::make_pair(it1->first, it1->second));
          }
          ++it1;
          ++i1;
          ++it2;
          ++i2;
        }
      }
      while (i1 < maxMarketDepth && it1 != c1.end()) {
        output.insert(std::make_pair(it1->first, it1->second));
        ++it1;
        ++i1;
      }
      while (i2 < maxMarketDepth && it2 != c2.end()) {
        output.insert(std::make_pair(it2->first, "0"));
        ++it2;
        ++i2;
      }
      return output;
    }
  }
  void updateElementListWithUpdateMarketDepth(const std::string& field, const std::map<std::string, std::string>& optionMap,
                                              const std::map<Decimal, std::string>& snapshotBid, const std::map<Decimal, std::string>& snapshotBidPrevious,
                                              const std::map<Decimal, std::string>& snapshotAsk, const std::map<Decimal, std::string>& snapshotAskPrevious,
                                              std::vector<Element>& elementList, bool alwaysUpdate) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    if (field == CCAPI_MARKET_DEPTH) {
      int maxMarketDepth = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
      if (optionMap.at(CCAPI_MARKET_DEPTH_RETURN_UPDATE) == CCAPI_MARKET_DEPTH_RETURN_UPDATE_ENABLE) {
        CCAPI_LOGGER_TRACE("lastNSame = " + toString(lastNSame(snapshotBid, snapshotBidPrevious, maxMarketDepth)));
        CCAPI_LOGGER_TRACE("firstNSame = " + toString(firstNSame(snapshotAsk, snapshotAskPrevious, maxMarketDepth)));
        const std::map<Decimal, std::string>& snapshotBidUpdate = this->calculateMarketDepthUpdate(true, snapshotBid, snapshotBidPrevious, maxMarketDepth);
        for (const auto& x : snapshotBidUpdate) {
          Element element;
          element.insert(CCAPI_BEST_BID_N_PRICE, x.first.toString());
          element.insert(CCAPI_BEST_BID_N_SIZE, x.second);
          elementList.push_back(std::move(element));
        }
        const std::map<Decimal, std::string>& snapshotAskUpdate = this->calculateMarketDepthUpdate(false, snapshotAsk, snapshotAskPrevious, maxMarketDepth);
        for (const auto& x : snapshotAskUpdate) {
          Element element;
          element.insert(CCAPI_BEST_ASK_N_PRICE, x.first.toString());
          element.insert(CCAPI_BEST_ASK_N_SIZE, x.second);
          elementList.push_back(std::move(element));
        }
      } else {
        CCAPI_LOGGER_TRACE("lastNSame = " + toString(lastNSame(snapshotBid, snapshotBidPrevious, maxMarketDepth)));
        CCAPI_LOGGER_TRACE("firstNSame = " + toString(firstNSame(snapshotAsk, snapshotAskPrevious, maxMarketDepth)));
        if (alwaysUpdate || !lastNSame(snapshotBid, snapshotBidPrevious, maxMarketDepth) || !firstNSame(snapshotAsk, snapshotAskPrevious, maxMarketDepth)) {
          int bidIndex = 0;
          for (auto iter = snapshotBid.rbegin(); iter != snapshotBid.rend(); ++iter) {
            if (bidIndex >= maxMarketDepth) {
              break;
            }
            Element element;
            element.insert(CCAPI_BEST_BID_N_PRICE, iter->first.toString());
            element.insert(CCAPI_BEST_BID_N_SIZE, iter->second);
            elementList.push_back(std::move(element));
            ++bidIndex;
          }
          if (snapshotBid.empty()) {
            Element element;
            element.insert(CCAPI_BEST_BID_N_PRICE, CCAPI_BEST_BID_N_PRICE_EMPTY);
            element.insert(CCAPI_BEST_BID_N_SIZE, CCAPI_BEST_BID_N_SIZE_EMPTY);
            elementList.push_back(std::move(element));
          }
          int askIndex = 0;
          for (auto iter = snapshotAsk.begin(); iter != snapshotAsk.end(); ++iter) {
            if (askIndex >= maxMarketDepth) {
              break;
            }
            Element element;
            element.insert(CCAPI_BEST_ASK_N_PRICE, iter->first.toString());
            element.insert(CCAPI_BEST_ASK_N_SIZE, iter->second);
            elementList.push_back(std::move(element));
            ++askIndex;
          }
          if (snapshotAsk.empty()) {
            Element element;
            element.insert(CCAPI_BEST_ASK_N_PRICE, CCAPI_BEST_ASK_N_PRICE_EMPTY);
            element.insert(CCAPI_BEST_ASK_N_SIZE, CCAPI_BEST_ASK_N_SIZE_EMPTY);
            elementList.push_back(std::move(element));
          }
        }
      }
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void updateElementListWithTrade(const std::string& field, const MarketDataMessage::TypeForData& input, std::vector<Element>& elementList) {
    if (field == CCAPI_TRADE) {
      for (const auto& x : input) {
        auto type = x.first;
        auto detail = x.second;
        if (type == MarketDataMessage::DataType::TRADE) {
          for (const auto& y : detail) {
            auto price = y.at(MarketDataMessage::DataFieldType::PRICE);
            auto size = y.at(MarketDataMessage::DataFieldType::SIZE);
            Element element;
            element.insert(CCAPI_LAST_PRICE, y.at(MarketDataMessage::DataFieldType::PRICE));
            element.insert(CCAPI_LAST_SIZE, y.at(MarketDataMessage::DataFieldType::SIZE));
            auto it = y.find(MarketDataMessage::DataFieldType::TRADE_ID);
            if (it != y.end()) {
              element.insert(CCAPI_TRADE_ID, it->second);
            }
            element.insert(CCAPI_IS_BUYER_MAKER, y.at(MarketDataMessage::DataFieldType::IS_BUYER_MAKER));
            elementList.push_back(std::move(element));
          }
        } else {
          CCAPI_LOGGER_WARN("extra type " + MarketDataMessage::dataTypeToString(type));
        }
      }
    }
  }
  void updateElementListWithOhlc(const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, const std::string& field,
                                 std::vector<Element>& elementList) {
    if (field == CCAPI_TRADE) {
      Element element;
      if (this->openByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].empty()) {
        element.insert(CCAPI_OPEN, CCAPI_OHLC_EMPTY);
        element.insert(CCAPI_HIGH, CCAPI_OHLC_EMPTY);
        element.insert(CCAPI_LOW, CCAPI_OHLC_EMPTY);
        element.insert(CCAPI_CLOSE, CCAPI_OHLC_EMPTY);
      } else {
        element.insert(CCAPI_OPEN, this->openByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]);
        element.insert(CCAPI_HIGH, this->highByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].toString());
        element.insert(CCAPI_LOW, this->lowByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].toString());
        element.insert(CCAPI_CLOSE, this->closeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]);
      }
      elementList.push_back(std::move(element));
      this->openByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = "";
      this->highByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = Decimal();
      this->lowByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = Decimal();
      this->closeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = "";
    }
  }
  void prepareConnect(WsConnection& wsConnection) {
    if (this->exchangeName == CCAPI_EXCHANGE_NAME_KUCOIN) {
      auto hostPort = this->extractHostFromUrl(CCAPI_KUCOIN_URL_REST_BASE);
      std::string host = hostPort.first;
      std::string port = hostPort.second;
      http::request<http::string_body> req;
      req.set(http::field::host, host + ":" + port);
      req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
      req.set(beast::http::field::content_type, "application/json");
      req.method(http::verb::post);
      req.target("/api/v1/bullet-public");
      this->sendRequest(
          host, port, req,
          [wsConnection, that = shared_from_base<MarketDataService>()](const beast::error_code& ec) {
            WsConnection thisWsConnection = wsConnection;
            that->onFail_(thisWsConnection);
          },
          [wsConnection, that = shared_from_base<MarketDataService>()](const http::response<http::string_body>& res) {
            WsConnection thisWsConnection = wsConnection;
            int statusCode = res.result_int();
            std::string body = res.body();
            if (statusCode / 100 == 2) {
              std::string urlWebsocketBase;
              try {
                rj::Document document;
                document.Parse(body.c_str());
                const rj::Value& instanceServer = document["data"]["instanceServers"][0];
                urlWebsocketBase += std::string(instanceServer["endpoint"].GetString());
                urlWebsocketBase += "?token=";
                urlWebsocketBase += std::string(document["data"]["token"].GetString());
                thisWsConnection.url = urlWebsocketBase;
                that->connect(thisWsConnection);
                // that->wsConnectionMap.insert(std::pair<std::string, WsConnection>(thisWsConnection.id, thisWsConnection));
                // that->instrumentGroupByWsConnectionIdMap.insert(std::pair<std::string, std::string>(thisWsConnection.id,
                // thisWsConnection.group));
                for (const auto& subscription : thisWsConnection.subscriptionList) {
                  auto instrument = subscription.getInstrument();
                  that->subscriptionStatusByInstrumentGroupInstrumentMap[thisWsConnection.group][instrument] = Subscription::Status::SUBSCRIBING;
                }
                that->extraPropertyByConnectionIdMap[thisWsConnection.id].insert({{"pingInterval", std::to_string(instanceServer["pingInterval"].GetInt())},
                                                                                  {"pingTimeout", std::to_string(instanceServer["pingTimeout"].GetInt())}});
                CCAPI_LOGGER_TRACE("that->extraPropertyByConnectionIdMap = " + toString(that->extraPropertyByConnectionIdMap));
                return;
              } catch (const std::runtime_error& e) {
                CCAPI_LOGGER_ERROR(std::string("e.what() = ") + e.what());
              }
            }
            that->onFail_(thisWsConnection);
          },
          this->sessionOptions.httpRequestTimeoutMilliSeconds);
    } else {
      this->connect(wsConnection);
      // this->wsConnectionMap.insert(std::pair<std::string, WsConnection>(wsConnection.id, wsConnection));
      // this->instrumentGroupByWsConnectionIdMap.insert(std::pair<std::string, std::string>(wsConnection.id,
      // wsConnection.group));
    }
  }
  void connect(WsConnection& wsConnection) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    wsConnection.status = WsConnection::Status::CONNECTING;
    CCAPI_LOGGER_DEBUG("connection initialization on dummy id " + wsConnection.id);
    std::string url = wsConnection.url;
    this->serviceContextPtr->tlsClientPtr->set_tls_init_handler(
        std::bind(&MarketDataService::onTlsInit, shared_from_base<MarketDataService>(), std::placeholders::_1));
    CCAPI_LOGGER_DEBUG("endpoint tls init handler set");
    ErrorCode ec;
    TlsClient::connection_ptr con = this->serviceContextPtr->tlsClientPtr->get_connection(url, ec);
    wsConnection.id = this->connectionAddressToString(con);
    CCAPI_LOGGER_DEBUG("connection initialization on actual id " + wsConnection.id);
    if (ec) {
      CCAPI_LOGGER_FATAL("connection initialization error: " + ec.message());
    }
    this->wsConnectionMap.insert(std::pair<std::string, WsConnection>(wsConnection.id, wsConnection));
    CCAPI_LOGGER_DEBUG("this->wsConnectionMap = " + toString(this->wsConnectionMap));
    this->instrumentGroupByWsConnectionIdMap.insert(std::pair<std::string, std::string>(wsConnection.id, wsConnection.group));
    CCAPI_LOGGER_DEBUG("this->instrumentGroupByWsConnectionIdMap = " + toString(this->instrumentGroupByWsConnectionIdMap));
    con->set_open_handler(std::bind(&MarketDataService::onOpen, shared_from_base<MarketDataService>(), std::placeholders::_1));
    con->set_fail_handler(std::bind(&MarketDataService::onFail, shared_from_base<MarketDataService>(), std::placeholders::_1));
    con->set_close_handler(std::bind(&MarketDataService::onClose, shared_from_base<MarketDataService>(), std::placeholders::_1));
    con->set_message_handler(std::bind(&MarketDataService::onMessage, shared_from_base<MarketDataService>(), std::placeholders::_1, std::placeholders::_2));
    if (this->sessionOptions.enableCheckPingPongWebsocketProtocolLevel) {
      con->set_pong_handler(std::bind(&MarketDataService::onPong, shared_from_base<MarketDataService>(), std::placeholders::_1, std::placeholders::_2));
    }
    con->set_ping_handler(std::bind(&MarketDataService::onPing, shared_from_base<MarketDataService>(), std::placeholders::_1, std::placeholders::_2));
    this->serviceContextPtr->tlsClientPtr->connect(con);
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  // void close(WsConnection& wsConnection, wspp::connection_hdl hdl, wspp::close::status::value const code, std::string const& reason, ErrorCode& ec) {
  //   if (wsConnection.status == WsConnection::Status::CLOSING) {
  //     CCAPI_LOGGER_WARN("websocket connection is already in the state of closing");
  //     return;
  //   }
  //   wsConnection.status = WsConnection::Status::CLOSING;
  //   this->serviceContextPtr->tlsClientPtr->close(hdl, code, reason, ec);
  // }
  void send(wspp::connection_hdl hdl, std::string const& payload, wspp::frame::opcode::value op, ErrorCode& ec) {
    this->serviceContextPtr->tlsClientPtr->send(hdl, payload, op, ec);
  }
  void ping(wspp::connection_hdl hdl, std::string const& payload, ErrorCode& ec) { this->serviceContextPtr->tlsClientPtr->ping(hdl, payload, ec); }
  virtual void pingOnApplicationLevel(wspp::connection_hdl hdl, ErrorCode& ec) {}
  void copySnapshot(bool isBid, const std::map<Decimal, std::string>& original, std::map<Decimal, std::string>& copy, const int maxMarketDepth) {
    size_t nToCopy = std::min(original.size(), static_cast<size_t>(maxMarketDepth));
    if (isBid) {
      std::copy_n(original.rbegin(), nToCopy, std::inserter(copy, copy.end()));
    } else {
      std::copy_n(original.begin(), nToCopy, std::inserter(copy, copy.end()));
    }
  }
  void processInitialSnapshot(const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, Event& event, bool& shouldEmitEvent,
                              const TimePoint& tp, const TimePoint& timeReceived, const MarketDataMessage::TypeForData& input, const std::string& field,
                              const std::map<std::string, std::string>& optionMap, const std::vector<std::string>& correlationIdList,
                              std::map<Decimal, std::string>& snapshotBid, std::map<Decimal, std::string>& snapshotAsk) {
    snapshotBid.clear();
    snapshotAsk.clear();
    int maxMarketDepth = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
    for (const auto& x : input) {
      auto type = x.first;
      auto detail = x.second;
      if (type == MarketDataMessage::DataType::BID) {
        for (const auto& y : detail) {
          auto price = y.at(MarketDataMessage::DataFieldType::PRICE);
          auto size = y.at(MarketDataMessage::DataFieldType::SIZE);
          snapshotBid.insert(std::pair<Decimal, std::string>(Decimal(price), size));
        }
        CCAPI_LOGGER_TRACE("lastNToString(snapshotBid, " + toString(maxMarketDepth) + ") = " + lastNToString(snapshotBid, maxMarketDepth));
      } else if (type == MarketDataMessage::DataType::ASK) {
        for (const auto& y : detail) {
          auto price = y.at(MarketDataMessage::DataFieldType::PRICE);
          auto size = y.at(MarketDataMessage::DataFieldType::SIZE);
          snapshotAsk.insert(std::pair<Decimal, std::string>(Decimal(price), size));
        }
        CCAPI_LOGGER_TRACE("firstNToString(snapshotAsk, " + toString(maxMarketDepth) + ") = " + firstNToString(snapshotAsk, maxMarketDepth));
      } else {
        CCAPI_LOGGER_WARN("extra type " + MarketDataMessage::dataTypeToString(type));
      }
    }
    std::vector<Element> elementList;
    this->updateElementListWithInitialMarketDepth(field, optionMap, snapshotBid, snapshotAsk, elementList);
    if (!elementList.empty()) {
      Message message;
      message.setTimeReceived(timeReceived);
      message.setType(Message::Type::MARKET_DATA_EVENTS);
      message.setRecapType(Message::RecapType::SOLICITED);
      message.setTime(tp);
      message.setElementList(elementList);
      message.setCorrelationIdList(correlationIdList);
      std::vector<Message> newMessageList = {message};
      event.addMessages(newMessageList);
      CCAPI_LOGGER_TRACE("event.getMessageList() = " + toString(event.getMessageList()));
    }
    this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = true;
    bool shouldConflate = optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS) != CCAPI_CONFLATE_INTERVAL_MILLISECONDS_DEFAULT;
    if (shouldConflate) {
      this->copySnapshot(true, snapshotBid, this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId],
                         maxMarketDepth);
      this->copySnapshot(false, snapshotAsk, this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId],
                         maxMarketDepth);
      CCAPI_LOGGER_TRACE(
          "this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at("
          "symbolId) = " +
          toString(this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId)));
      CCAPI_LOGGER_TRACE(
          "this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at("
          "symbolId) = " +
          toString(this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId)));
      TimePoint previousConflateTp = UtilTime::makeTimePointFromMilliseconds(
          std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count() / std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)) *
          std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)));
      this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = previousConflateTp;
      if (optionMap.at(CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS) != CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS_DEFAULT) {
        auto interval = std::chrono::milliseconds(std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)));
        auto gracePeriod = std::chrono::milliseconds(std::stoi(optionMap.at(CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS)));
        this->setConflateTimer(previousConflateTp, interval, gracePeriod, wsConnection, channelId, symbolId, field, optionMap, correlationIdList);
      }
    }
  }
  void processUpdateSnapshot(const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, Event& event, bool& shouldEmitEvent,
                             const TimePoint& tp, const TimePoint& timeReceived, const MarketDataMessage::TypeForData& input, const std::string& field,
                             const std::map<std::string, std::string>& optionMap, const std::vector<std::string>& correlationIdList,
                             std::map<Decimal, std::string>& snapshotBid, std::map<Decimal, std::string>& snapshotAsk) {
    CCAPI_LOGGER_TRACE("input = " + MarketDataMessage::dataToString(input));
    if (this->processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
      std::vector<Message> messageList;
      CCAPI_LOGGER_TRACE("optionMap = " + toString(optionMap));
      int maxMarketDepth = std::stoi(optionMap.at(CCAPI_MARKET_DEPTH_MAX));
      std::map<Decimal, std::string> snapshotBidPrevious;
      this->copySnapshot(true, snapshotBid, snapshotBidPrevious, maxMarketDepth);
      std::map<Decimal, std::string> snapshotAskPrevious;
      this->copySnapshot(false, snapshotAsk, snapshotAskPrevious, maxMarketDepth);
      CCAPI_LOGGER_TRACE("before updating orderbook");
      CCAPI_LOGGER_TRACE("lastNToString(snapshotBid, " + toString(maxMarketDepth) + ") = " + lastNToString(snapshotBid, maxMarketDepth));
      CCAPI_LOGGER_TRACE("firstNToString(snapshotAsk, " + toString(maxMarketDepth) + ") = " + firstNToString(snapshotAsk, maxMarketDepth));
      if (this->l2UpdateIsReplaceByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
        CCAPI_LOGGER_TRACE("l2Update is replace");
        if (input.find(MarketDataMessage::DataType::BID) != input.end()) {
          snapshotBid.clear();
        }
        if (input.find(MarketDataMessage::DataType::ASK) != input.end()) {
          snapshotAsk.clear();
        }
      }
      for (const auto& x : input) {
        auto type = x.first;
        auto detail = x.second;
        if (type == MarketDataMessage::DataType::BID) {
          for (const auto& y : detail) {
            auto price = y.at(MarketDataMessage::DataFieldType::PRICE);
            auto size = y.at(MarketDataMessage::DataFieldType::SIZE);
            this->updateOrderBook(snapshotBid, Decimal(price), size);
          }
        } else if (type == MarketDataMessage::DataType::ASK) {
          for (const auto& y : detail) {
            auto price = y.at(MarketDataMessage::DataFieldType::PRICE);
            auto size = y.at(MarketDataMessage::DataFieldType::SIZE);
            this->updateOrderBook(snapshotAsk, Decimal(price), size);
          }
        } else {
          CCAPI_LOGGER_WARN("extra type " + MarketDataMessage::dataTypeToString(type));
        }
      }
      CCAPI_LOGGER_TRACE("this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap = " +
                         toString(this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap));
      if (this->shouldAlignSnapshot) {
        int marketDepthSubscribedToExchange =
            this->marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
        this->alignSnapshot(snapshotBid, snapshotAsk, marketDepthSubscribedToExchange);
      }
      CCAPI_LOGGER_TRACE("afer updating orderbook");
      CCAPI_LOGGER_TRACE("lastNToString(snapshotBid, " + toString(maxMarketDepth) + ") = " + lastNToString(snapshotBid, maxMarketDepth));
      CCAPI_LOGGER_TRACE("firstNToString(snapshotAsk, " + toString(maxMarketDepth) + ") = " + firstNToString(snapshotAsk, maxMarketDepth));
      CCAPI_LOGGER_TRACE("lastNToString(snapshotBidPrevious, " + toString(maxMarketDepth) + ") = " + lastNToString(snapshotBidPrevious, maxMarketDepth));
      CCAPI_LOGGER_TRACE("firstNToString(snapshotAskPrevious, " + toString(maxMarketDepth) + ") = " + firstNToString(snapshotAskPrevious, maxMarketDepth));
      CCAPI_LOGGER_TRACE("field = " + toString(field));
      CCAPI_LOGGER_TRACE("maxMarketDepth = " + toString(maxMarketDepth));
      CCAPI_LOGGER_TRACE("optionMap = " + toString(optionMap));
      bool shouldConflate = optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS) != CCAPI_CONFLATE_INTERVAL_MILLISECONDS_DEFAULT;
      CCAPI_LOGGER_TRACE("shouldConflate = " + toString(shouldConflate));
      TimePoint conflateTp =
          shouldConflate ? UtilTime::makeTimePointFromMilliseconds(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count() /
                                                                   std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)) *
                                                                   std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)))
                         : tp;
      CCAPI_LOGGER_TRACE("conflateTp = " + toString(conflateTp));
      bool intervalChanged =
          shouldConflate && conflateTp > this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
      CCAPI_LOGGER_TRACE("intervalChanged = " + toString(intervalChanged));
      if (!shouldConflate || intervalChanged) {
        std::vector<Element> elementList;
        if (shouldConflate && intervalChanged) {
          const std::map<Decimal, std::string>& snapshotBidPreviousPrevious =
              this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
          const std::map<Decimal, std::string>& snapshotAskPreviousPrevious =
              this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
          this->updateElementListWithUpdateMarketDepth(field, optionMap, snapshotBidPrevious, snapshotBidPreviousPrevious, snapshotAskPrevious,
                                                       snapshotAskPreviousPrevious, elementList, false);
          this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) = snapshotBidPrevious;
          this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) = snapshotAskPrevious;
          CCAPI_LOGGER_TRACE(
              "this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at("
              "symbolId) = " +
              toString(this->previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId)));
          CCAPI_LOGGER_TRACE(
              "this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at("
              "symbolId) = " +
              toString(this->previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId)));
        } else {
          this->updateElementListWithUpdateMarketDepth(field, optionMap, snapshotBid, snapshotBidPrevious, snapshotAsk, snapshotAskPrevious, elementList,
                                                       false);
        }
        CCAPI_LOGGER_TRACE("elementList = " + toString(elementList));
        if (!elementList.empty()) {
          Message message;
          message.setTimeReceived(timeReceived);
          message.setType(Message::Type::MARKET_DATA_EVENTS);
          message.setRecapType(Message::RecapType::NONE);
          TimePoint time = shouldConflate ? this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) +
                                                std::chrono::milliseconds(std::stoll(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)))
                                          : conflateTp;
          message.setTime(time);
          message.setElementList(elementList);
          message.setCorrelationIdList(correlationIdList);
          messageList.push_back(std::move(message));
        }
        if (!messageList.empty()) {
          event.addMessages(messageList);
        }
        if (shouldConflate) {
          this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) = conflateTp;
        }
      }
    }
  }
  void processTrade(const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, Event& event, bool& shouldEmitEvent,
                    const TimePoint& tp, const TimePoint& timeReceived, const MarketDataMessage::TypeForData& input, const std::string& field,
                    const std::map<std::string, std::string>& optionMap, const std::vector<std::string>& correlationIdList) {
    CCAPI_LOGGER_TRACE("input = " + MarketDataMessage::dataToString(input));
    CCAPI_LOGGER_TRACE("optionMap = " + toString(optionMap));
    bool shouldConflate = optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS) != CCAPI_CONFLATE_INTERVAL_MILLISECONDS_DEFAULT;
    CCAPI_LOGGER_TRACE("shouldConflate = " + toString(shouldConflate));
    TimePoint conflateTp = shouldConflate
                               ? UtilTime::makeTimePointFromMilliseconds(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count() /
                                                                         std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)) *
                                                                         std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)))
                               : tp;
    CCAPI_LOGGER_TRACE("conflateTp = " + toString(conflateTp));
    if (!this->processedInitialTradeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
      if (shouldConflate) {
        TimePoint previousConflateTp = conflateTp;
        this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = previousConflateTp;
        if (optionMap.at(CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS) != CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS_DEFAULT) {
          auto interval = std::chrono::milliseconds(std::stoi(optionMap.at(CCAPI_CONFLATE_INTERVAL_MILLISECONDS)));
          auto gracePeriod = std::chrono::milliseconds(std::stoi(optionMap.at(CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS)));
          this->setConflateTimer(previousConflateTp, interval, gracePeriod, wsConnection, channelId, symbolId, field, optionMap, correlationIdList);
        }
      }
      this->processedInitialTradeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = true;
    }
    bool intervalChanged =
        shouldConflate && conflateTp > this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId);
    CCAPI_LOGGER_TRACE("intervalChanged = " + toString(intervalChanged));
    if (!shouldConflate || intervalChanged) {
      std::vector<Message> messageList;
      std::vector<Element> elementList;
      if (shouldConflate && intervalChanged) {
        this->updateElementListWithOhlc(wsConnection, channelId, symbolId, field, elementList);
      } else {
        this->updateElementListWithTrade(field, input, elementList);
      }
      CCAPI_LOGGER_TRACE("elementList = " + toString(elementList));
      if (!elementList.empty()) {
        Message message;
        message.setTimeReceived(timeReceived);
        message.setType(Message::Type::MARKET_DATA_EVENTS);
        message.setRecapType(Message::RecapType::NONE);
        TimePoint time =
            shouldConflate ? this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) : conflateTp;
        message.setTime(time);
        message.setElementList(elementList);
        message.setCorrelationIdList(correlationIdList);
        messageList.push_back(std::move(message));
      }
      if (!messageList.empty()) {
        event.addMessages(messageList);
      }
      if (shouldConflate) {
        this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) = conflateTp;
        this->updateOhlc(wsConnection, channelId, symbolId, field, input);
      }
    } else {
      this->updateOhlc(wsConnection, channelId, symbolId, field, input);
    }
  }
  void updateOhlc(const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, const std::string& field,
                  const MarketDataMessage::TypeForData& input) {
    if (field == CCAPI_TRADE) {
      for (const auto& x : input) {
        auto type = x.first;
        auto detail = x.second;
        if (type == MarketDataMessage::DataType::TRADE) {
          for (const auto& y : detail) {
            auto price = y.at(MarketDataMessage::DataFieldType::PRICE);
            if (this->openByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId].empty()) {
              this->openByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = price;
              this->highByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = Decimal(price);
              this->lowByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = Decimal(price);
            } else {
              auto decimalPrice = Decimal(price);
              if (decimalPrice > this->highByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
                this->highByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = decimalPrice;
              }
              if (decimalPrice < this->lowByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]) {
                this->lowByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = decimalPrice;
              }
            }
            this->closeByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = price;
          }
        } else {
          CCAPI_LOGGER_WARN("extra type " + MarketDataMessage::dataTypeToString(type));
        }
      }
    }
  }
  virtual void alignSnapshot(std::map<Decimal, std::string>& snapshotBid, std::map<Decimal, std::string>& snapshotAsk, int marketDepthSubscribedToExchange) {
    CCAPI_LOGGER_TRACE("snapshotBid.size() = " + toString(snapshotBid.size()));
    if (snapshotBid.size() > marketDepthSubscribedToExchange) {
      keepLastN(snapshotBid, marketDepthSubscribedToExchange);
    }
    CCAPI_LOGGER_TRACE("snapshotBid.size() = " + toString(snapshotBid.size()));
    CCAPI_LOGGER_TRACE("snapshotAsk.size() = " + toString(snapshotAsk.size()));
    if (snapshotAsk.size() > marketDepthSubscribedToExchange) {
      keepFirstN(snapshotAsk, marketDepthSubscribedToExchange);
    }
    CCAPI_LOGGER_TRACE("snapshotAsk.size() = " + toString(snapshotAsk.size()));
  }
  // WsConnection& getWsConnectionFromConnectionPtr(TlsClient::connection_ptr connectionPtr) {
  //   return this->wsConnectionMap.at(this->connectionAddressToString(connectionPtr));
  // }
  // std::string connectionAddressToString(const TlsClient::connection_ptr con) {
  //   const void* address = static_cast<const void*>(con.get());
  //   std::stringstream ss;
  //   ss << address;
  //   return ss.str();
  // }
  void setPingPongTimer(PingPongMethod method, WsConnection& wsConnection, wspp::connection_hdl hdl,
                        std::function<void(wspp::connection_hdl, ErrorCode&)> pingMethod) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    CCAPI_LOGGER_TRACE("method = " + pingPongMethodToString(method));
    auto pingIntervalMilliSeconds = this->pingIntervalMilliSecondsByMethodMap[method];
    auto pongTimeoutMilliSeconds = this->pongTimeoutMilliSecondsByMethodMap[method];
    CCAPI_LOGGER_TRACE("pingIntervalMilliSeconds = " + toString(pingIntervalMilliSeconds));
    CCAPI_LOGGER_TRACE("pongTimeoutMilliSeconds = " + toString(pongTimeoutMilliSeconds));
    if (pingIntervalMilliSeconds <= pongTimeoutMilliSeconds) {
      return;
    }
    if (wsConnection.status == WsConnection::Status::OPEN) {
      if (this->pingTimerByMethodByConnectionIdMap.find(wsConnection.id) != this->pingTimerByMethodByConnectionIdMap.end() &&
          this->pingTimerByMethodByConnectionIdMap.at(wsConnection.id).find(method) != this->pingTimerByMethodByConnectionIdMap.at(wsConnection.id).end()) {
        this->pingTimerByMethodByConnectionIdMap.at(wsConnection.id).at(method)->cancel();
      }
      this->pingTimerByMethodByConnectionIdMap[wsConnection.id][method] = this->serviceContextPtr->tlsClientPtr->set_timer(
          pingIntervalMilliSeconds - pongTimeoutMilliSeconds,
          [wsConnection, that = shared_from_base<MarketDataService>(), hdl, pingMethod, pongTimeoutMilliSeconds, method](ErrorCode const& ec) {
            if (that->wsConnectionMap.find(wsConnection.id) != that->wsConnectionMap.end()) {
              if (ec) {
                CCAPI_LOGGER_ERROR("wsConnection = " + toString(wsConnection) + ", ping timer error: " + ec.message());
                that->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, ec, "timer");
              } else {
                if (that->wsConnectionMap.at(wsConnection.id).status == WsConnection::Status::OPEN) {
                  ErrorCode ec;
                  pingMethod(hdl, ec);
                  if (ec) {
                    that->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, ec, "ping");
                  }
                  if (pongTimeoutMilliSeconds <= 0) {
                    return;
                  }
                  if (that->pongTimeOutTimerByMethodByConnectionIdMap.find(wsConnection.id) != that->pongTimeOutTimerByMethodByConnectionIdMap.end() &&
                      that->pongTimeOutTimerByMethodByConnectionIdMap.at(wsConnection.id).find(method) !=
                          that->pongTimeOutTimerByMethodByConnectionIdMap.at(wsConnection.id).end()) {
                    that->pongTimeOutTimerByMethodByConnectionIdMap.at(wsConnection.id).at(method)->cancel();
                  }
                  that->pongTimeOutTimerByMethodByConnectionIdMap[wsConnection.id][method] = that->serviceContextPtr->tlsClientPtr->set_timer(
                      pongTimeoutMilliSeconds, [wsConnection, that, hdl, pingMethod, pongTimeoutMilliSeconds, method](ErrorCode const& ec) {
                        if (that->wsConnectionMap.find(wsConnection.id) != that->wsConnectionMap.end()) {
                          if (ec) {
                            CCAPI_LOGGER_ERROR("wsConnection = " + toString(wsConnection) + ", pong time out timer error: " + ec.message());
                            that->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, ec, "timer");
                          } else {
                            if (that->wsConnectionMap.at(wsConnection.id).status == WsConnection::Status::OPEN) {
                              auto now = UtilTime::now();
                              if (that->lastPongTpByMethodByConnectionIdMap.find(wsConnection.id) != that->lastPongTpByMethodByConnectionIdMap.end() &&
                                  that->lastPongTpByMethodByConnectionIdMap.at(wsConnection.id).find(method) !=
                                      that->lastPongTpByMethodByConnectionIdMap.at(wsConnection.id).end() &&
                                  std::chrono::duration_cast<std::chrono::milliseconds>(
                                      now - that->lastPongTpByMethodByConnectionIdMap.at(wsConnection.id).at(method))
                                          .count() >= pongTimeoutMilliSeconds) {
                                auto thisWsConnection = wsConnection;
                                ErrorCode ec;
                                that->close(thisWsConnection, hdl, websocketpp::close::status::normal, "pong timeout", ec);
                                if (ec) {
                                  that->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, ec, "shutdown");
                                }
                                that->shouldProcessRemainingMessageOnClosingByConnectionIdMap[thisWsConnection.id] = true;
                              } else {
                                auto thisWsConnection = wsConnection;
                                that->setPingPongTimer(method, thisWsConnection, hdl, pingMethod);
                              }
                            }
                          }
                        }
                      });
                }
              }
            }
          });
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  virtual bool checkOrderBookChecksum(const std::map<Decimal, std::string>& snapshotBid, const std::map<Decimal, std::string>& snapshotAsk,
                                      const std::string& receivedOrderBookChecksumStr, bool& shouldProcessRemainingMessage) {
    return true;
  }
  virtual bool checkOrderBookCrossed(const std::map<Decimal, std::string>& snapshotBid, const std::map<Decimal, std::string>& snapshotAsk,
                                     bool& shouldProcessRemainingMessage) {
    if (this->sessionOptions.enableCheckOrderBookCrossed) {
      auto i1 = snapshotBid.rbegin();
      auto i2 = snapshotAsk.begin();
      if (i1 != snapshotBid.rend() && i2 != snapshotAsk.end()) {
        auto bid = i1->first;
        auto ask = i2->first;
        if (bid >= ask) {
          CCAPI_LOGGER_ERROR("bid = " + toString(bid));
          CCAPI_LOGGER_ERROR("ask = " + toString(ask));
          shouldProcessRemainingMessage = false;
          return false;
        }
      }
    }
    return true;
  }
  virtual void onIncorrectStatesFound(WsConnection& wsConnection, wspp::connection_hdl hdl, const std::string& textMessage, const TimePoint& timeReceived,
                                      const std::string& exchangeSubscriptionId, std::string const& reason) {
    std::string errorMessage = "incorrect states found: connection = " + toString(wsConnection) + ", textMessage = " + textMessage +
                               ", timeReceived = " + UtilTime::getISOTimestamp(timeReceived) + ", exchangeSubscriptionId = " + exchangeSubscriptionId +
                               ", reason = " + reason;
    CCAPI_LOGGER_ERROR(errorMessage);
    ErrorCode ec;
    this->close(wsConnection, hdl, websocketpp::close::status::normal, "incorrect states found: " + reason, ec);
    if (ec) {
      this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, "shutdown");
    }
    this->shouldProcessRemainingMessageOnClosingByConnectionIdMap[wsConnection.id] = false;
    this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::INCORRECT_STATE_FOUND, errorMessage);
  }
  int calculateMarketDepthSubscribedToExchange(int depthWanted, std::vector<int> availableMarketDepth) {
    int i = ceilSearch(availableMarketDepth, 0, availableMarketDepth.size(), depthWanted);
    if (i < 0) {
      i = availableMarketDepth.size() - 1;
    }
    return availableMarketDepth[i];
  }
  void setConflateTimer(const TimePoint& previousConflateTp, const std::chrono::milliseconds& interval, const std::chrono::milliseconds& gracePeriod,
                        const WsConnection& wsConnection, const std::string& channelId, const std::string& symbolId, const std::string& field,
                        const std::map<std::string, std::string>& optionMap, const std::vector<std::string>& correlationIdList) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    if (wsConnection.status == WsConnection::Status::OPEN) {
      if (this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.find(wsConnection.id) != this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap.end() &&
          this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id].find(channelId) !=
              this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id].end() &&
          this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId].find(symbolId) !=
              this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId].end()) {
        this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId]->cancel();
      }
      long waitMilliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(previousConflateTp + interval + gracePeriod - std::chrono::system_clock::now()).count();
      if (waitMilliseconds > 0) {
        this->conflateTimerMapByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId] = this->serviceContextPtr->tlsClientPtr->set_timer(
            waitMilliseconds,
            [wsConnection, channelId, symbolId, field, optionMap, correlationIdList, previousConflateTp, interval, gracePeriod, this](ErrorCode const& ec) {
              if (this->wsConnectionMap.find(wsConnection.id) != this->wsConnectionMap.end()) {
                if (ec) {
                  CCAPI_LOGGER_ERROR("wsConnection = " + toString(wsConnection) + ", conflate timer error: " + ec.message());
                  this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::GENERIC_ERROR, ec, "timer");
                } else {
                  if (this->wsConnectionMap.at(wsConnection.id).status == WsConnection::Status::OPEN) {
                    auto conflateTp = previousConflateTp + interval;
                    if (conflateTp > this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId)) {
                      Event event;
                      event.setType(Event::Type::SUBSCRIPTION_DATA);
                      std::vector<Element> elementList;
                      if (field == CCAPI_MARKET_DEPTH) {
                        std::map<Decimal, std::string>& snapshotBid = this->snapshotBidByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId];
                        std::map<Decimal, std::string>& snapshotAsk = this->snapshotAskByConnectionIdChannelIdSymbolIdMap[wsConnection.id][channelId][symbolId];
                        this->updateElementListWithUpdateMarketDepth(field, optionMap, snapshotBid, std::map<Decimal, std::string>(), snapshotAsk,
                                                                     std::map<Decimal, std::string>(), elementList, true);
                      } else if (field == CCAPI_TRADE) {
                        this->updateElementListWithOhlc(wsConnection, channelId, symbolId, field, elementList);
                      }
                      CCAPI_LOGGER_TRACE("elementList = " + toString(elementList));
                      this->previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap.at(wsConnection.id).at(channelId).at(symbolId) = conflateTp;
                      std::vector<Message> messageList;
                      if (!elementList.empty()) {
                        Message message;
                        message.setTimeReceived(conflateTp);
                        message.setType(Message::Type::MARKET_DATA_EVENTS);
                        message.setRecapType(Message::RecapType::NONE);
                        message.setTime(field == CCAPI_MARKET_DEPTH ? conflateTp : previousConflateTp);
                        message.setElementList(elementList);
                        message.setCorrelationIdList(correlationIdList);
                        messageList.push_back(std::move(message));
                      }
                      if (!messageList.empty()) {
                        event.addMessages(messageList);
                        this->eventHandler(event);
                      }
                    }
                    auto now = UtilTime::now();
                    while (conflateTp + interval + gracePeriod <= now) {
                      conflateTp += interval;
                    }
                    this->setConflateTimer(conflateTp, interval, gracePeriod, wsConnection, channelId, symbolId, field, optionMap, correlationIdList);
                  }
                }
              }
            });
      }
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  virtual void subscribeToExchange(const WsConnection& wsConnection) {
    CCAPI_LOGGER_INFO("exchange is " + this->exchangeName);
    std::vector<std::string> requestStringList = this->createRequestStringList(wsConnection);
    for (const auto& requestString : requestStringList) {
      CCAPI_LOGGER_INFO("requestString = " + requestString);
      ErrorCode ec;
      this->send(wsConnection.hdl, requestString, wspp::frame::opcode::text, ec);
      if (ec) {
        this->onError(Event::Type::SUBSCRIPTION_STATUS, Message::Type::SUBSCRIPTION_FAILURE, ec, "subscribe");
      }
    }
  }
  void processSuccessfulTextMessage(const Request& request, const std::string& textMessage, const TimePoint& timeReceived) override {
    CCAPI_LOGGER_FUNCTION_ENTER;
    const std::vector<MarketDataMessage>& marketDataMessageList = this->convertTextMessageToMarketDataMessage(request, textMessage, timeReceived);
    CCAPI_LOGGER_TRACE("marketDataMessageList = " + toString(marketDataMessageList));
    if (!marketDataMessageList.empty()) {
      Event event;
      event.setType(Event::Type::RESPONSE);
      for (auto const& marketDataMessage : marketDataMessageList) {
        if (marketDataMessage.type == MarketDataMessage::Type::MARKET_DATA_EVENTS) {
          std::vector<std::string> correlationIdList = {request.getCorrelationId()};
          CCAPI_LOGGER_TRACE("correlationIdList = " + toString(correlationIdList));
          if (marketDataMessage.data.find(MarketDataMessage::DataType::TRADE) != marketDataMessage.data.end()) {
            auto messageType = this->requestOperationToMessageTypeMap.at(request.getOperation());
            this->processTrade(event, marketDataMessage.tp, timeReceived, marketDataMessage.data, correlationIdList, messageType);
          }
        } else {
          CCAPI_LOGGER_WARN("market data event type is unknown!");
        }
      }
      CCAPI_LOGGER_TRACE("event type is " + event.typeToString(event.getType()));
      this->eventHandler(event);
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void processTrade(Event& event, const TimePoint& tp, const TimePoint& timeReceived, const MarketDataMessage::TypeForData& input,
                    const std::vector<std::string>& correlationIdList, Message::Type messageType) {
    std::vector<Message> messageList;
    std::vector<Element> elementList;
    this->updateElementListWithTrade(CCAPI_TRADE, input, elementList);
    CCAPI_LOGGER_TRACE("elementList = " + toString(elementList));
    Message message;
    message.setTimeReceived(timeReceived);
    message.setType(messageType);
    message.setTime(tp);
    message.setElementList(elementList);
    message.setCorrelationIdList(correlationIdList);
    messageList.push_back(std::move(message));
    event.addMessages(messageList);
  }
  void substituteParam(std::string& target, const std::map<std::string, std::string>& param, const std::map<std::string, std::string> standardizationMap = {}) {
    for (const auto& kv : param) {
      auto key = standardizationMap.find(kv.first) != standardizationMap.end() ? standardizationMap.at(kv.first) : kv.first;
      auto value = kv.second;
      target = target.replace(target.find(key), key.length(), value);
    }
  }
  void appendParam(std::string& queryString, const std::map<std::string, std::string>& param,
                   const std::map<std::string, std::string> standardizationMap = {}) {
    int i = 0;
    for (const auto& kv : param) {
      std::string key = standardizationMap.find(kv.first) != standardizationMap.end() ? standardizationMap.at(kv.first) : kv.first;
      queryString += key;
      queryString += "=";
      queryString += Url::urlEncode(kv.second);
      if (i < param.size() - 1) {
        queryString += "&";
      }
      ++i;
    }
  }
  void appendSymbolId(std::string& queryString, const std::string& symbolId, const std::string symbolIdCalled) {
    if (!symbolId.empty()) {
      queryString += symbolIdCalled;
      queryString += "=";
      queryString += Url::urlEncode(symbolId);
      queryString += "&";
    }
  }
  virtual std::vector<MarketDataMessage> convertTextMessageToMarketDataMessage(const Request& request, const std::string& textMessage,
                                                                               const TimePoint& timeReceived) = 0;
  virtual std::vector<std::string> createRequestStringList(const WsConnection& wsConnection) = 0;
  virtual std::vector<MarketDataMessage> processTextMessage(WsConnection& wsConnection, wspp::connection_hdl hdl, const std::string& textMessage,
                                                            const TimePoint& timeReceived) = 0;

  // std::shared_ptr<ServiceContext> serviceContextPtr;
  // std::map<std::string, WsConnection> wsConnectionMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> fieldByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<std::string, std::string>>>> optionMapByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, int>>> marketDepthSubscribedToExchangeByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::vector<Subscription>>>> subscriptionListByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::vector<std::string>>>> correlationIdListByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> channelIdSymbolIdByConnectionIdExchangeSubscriptionIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<Decimal, std::string>>>> snapshotBidByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<Decimal, std::string>>>> snapshotAskByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<Decimal, std::string>>>>
      previousConflateSnapshotBidByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::map<Decimal, std::string>>>>
      previousConflateSnapshotAskByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, bool>>> processedInitialSnapshotByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, bool>>> processedInitialTradeByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, bool>>> l2UpdateIsReplaceByConnectionIdChannelIdSymbolIdMap;
  // std::map<std::string, bool> shouldProcessRemainingMessageOnClosingByConnectionIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, TimePoint>>> previousConflateTimeMapByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, TimerPtr>>> conflateTimerMapByConnectionIdChannelIdSymbolIdMap;
  // std::map<std::string, int> connectNumRetryOnFailByConnectionUrlMap;
  // std::map<std::string, TimerPtr> connectRetryOnFailTimerByConnectionIdMap;
  // std::map<std::string, std::map<PingPongMethod, TimePoint>> lastPongTpByMethodByConnectionIdMap;
  // std::map<std::string, std::map<PingPongMethod, TimerPtr>> pingTimerByMethodByConnectionIdMap;
  // std::map<std::string, std::map<PingPongMethod, TimerPtr>> pongTimeOutTimerByMethodByConnectionIdMap;
  // std::map<PingPongMethod, long> pingIntervalMilliSecondsByMethodMap;
  // std::map<PingPongMethod, long> pongTimeoutMilliSecondsByMethodMap;
  std::map<std::string, std::map<std::string, std::string>> orderBookChecksumByConnectionIdSymbolIdMap;
  bool shouldAlignSnapshot{};
  // SessionOptions sessionOptions;
  // SessionConfigs sessionConfigs;
  // std::function<void(Event& event)> eventHandler;
#if defined(CCAPI_ENABLE_EXCHANGE_HUOBI) || defined(CCAPI_ENABLE_EXCHANGE_HUOBI_USDT_SWAP) || defined(CCAPI_ENABLE_EXCHANGE_OKEX)
  struct monostate {};
  websocketpp::extensions_workaround::permessage_deflate::enabled<monostate> inflater;
#endif
  // std::atomic<bool> shouldContinue{true};
  std::map<std::string, std::map<std::string, Subscription::Status>> subscriptionStatusByInstrumentGroupInstrumentMap;
  std::map<std::string, std::string> instrumentGroupByWsConnectionIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> openByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, Decimal>>> highByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, Decimal>>> lowByConnectionIdChannelIdSymbolIdMap;
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> closeByConnectionIdChannelIdSymbolIdMap;
  // std::map<std::string, std::map<std::string, std::string>> extraPropertyByConnectionIdMap;
  // bool enableCheckPingPongWebsocketProtocolLevel{};
  // bool enableCheckPingPongWebsocketApplicationLevel{};
  std::string getRecentTradesTarget;
  std::map<Request::Operation, Message::Type> requestOperationToMessageTypeMap;
};
} /* namespace ccapi */
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_MARKET_DATA_SERVICE_H_
