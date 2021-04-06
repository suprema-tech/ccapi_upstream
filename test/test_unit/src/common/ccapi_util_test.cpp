#include "ccapi_cpp/ccapi_util_private.h"
#include "gtest/gtest.h"
namespace ccapi {
TEST(UtilAlgorithmTest, base64) {
  std::string original("+xT7GWTDRHi09EZEhkOC8S7ktzngKtoT1ZoZ6QclGURlq3ePfUd7kLQzK4+P54685NEqYDaIerYj9cuYFILOhQ==");
  auto result = UtilAlgorithm::base64Encode(UtilAlgorithm::base64Decode(original));
  EXPECT_EQ(result, original);
}
TEST(UtilAlgorithmTest, hexStringConversion) {
  std::string original("D8E30E653FF472796722CBAA816E6AE4E3B0D0EC530F857E41002B0A9F5B7F00A32BC60017E82EFB2E7A43C3049D16A0");
  auto result = UtilAlgorithm::stringToHex(UtilAlgorithm::hexToString(original));
  EXPECT_EQ(result, original);
}
TEST(UtilAlgorithmTest, base64UrlFromBase64) {
  std::string original("TJVA95OrM7E2cBab30RMHrHDcEfxjoYZgeFONFh7HgQ=");
  auto result = UtilAlgorithm::base64UrlFromBase64(original);
  EXPECT_EQ(result, "TJVA95OrM7E2cBab30RMHrHDcEfxjoYZgeFONFh7HgQ");
}
TEST(UtilAlgorithmTest, base64FromBase64Url) {
  std::string original("TJVA95OrM7E2cBab30RMHrHDcEfxjoYZgeFONFh7HgQ");
  auto result = UtilAlgorithm::base64FromBase64Url(original);
  EXPECT_EQ(result, "TJVA95OrM7E2cBab30RMHrHDcEfxjoYZgeFONFh7HgQ=");
}
} /* namespace ccapi */
