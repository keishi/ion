/**
Copyright 2016 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS-IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#include <functional>
#include <iostream>
#include <sstream>
#include <string>

#include "ion/base/logchecker.h"
#include "ion/base/logging.h"
#include "ion/base/nulllogentrywriter.h"
#include "ion/port/fileutils.h"
#include "ion/port/logging.h"
#include "ion/port/timer.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

using ion::port::GetCanonicalFilePath;

namespace {

// Helper class to test base::Logging::CheckMessage and base::SetBreakHandler.
class TestBreakHandlerWrapper {
 public:
  TestBreakHandlerWrapper() : has_been_called_(false) {}
  void HandleBreak() { has_been_called_ = true; }
  bool HasBeenCalled() const { return has_been_called_; }
 private:
  bool has_been_called_;
};

#if !ION_PRODUCTION
// Helper function that builds a line number into a string.
static int LogMessageOnce() {
  LOG_ONCE(INFO) << "This message should be printed once";
  return __LINE__ - 1;
}

static int LogAnotherMessageOnce() {
  LOG_ONCE(INFO) << "This message should also be printed once";
  return __LINE__ - 1;
}

static int LogMessageEverySecond() {
  LOG_EVERY_N_SEC(INFO, 1) << "This message should be printed no more than "
                              "once per second";
  return __LINE__ - 2;
}
#endif

}  // namespace

// Helper function that builds a line number into a string.
static const std::string BuildMessage(const char* severity, int line,
                                      const char* after) {
  std::ostringstream s;
  s << severity << " [" << GetCanonicalFilePath(__FILE__) << ":" << line << "] "
    << after;
  return s.str();
}

TEST(Logging, SetWriter) {
  // Expect the default log-writer to be used before we
  // replace it with our own.
  EXPECT_EQ(ion::base::GetDefaultLogEntryWriter(),
            ion::base::GetLogEntryWriter());

  ion::base::NullLogEntryWriter null_logger;

  EXPECT_EQ(&null_logger, ion::base::GetLogEntryWriter());
}

TEST(Logging, BadSeverity) {
  ion::base::LogChecker checker;

  using ion::base::logging_internal::Logger;
  using ion::port::LogSeverity;

  // Can't use LOG macro because the Severity is not one of the supported ones.
  int severity_as_int = 123;
  const LogSeverity severity = static_cast<LogSeverity>(severity_as_int);
  Logger(__FILE__, __LINE__, severity).GetStream() << "Blah";
  const int line = __LINE__ - 1;
  EXPECT_EQ(BuildMessage("<Unknown severity>", line, "Blah\n"),
            GetCanonicalFilePath(checker.GetLogString()));
  checker.ClearLog();
}

TEST(Logging, CheckMessage) {
  std::string message =
      ion::base::logging_internal::Logger::CheckMessage("check", "expr");
  EXPECT_EQ(0, strcmp("check failed: expression='expr' ", message.c_str()));
}

TEST(Logging, NullLogger) {
  ion::base::logging_internal::NullLogger null_logger;
  // Test that NullLogger can handle std::endl.
  null_logger.GetStream() << std::endl;
}

#if !ION_PRODUCTION
TEST(Logging, OneInfo) {
  ion::base::LogChecker checker;

  LOG(INFO) << "Test string";
  const int line = __LINE__ - 1;
  EXPECT_EQ(BuildMessage("INFO", line, "Test string\n"),
            GetCanonicalFilePath(checker.GetLogString()));
  checker.ClearLog();
}

TEST(Logging, Multiple) {
  ion::base::LogChecker checker;

  LOG(WARNING) << "This is a warning!";
  const int line0 = __LINE__ - 1;
  LOG(ERROR) << "And an error!";
  const int line1 = __LINE__ - 1;
  EXPECT_EQ(BuildMessage("WARNING", line0, "This is a warning!\n") +
            BuildMessage("ERROR", line1, "And an error!\n"),
            GetCanonicalFilePath(checker.GetLogString()));
  checker.ClearLog();
}

TEST(Logging, SingleLogger) {
  ion::base::LogChecker checker;
  int line = LogMessageOnce();
  EXPECT_EQ(BuildMessage("INFO", line, "This message should be printed once\n"),
            GetCanonicalFilePath(checker.GetLogString()));
  checker.ClearLog();
  LogMessageOnce();
  EXPECT_FALSE(checker.HasAnyMessages());

  line = LogAnotherMessageOnce();
  EXPECT_EQ(
      BuildMessage("INFO", line, "This message should also be printed once\n"),
      GetCanonicalFilePath(checker.GetLogString()));
  checker.ClearLog();
  LogAnotherMessageOnce();
  EXPECT_FALSE(checker.HasAnyMessages());

  // Clear the set of logged messages.
  ion::base::logging_internal::SingleLogger::ClearMessages();
  line = LogAnotherMessageOnce();
  EXPECT_EQ(
      BuildMessage("INFO", line, "This message should also be printed once\n"),
      GetCanonicalFilePath(checker.GetLogString()));
  checker.ClearLog();
  LogAnotherMessageOnce();
  EXPECT_FALSE(checker.HasAnyMessages());
}

TEST(Logging, ThrottledLogger) {
  ion::base::LogChecker checker;
  int line = LogMessageEverySecond();
  EXPECT_EQ(BuildMessage("INFO", line, "This message should be printed no more "
                              "than once per second\n"),
            GetCanonicalFilePath(checker.GetLogString()));
  checker.ClearLog();
  LogMessageEverySecond();
  EXPECT_FALSE(checker.HasAnyMessages());
  ion::port::Timer::SleepNSeconds(2);
  line = LogMessageEverySecond();
  EXPECT_EQ(BuildMessage("INFO", line, "This message should be printed no more "
                                       "than once per second\n"),
            GetCanonicalFilePath(checker.GetLogString()));
  EXPECT_TRUE(checker.HasMessage("INFO", "This message should be printed"));
}
#endif

TEST(Logging, BreakHandlerOnFatal) {
  ion::base::LogChecker checker;
  TestBreakHandlerWrapper handler_;
  EXPECT_FALSE(handler_.HasBeenCalled());
  ion::base::SetBreakHandler(
      std::bind(&TestBreakHandlerWrapper::HandleBreak, &handler_));
  LOG(FATAL) << "Fatal error";
  EXPECT_TRUE(handler_.HasBeenCalled());
  EXPECT_TRUE(checker.HasMessage("FATAL", "Fatal error"));
}

TEST(Logging, BreakHandlerOnCheck) {
  ion::base::LogChecker checker;
  TestBreakHandlerWrapper handler_;
  EXPECT_FALSE(handler_.HasBeenCalled());
  ion::base::SetBreakHandler(
      std::bind(&TestBreakHandlerWrapper::HandleBreak, &handler_));
  CHECK(false) << "Failed check";
  EXPECT_TRUE(handler_.HasBeenCalled());
  EXPECT_TRUE(checker.HasMessage("FATAL", "Failed check"));
}

TEST(Logging, NullLoggerBreaksOnFatal) {
  ion::base::LogChecker checker;
  TestBreakHandlerWrapper handler_;
  EXPECT_FALSE(handler_.HasBeenCalled());
  ion::base::SetBreakHandler(
      std::bind(&TestBreakHandlerWrapper::HandleBreak, &handler_));
  ion::base::logging_internal::NullLogger null_logger_info(ion::port::INFO);
  EXPECT_FALSE(handler_.HasBeenCalled());
  ion::base::logging_internal::NullLogger null_logger_fatal(ion::port::FATAL);
  EXPECT_TRUE(handler_.HasBeenCalled());
}

TEST(Logging, DcheckSyntax) {
  // Make sure that CHECK and DCHECK parenthesize expressions properly.
  CHECK_EQ(0x1, 0x1 & 0x3);
  CHECK_NE(0x0, 0x1 & 0x3);
  CHECK_LE(0x1, 0x1 & 0x3);
  CHECK_LT(0x0, 0x1 & 0x3);
  CHECK_GE(0x1, 0x1 & 0x3);
  CHECK_GT(0x2, 0x1 & 0x3);

  DCHECK_EQ(0x1, 0x1 & 0x3);
  DCHECK_NE(0x0, 0x1 & 0x3);
  DCHECK_LE(0x1, 0x1 & 0x3);
  DCHECK_LT(0x0, 0x1 & 0x3);
  DCHECK_GE(0x1, 0x1 & 0x3);
  DCHECK_GT(0x2, 0x1 & 0x3);

  // Make sure that CHECK_NOTNULL returns the argument value.
  int some_int = 0;
  int* some_int_ptr = CHECK_NOTNULL(&some_int);
  CHECK_EQ(&some_int, some_int_ptr);
}

// Verify that log messages don't interleave.
TEST(Logging, NoInterleaving) {
  ion::base::LogChecker checker;

  using ion::base::logging_internal::Logger;
  std::unique_ptr<Logger> logger1(new Logger("file1", 42, ion::port::INFO));
  std::unique_ptr<Logger> logger2(new Logger("file2", 24, ion::port::INFO));
  logger1->GetStream() << "logger1 message";
  logger2->GetStream() << "logger2 message";
  // This is the key to this test; logger1 needs to be freed before logger2 to
  // demonstrate that messages don't get interleaved.
  logger1.reset();
  ASSERT_EQ("INFO [file1:42] logger1 message\n", checker.GetLogString());
  checker.ClearLog();

  logger2.reset();
  ASSERT_EQ("INFO [file2:24] logger2 message\n", checker.GetLogString());
  checker.ClearLog();

  EXPECT_FALSE(checker.HasAnyMessages());
}
