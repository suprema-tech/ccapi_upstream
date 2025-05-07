#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_SERVICE_CONTEXT_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_SERVICE_CONTEXT_H_

#include "boost/asio/ssl.hpp"
#include "ccapi_cpp/ccapi_logger.h"
namespace ccapi {
/**
 * Defines the service that the service depends on.
 */
class ServiceContext CCAPI_FINAL {
 public:
  typedef boost::asio::io_context IoContext;
  typedef boost::asio::io_context* IoContextPtr;
  typedef boost::asio::executor_work_guard<boost::asio::io_context::executor_type> ExecutorWorkGuard;
  typedef ExecutorWorkGuard* ExecutorWorkGuardPtr;
  typedef boost::asio::ssl::context SslContext;
  typedef SslContext* SslContextPtr;
  ServiceContext() {
    this->ioContextPtr = new boost::asio::io_context();
    this->executorWorkGuardPtr = new ExecutorWorkGuard(this->ioContextPtr->get_executor());
    this->sslContextPtr = new SslContext(SslContext::tls_client);
    // this->sslContextPtr->set_options(SslContext::default_workarounds | SslContext::no_sslv2 | SslContext::no_sslv3 | SslContext::single_dh_use);
    this->sslContextPtr->set_verify_mode(boost::asio::ssl::verify_none);
    // TODO(cryptochassis): verify ssl certificate to strengthen security
    // https://github.com/boostorg/asio/blob/develop/example/cpp03/ssl/client.cpp
  }
#ifndef SWIG
  ServiceContext(IoContextPtr ioContextPtr) {
    this->ioContextPtr = ioContextPtr;
    this->executorWorkGuardPtr = new ExecutorWorkGuard(this->ioContextPtr->get_executor());
    this->sslContextPtr = new SslContext(SslContext::tls_client);
    this->sslContextPtr->set_verify_mode(boost::asio::ssl::verify_none);
  }
  ServiceContext(SslContextPtr sslContextPtr) {
    this->ioContextPtr = new boost::asio::io_context();
    this->executorWorkGuardPtr = new ExecutorWorkGuard(this->ioContextPtr->get_executor());
    this->sslContextPtr = sslContextPtr;
    this->sslContextPtr->set_verify_mode(boost::asio::ssl::verify_none);
  }
  ServiceContext(IoContextPtr ioContextPtr, SslContextPtr sslContextPtr) {
    this->ioContextPtr = ioContextPtr;
    this->executorWorkGuardPtr = new ExecutorWorkGuard(this->ioContextPtr->get_executor());
    this->sslContextPtr = sslContextPtr;
    this->sslContextPtr->set_verify_mode(boost::asio::ssl::verify_none);
  }
#endif
  ServiceContext(const ServiceContext&) = delete;
  ServiceContext& operator=(const ServiceContext&) = delete;
  virtual ~ServiceContext() {
    delete this->executorWorkGuardPtr;
    delete this->ioContextPtr;
    delete this->sslContextPtr;
  }
  void start() {
    CCAPI_LOGGER_INFO("about to start client asio io_context run loop");
    this->ioContextPtr->run();
    CCAPI_LOGGER_INFO("just exited client asio io_context run loop");
  }
  void stop() {
    this->executorWorkGuardPtr->reset();
    this->ioContextPtr->stop();
  }
  IoContextPtr ioContextPtr{nullptr};
  ExecutorWorkGuardPtr executorWorkGuardPtr{nullptr};
  SslContextPtr sslContextPtr{nullptr};
  // IoContextPtr ioContextPtr{new IoContext()};
  // ExecutorWorkGuardPtr executorWorkGuardPtr{new ExecutorWorkGuard(ioContextPtr->get_executor())};
  // SslContextPtr sslContextPtr{new SslContext(SslContext::tls_client)};
};

} /* namespace ccapi */


#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_SERVICE_CONTEXT_H_
