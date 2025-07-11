#ifndef INCLUDE_CCAPI_CPP_CCAPI_FIX_CONNECTION_H_
#define INCLUDE_CCAPI_CPP_CCAPI_FIX_CONNECTION_H_
#include <string>
#include <variant>
#include "ccapi_cpp/ccapi_logger.h"
#include "ccapi_cpp/ccapi_subscription.h"
namespace beast = boost::beast;

namespace ccapi {

/**
 * This class represents a TCP socket connection for the FIX API.
 */

class FixConnection {
 public:
 FixConnection(const FixConnection&) = delete;
  FixConnection& operator=(const FixConnection&) = delete;

  FixConnection(const std::string& host, const std::string& port, const Subscription& subscription)
      : host(host), port(port), subscription(subscription) {
    this->id = subscription.getCorrelationId();
    this->url = host + ":" + port;
    this->isSecure = host.rfind("tcp+ssl", 0) == 0;
  }

  std::string toString() const {
    std::ostringstream oss;
    std::visit(
        [&oss](auto&& streamPtr) {
          if (streamPtr) {
            oss << streamPtr.get();
          } else {
            oss << "nullptr";
          }
        },
        streamPtr);
    std::string output = "FixConnection [id = " + id + ", host = " + host + ", port = " + port + ", subscription = " + ccapi::toString(subscription) +
                         ", status = " + statusToString(status) + ", streamPtr = " + oss.str() + "]";
    return output;
  }
  enum class Status {
    UNKNOWN,
    CONNECTING,
    OPEN,
    FAILED,
    CLOSING,
    CLOSED,
  };

  static std::string statusToString(Status status) {
    std::string output;
    switch (status) {
      case Status::UNKNOWN:
        output = "UNKNOWN";
        break;
      case Status::CONNECTING:
        output = "CONNECTING";
        break;
      case Status::OPEN:
        output = "OPEN";
        break;
      case Status::FAILED:
        output = "FAILED";
        break;
      case Status::CLOSING:
        output = "CLOSING";
        break;
      case Status::CLOSED:
        output = "CLOSED";
        break;
      default:
        CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
    }
    return output;
  }

  std::string id;
  std::string host;
  std::string port;
  std::string url;
  Subscription subscription;
  Status status{Status::UNKNOWN};
  std::variant<std::shared_ptr<beast::ssl_stream<beast::tcp_stream>>, std::shared_ptr<beast::tcp_stream>> streamPtr;
      bool isSecure{};
};

} /* namespace ccapi */
#endif  // INCLUDE_CCAPI_CPP_CCAPI_FIX_CONNECTION_H_
