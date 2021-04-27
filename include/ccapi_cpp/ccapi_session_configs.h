#ifndef INCLUDE_CCAPI_CPP_CCAPI_SESSION_CONFIGS_H_
#define INCLUDE_CCAPI_CPP_CCAPI_SESSION_CONFIGS_H_
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ccapi_cpp/ccapi_logger.h"
#include "ccapi_cpp/ccapi_macro.h"
#include "ccapi_cpp/ccapi_util_private.h"
namespace ccapi {
class SessionConfigs CCAPI_FINAL {
 public:
  SessionConfigs() : SessionConfigs({}, {}, {}) {}
  explicit SessionConfigs(std::map<std::string, std::map<std::string, std::string> > exchangeInstrumentSymbolMap,
                          std::map<std::string, std::map<std::string, std::string> > exchangeInstrumentSymbolMapRest = {},
                          std::map<std::string, std::string> credential = {})
      : exchangeInstrumentSymbolMap(exchangeInstrumentSymbolMap), exchangeInstrumentSymbolMapRest(exchangeInstrumentSymbolMapRest), credential(credential) {
    this->exchangeSymbolInstrumentMap = this->invertInstrumentSymbolMap(exchangeInstrumentSymbolMap);
    this->exchangeSymbolInstrumentMapRest = this->invertInstrumentSymbolMap(exchangeInstrumentSymbolMapRest);
    this->updateExchangeInstrumentMap();
    this->updateExchangeInstrumentMapRest();
  }
  const std::map<std::string, std::map<std::string, std::string> >& getExchangeInstrumentSymbolMap() const { return exchangeInstrumentSymbolMap; }
  const std::map<std::string, std::map<std::string, std::string> >& getExchangeInstrumentSymbolMapRest() const { return exchangeInstrumentSymbolMapRest; }
  void setExchangeInstrumentSymbolMap(const std::map<std::string, std::map<std::string, std::string> >& exchangeInstrumentSymbolMap) {
    this->exchangeInstrumentSymbolMap = exchangeInstrumentSymbolMap;
    this->exchangeSymbolInstrumentMap = this->invertInstrumentSymbolMap(exchangeInstrumentSymbolMap);
    this->updateExchangeInstrumentMap();
    this->updateExchangeInstrumentMapRest();
  }
  void setExchangeInstrumentSymbolMapRest(const std::map<std::string, std::map<std::string, std::string> >& exchangeInstrumentSymbolMapRest) {
    this->exchangeInstrumentSymbolMapRest = exchangeInstrumentSymbolMapRest;
    this->exchangeSymbolInstrumentMapRest = this->invertInstrumentSymbolMap(exchangeInstrumentSymbolMapRest);
    this->updateExchangeInstrumentMapRest();
  }
  const std::map<std::string, std::vector<std::string> >& getExchangeInstrumentMap() const { return exchangeInstrumentMap; }
  const std::map<std::string, std::vector<std::string> >& getExchangeInstrumentMapRest() const { return exchangeInstrumentMapRest; }
  const std::map<std::string, std::vector<std::string> >& getExchangeFieldMap() const { return exchangeFieldMap; }
  const std::map<std::string, std::map<std::string, std::string> >& getExchangeFieldWebsocketChannelMap() const { return exchangeFieldWebsocketChannelMap; }
  const std::map<std::string, std::vector<int> >& getWebsocketAvailableMarketDepth() const { return websocketAvailableMarketDepth; }
  const std::map<std::string, std::string>& getUrlWebsocketBase() const { return urlWebsocketBase; }
  const std::map<std::string, std::string>& getUrlRestBase() const { return urlRestBase; }
  const std::map<std::string, int>& getInitialSequenceByExchangeMap() const { return initialSequenceByExchangeMap; }
  const std::map<std::string, std::string>& getCredential() const { return credential; }
  void setCredential(const std::map<std::string, std::string>& credential) { this->credential = credential; }
  const std::map<std::string, std::map<std::string, std::string> >& getExchangeSymbolInstrumentMap() const { return exchangeSymbolInstrumentMap; }
  const std::map<std::string, std::map<std::string, std::string> >& getExchangeSymbolInstrumentMapRest() const { return exchangeSymbolInstrumentMapRest; }

 private:
  void updateExchangeInstrumentMap() {
    for (const auto& x : exchangeInstrumentSymbolMap) {
      for (const auto& y : x.second) {
        this->exchangeInstrumentMap[x.first].push_back(y.first);
      }
    }
    std::map<std::string, std::string> fieldWebsocketChannelMapCoinbase = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_COINBASE_CHANNEL_MATCH},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_COINBASE_CHANNEL_LEVEL2},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapGemini = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_GEMINI_PARAMETER_TRADES},
        {CCAPI_MARKET_DEPTH, std::string(CCAPI_WEBSOCKET_GEMINI_PARAMETER_BIDS) + "," + CCAPI_WEBSOCKET_GEMINI_PARAMETER_OFFERS},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapKraken = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_KRAKEN_CHANNEL_TRADE},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_KRAKEN_CHANNEL_BOOK},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapBitstamp = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_BITSTAMP_CHANNEL_LIVE_TRADES},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_BITSTAMP_CHANNEL_ORDER_BOOK},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapBitfinex = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_BITFINEX_CHANNEL_TRADES},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_BITFINEX_CHANNEL_BOOK},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapBitmex = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_BITMEX_CHANNEL_TRADE},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_BITMEX_CHANNEL_ORDER_BOOK_L2},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapBinanceUs = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_BINANCE_BASE_CHANNEL_TRADE},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_BINANCE_BASE_CHANNEL_PARTIAL_BOOK_DEPTH},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapBinance = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_BINANCE_BASE_CHANNEL_TRADE},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_BINANCE_BASE_CHANNEL_PARTIAL_BOOK_DEPTH},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapBinanceFutures = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_BINANCE_BASE_CHANNEL_AGG_TRADE},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_BINANCE_BASE_CHANNEL_PARTIAL_BOOK_DEPTH},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapHuobi = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_HUOBI_CHANNEL_TRADE_DETAIL},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_HUOBI_CHANNEL_MARKET_DEPTH},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapHuobiUsdtSwap = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_HUOBI_CHANNEL_TRADE_DETAIL},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_HUOBI_CHANNEL_MARKET_DEPTH},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapOkex = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_OKEX_CHANNEL_TRADE},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_OKEX_CHANNEL_PUBLIC_DEPTH400},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapErisx = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_ERISX_CHANNEL_MARKET_DATA_SUBSCRIBE},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_ERISX_CHANNEL_MARKET_DATA_SUBSCRIBE},
    };
    std::map<std::string, std::string> fieldWebsocketChannelMapKucoin = {
        {CCAPI_TRADE, CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_MATCH},
        {CCAPI_MARKET_DEPTH, CCAPI_WEBSOCKET_KUCOIN_CHANNEL_MARKET_LEVEL2},
    };
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapCoinbase) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_COINBASE].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketParameter : fieldWebsocketChannelMapGemini) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_GEMINI].push_back(fieldWebsocketParameter.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapKraken) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_KRAKEN].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapBitstamp) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_BITSTAMP].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapBitfinex) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_BITFINEX].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapBitmex) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_BITMEX].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapBinanceUs) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_BINANCE_US].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapBinance) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_BINANCE].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapBinanceFutures) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_BINANCE_FUTURES].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapHuobi) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_HUOBI].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapHuobiUsdtSwap) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_HUOBI_USDT_SWAP].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapOkex) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_OKEX].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapErisx) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_ERISX].push_back(fieldWebsocketChannel.first);
    }
    for (auto const& fieldWebsocketChannel : fieldWebsocketChannelMapKucoin) {
      this->exchangeFieldMap[CCAPI_EXCHANGE_NAME_KUCOIN].push_back(fieldWebsocketChannel.first);
    }
    CCAPI_LOGGER_TRACE("this->exchangeFieldMap = " + toString(this->exchangeFieldMap));
    this->exchangeFieldWebsocketChannelMap = {{CCAPI_EXCHANGE_NAME_COINBASE, fieldWebsocketChannelMapCoinbase},
                                              {CCAPI_EXCHANGE_NAME_GEMINI, fieldWebsocketChannelMapGemini},
                                              {CCAPI_EXCHANGE_NAME_KRAKEN, fieldWebsocketChannelMapKraken},
                                              {CCAPI_EXCHANGE_NAME_BITSTAMP, fieldWebsocketChannelMapBitstamp},
                                              {CCAPI_EXCHANGE_NAME_BITFINEX, fieldWebsocketChannelMapBitfinex},
                                              {CCAPI_EXCHANGE_NAME_BITMEX, fieldWebsocketChannelMapBitmex},
                                              {CCAPI_EXCHANGE_NAME_BINANCE_US, fieldWebsocketChannelMapBinanceUs},
                                              {CCAPI_EXCHANGE_NAME_BINANCE, fieldWebsocketChannelMapBinance},
                                              {CCAPI_EXCHANGE_NAME_BINANCE_FUTURES, fieldWebsocketChannelMapBinanceFutures},
                                              {CCAPI_EXCHANGE_NAME_HUOBI, fieldWebsocketChannelMapHuobi},
                                              {CCAPI_EXCHANGE_NAME_HUOBI_USDT_SWAP, fieldWebsocketChannelMapHuobiUsdtSwap},
                                              {CCAPI_EXCHANGE_NAME_OKEX, fieldWebsocketChannelMapOkex},
                                              {CCAPI_EXCHANGE_NAME_ERISX, fieldWebsocketChannelMapErisx},
                                              {CCAPI_EXCHANGE_NAME_KUCOIN, fieldWebsocketChannelMapKucoin}};
    this->websocketAvailableMarketDepth = {
        {CCAPI_EXCHANGE_NAME_KRAKEN, std::vector<int>({10, 25, 100, 500, 1000})},
        {CCAPI_EXCHANGE_NAME_BITSTAMP, std::vector<int>({100})},
        {CCAPI_EXCHANGE_NAME_BITFINEX, std::vector<int>({1, 25, 100})},
        {CCAPI_EXCHANGE_NAME_BITMEX, std::vector<int>({1, 10, 25})},
        {CCAPI_EXCHANGE_NAME_BINANCE_US, std::vector<int>({5, 10, 20})},
        {CCAPI_EXCHANGE_NAME_BINANCE, std::vector<int>({5, 10, 20})},
        {CCAPI_EXCHANGE_NAME_BINANCE_FUTURES, std::vector<int>({5, 10, 20})},
        {CCAPI_EXCHANGE_NAME_HUOBI, std::vector<int>({150})},
        {CCAPI_EXCHANGE_NAME_HUOBI_USDT_SWAP, std::vector<int>({150})},
        {CCAPI_EXCHANGE_NAME_OKEX, std::vector<int>({5, 400})},
        {CCAPI_EXCHANGE_NAME_KUCOIN, std::vector<int>({1, 5, 50})},
    };
    this->urlWebsocketBase = {{CCAPI_EXCHANGE_NAME_COINBASE, "wss://ws-feed.pro.coinbase.com"},
                              {CCAPI_EXCHANGE_NAME_GEMINI, "wss://api.gemini.com/v1/marketdata"},
                              {CCAPI_EXCHANGE_NAME_KRAKEN, "wss://ws.kraken.com"},
                              {CCAPI_EXCHANGE_NAME_BITSTAMP, "wss://ws.bitstamp.net"},
                              {CCAPI_EXCHANGE_NAME_BITFINEX, "wss://api-pub.bitfinex.com/ws/2"},
                              {CCAPI_EXCHANGE_NAME_BITMEX, "wss://www.bitmex.com/realtime"},
                              {CCAPI_EXCHANGE_NAME_BINANCE_US, "wss://stream.binance.us:9443/stream"},
                              {CCAPI_EXCHANGE_NAME_BINANCE, "wss://stream.binance.com:9443/stream"},
                              {CCAPI_EXCHANGE_NAME_BINANCE_FUTURES, "wss://fstream.binance.com/stream"},
                              {CCAPI_EXCHANGE_NAME_HUOBI, "wss://api.huobi.pro"},
                              {CCAPI_EXCHANGE_NAME_HUOBI_USDT_SWAP, "wss://api.hbdm.com/linear-swap-ws"},
                              {CCAPI_EXCHANGE_NAME_OKEX, "wss://ws.okex.com:8443/ws/v5"},
                              {CCAPI_EXCHANGE_NAME_ERISX, "wss://publicmd-api.erisx.com"},
                              //  Kucoin has dynamic websocket url. Here it is only a placeholder for subscription grouping purposes.
                              {CCAPI_EXCHANGE_NAME_KUCOIN, "CCAPI_EXCHANGE_NAME_KUCOIN_URL_WEBSOCKET_BASE"}};
    this->initialSequenceByExchangeMap = {{CCAPI_EXCHANGE_NAME_GEMINI, 0}, {CCAPI_EXCHANGE_NAME_BITFINEX, 1}};
  }
  void updateExchangeInstrumentMapRest() {
    for (const auto& x : exchangeInstrumentSymbolMapRest) {
      for (const auto& y : x.second) {
        this->exchangeInstrumentMapRest[x.first].push_back(y.first);
      }
    }
    this->urlRestBase = {
        {CCAPI_EXCHANGE_NAME_COINBASE, CCAPI_COINBASE_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_GEMINI, CCAPI_GEMINI_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_KRAKEN, CCAPI_KRAKEN_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_BITSTAMP, CCAPI_BITSTAMP_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_BITFINEX, CCAPI_BITFINEX_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_BITMEX, CCAPI_BITMEX_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_BINANCE_US, CCAPI_BINANCE_US_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_BINANCE, CCAPI_BINANCE_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_BINANCE_FUTURES, CCAPI_BINANCE_FUTURES_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_HUOBI, CCAPI_HUOBI_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_HUOBI_USDT_SWAP, CCAPI_HUOBI_USDT_SWAP_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_OKEX, CCAPI_OKEX_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_ERISX, CCAPI_ERISX_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_KUCOIN, CCAPI_KUCOIN_URL_REST_BASE},
        {CCAPI_EXCHANGE_NAME_FTX, CCAPI_FTX_URL_REST_BASE},
    };
  }
  std::map<std::string, std::map<std::string, std::string> > invertInstrumentSymbolMap(
      std::map<std::string, std::map<std::string, std::string> > exchangeInstrumentSymbolMap) {
    std::map<std::string, std::map<std::string, std::string> > exchangeSymbolInstrumentMap;
    for (const auto& x : exchangeInstrumentSymbolMap) {
      exchangeSymbolInstrumentMap.insert(std::make_pair(x.first, invertMap(x.second)));
    }
    return exchangeSymbolInstrumentMap;
  }
  std::map<std::string, std::map<std::string, std::string> > exchangeInstrumentSymbolMap;
  std::map<std::string, std::map<std::string, std::string> > exchangeInstrumentSymbolMapRest;
  std::map<std::string, std::map<std::string, std::string> > exchangeSymbolInstrumentMap;
  std::map<std::string, std::map<std::string, std::string> > exchangeSymbolInstrumentMapRest;
  std::map<std::string, std::vector<std::string> > exchangeInstrumentMap;
  std::map<std::string, std::vector<std::string> > exchangeFieldMap;
  std::map<std::string, std::vector<std::string> > exchangeInstrumentMapRest;
  std::map<std::string, std::map<std::string, std::string> > exchangeFieldWebsocketChannelMap;
  std::map<std::string, std::vector<int> > websocketAvailableMarketDepth;
  std::map<std::string, int> websocketMaxAvailableMarketDepth;
  std::map<std::string, std::string> urlWebsocketBase;
  std::map<std::string, std::string> urlRestBase;
  std::map<std::string, int> initialSequenceByExchangeMap;
  std::map<std::string, std::string> credential;
};
} /* namespace ccapi */
#endif  // INCLUDE_CCAPI_CPP_CCAPI_SESSION_CONFIGS_H_
