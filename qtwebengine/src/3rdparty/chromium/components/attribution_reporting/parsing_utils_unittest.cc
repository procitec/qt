// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/attribution_reporting/parsing_utils.h"

#include <stdint.h>

#include <limits>
#include <string>

#include "base/test/gmock_expected_support.h"
#include "base/test/values_test_util.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "components/attribution_reporting/constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/numeric/int128.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/abseil-cpp/absl/types/variant.h"

namespace attribution_reporting {
namespace {

TEST(AttributionReportingParsingUtilsTest, ParseAggregationKeyPiece) {
  const struct {
    base::Value value;
    base::expected<absl::uint128, AggregationKeyPieceError> expected;
  } kTestCases[] = {
      {base::Value(), base::unexpected(AggregationKeyPieceError::kWrongType)},
      {base::Value("123"),
       base::unexpected(AggregationKeyPieceError::kWrongFormat)},
      {base::Value("0x123"), 291},
      {base::Value("0X123"), 291},
      {base::Value("0xG"),
       base::unexpected(AggregationKeyPieceError::kWrongFormat)},
  };

  for (const auto& test_case : kTestCases) {
    EXPECT_EQ(ParseAggregationKeyPiece(test_case.value), test_case.expected);
  }
}

TEST(AttributionReportingParsingUtilsTest, AggregationKeyIdHasValidLength) {
  EXPECT_TRUE(AggregationKeyIdHasValidLength(
      std::string(kMaxBytesPerAggregationKeyId, 'a')));
  EXPECT_FALSE(AggregationKeyIdHasValidLength(
      std::string(kMaxBytesPerAggregationKeyId + 1, 'a')));
}

TEST(AttributionReportingParsingUtilsTest, ParseUint64) {
  const struct {
    const char* description;
    const char* json;
    base::expected<absl::optional<uint64_t>, absl::monostate> expected;
  } kTestCases[] = {
      {
          "missing_key",
          R"json({})json",
          absl::nullopt,
      },
      {
          "not_string",
          R"json({"key":123})json",
          base::unexpected(absl::monostate()),
      },
      {
          "invalid_format",
          R"json({"key":"0x123"})json",
          base::unexpected(absl::monostate()),
      },
      {
          "negative",
          R"json({"key":"-1"})json",
          base::unexpected(absl::monostate()),
      },
      {
          "zero",
          R"json({"key":"0"})json",
          0,
      },
      {
          "max",
          R"json({"key":"18446744073709551615"})json",
          std::numeric_limits<uint64_t>::max(),
      },
      {
          "out_of_range",
          R"json({"key":"18446744073709551616"})json",
          base::unexpected(absl::monostate()),
      },
  };

  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.description);
    const base::Value::Dict dict = base::test::ParseJsonDict(test_case.json);
    EXPECT_EQ(ParseUint64(dict, "key"), test_case.expected);
  }
}

TEST(AttributionReportingParsingUtilsTest, ParseInt64) {
  const struct {
    const char* description;
    const char* json;
    base::expected<absl::optional<int64_t>, absl::monostate> expected;
  } kTestCases[] = {
      {
          "missing_key",
          R"json({})json",
          absl::nullopt,
      },
      {
          "not_string",
          R"json({"key":123})json",
          base::unexpected(absl::monostate()),
      },
      {
          "invalid_format",
          R"json({"key":"0x123"})json",
          base::unexpected(absl::monostate()),
      },
      {
          "zero",
          R"json({"key":"0"})json",
          0,
      },
      {
          "min",
          R"json({"key":"-9223372036854775808"})json",
          std::numeric_limits<int64_t>::min(),
      },
      {
          "max",
          R"json({"key":"9223372036854775807"})json",
          std::numeric_limits<int64_t>::max(),
      },
      {
          "out_of_range",
          R"json({"key":"9223372036854775808"})json",
          base::unexpected(absl::monostate()),
      },
  };

  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.description);
    const base::Value::Dict dict = base::test::ParseJsonDict(test_case.json);
    EXPECT_EQ(ParseInt64(dict, "key"), test_case.expected);
  }
}

TEST(AttributionReportingParsingUtilsTest, HexEncodeAggregationKey) {
  const struct {
    absl::uint128 input;
    const char* expected;
  } kTestCases[] = {
      {0, "0x0"},
      {absl::MakeUint128(/*high=*/0,
                         /*low=*/std::numeric_limits<uint64_t>::max()),
       "0xffffffffffffffff"},
      {absl::MakeUint128(/*high=*/1,
                         /*low=*/std::numeric_limits<uint64_t>::max()),
       "0x1ffffffffffffffff"},
      {std::numeric_limits<absl::uint128>::max(),
       "0xffffffffffffffffffffffffffffffff"},
  };

  for (const auto& test_case : kTestCases) {
    EXPECT_EQ(HexEncodeAggregationKey(test_case.input), test_case.expected)
        << test_case.input;
  }
}

}  // namespace
}  // namespace attribution_reporting
