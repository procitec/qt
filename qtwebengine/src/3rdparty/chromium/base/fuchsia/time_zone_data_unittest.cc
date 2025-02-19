// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/icu_util.h"

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/test/icu_test_util.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/icu/source/common/unicode/uclean.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace base {
namespace i18n {

namespace {

// Directory path to the tzdata configuration files.
const char kTzDataDirPath[] = "/pkg/base/test/data/tzdata/icu/44/le";

// File path to the text file containing the expected ICU library revision, for
// example "2019c".
// TODO(crbug.com/1360077): Remove once tzdata is fully migrated away from
// config-data. I.e. all released Chromium versions in Fuchsia support it.
const char kRevisionFilePath[] = "/config/data/tzdata/revision.txt";
// Same as above, except the modern version.
const char kTZDataRevisionFilePath[] = "/config/tzdata/icu/revision.txt";

}  // namespace

class TimeZoneDataTest : public testing::Test {
 protected:
  void TearDown() override {
    ResetIcu();

    // ICU must be set back up in case e.g. a log statement that formats times
    // uses it.
    test::InitializeICUForTesting();
  }

  // Needed to enable loading of ICU config files that are different from what
  // is available in Chromium.  Both icu_util and ICU library keep internal
  // state so clear both.
  void ResetIcu() {
    // Clears the state in the reverse order of construction.
    u_cleanup();
    ResetGlobalsForTesting();
  }

  void GetActualRevision(std::string* icu_version) {
    UErrorCode err = U_ZERO_ERROR;
    *icu_version = icu::TimeZone::getTZDataVersion(err);
    ASSERT_EQ(U_ZERO_ERROR, err) << u_errorName(err);
  }
};

// Loads a file revision.txt from the actual underlying filesystem, which
// contains the tzdata version we expect to be able to load.  It then attempts
// to load this configuration from the default path and compares the version it
// obtained from the load with the expected version, failing on version
// mismatch.
//
// In Fuchsia build bot setup, we ensure that the file revision.txt exists, so
// that this test is not skipped. In Chromium build bot setup, this file may
// not be present, in which case we skip running this test.
TEST_F(TimeZoneDataTest, CompareSystemRevisionWithExpected) {
  std::string revision_file_path;
  if (base::PathExists(base::FilePath(kTZDataRevisionFilePath))) {
    revision_file_path = kTZDataRevisionFilePath;
  } else if (base::PathExists(base::FilePath(kRevisionFilePath))) {
    // Legacy path.
    revision_file_path = kRevisionFilePath;
  }

  if (revision_file_path.empty()) {
    FAIL() << "No revision file found";
  }

  // ResetIcu() ensures that time zone data is loaded from the default location.
  // This is done after the GTEST_SKIP() call above, since that may output a
  // timestamp that requires ICU to be set up.
  ResetIcu();

  ASSERT_TRUE(InitializeICU());
  std::string expected;
  EXPECT_TRUE(
      base::ReadFileToString(base::FilePath(revision_file_path), &expected));
  std::string actual;
  GetActualRevision(&actual);
  EXPECT_EQ(expected, actual);
}

// Verifies that the current version of the ICU library in use can load ICU
// data in a specific version format (in this case 44).  Designed to fail if
// the ICU library version used in Chromium drifts from version 44 so much that
// the library is no longer able to load the old tzdata.  If the test fails,
// this could be a sign that all platforms Chromium runs on need to upgrade the
// ICU library versions.
TEST_F(TimeZoneDataTest, TestLoadingTimeZoneDataFromKnownConfigs) {
  ASSERT_TRUE(base::DirectoryExists(base::FilePath(kTzDataDirPath)));
  ResetIcu();
  SetIcuTimeZoneDataDirForTesting(kTzDataDirPath);

  ASSERT_TRUE(InitializeICU());
  std::string actual;
  GetActualRevision(&actual);
  EXPECT_EQ("2019a", actual) << "If ICU no longer supports this tzdata "
                                "version, tzdata version needs to be upgraded";
}

TEST_F(TimeZoneDataTest, DoesNotCrashWithInvalidPath) {
  ResetIcu();
  SetIcuTimeZoneDataDirForTesting("/some/nonexistent/path");

  ASSERT_TRUE(InitializeICU());
  std::string actual;
  GetActualRevision(&actual);
  EXPECT_TRUE(
      base::StartsWith(actual, "20", base::CompareCase::INSENSITIVE_ASCII))
      << "Got version: " << actual;
}

}  // namespace i18n
}  // namespace base
