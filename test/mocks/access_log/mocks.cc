#include "test/mocks/access_log/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::Matcher;


namespace Envoy {
namespace AccessLog {

MockAccessLogFile::MockAccessLogFile() = default;
MockAccessLogFile::~MockAccessLogFile() = default;

MockFilter::MockFilter() = default;
MockFilter::~MockFilter() = default;

MockAccessLogManager::MockAccessLogManager() {
  ON_CALL(*this, createAccessLog(Matcher<const std::string&>(_))).WillByDefault(Return(file_));
  ON_CALL(*this, createAccessLog(Matcher<const Envoy::Filesystem::FilePathAndType&>(_))).WillByDefault(Return(file_));
}

MockAccessLogManager::~MockAccessLogManager() = default;

MockInstance::MockInstance() = default;
MockInstance::~MockInstance() = default;

} // namespace AccessLog
} // namespace Envoy
