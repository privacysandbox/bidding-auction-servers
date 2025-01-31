// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"

#include <utility>

#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::string_view kValidUrl = "https://example.com:80";
constexpr absl::string_view kInvalidUrl =
    "example.com:80:https://path/to/2?query=1&2#fragment";
constexpr absl::string_view kValidDomain = "example.com";

TEST(GetDomainFromUrlTest, ReturnsDomainFromUrl) {
  std::string input = absl::StrCat(kValidUrl);
  auto actual = GetDomainFromUrl(std::move(input));
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(kValidDomain, *actual);
}

TEST(GetDomainFromUrlTest, ReturnsDomainFromUrlWithPath) {
  std::string input = absl::StrCat(kValidUrl);
  for (int i = 0; i < 10; i++) {
    input += "/";
    input += std::to_string(i);
  }
  auto actual = GetDomainFromUrl(input);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(kValidDomain, *actual);
}

TEST(GetDomainFromUrlTest, ReturnsDomainFromUrlWithQuery) {
  std::string input = absl::StrCat(kValidUrl, "?foo=bar");
  auto actual = GetDomainFromUrl(input);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(kValidDomain, *actual);
}

TEST(GetDomainFromUrlTest, ReturnsDomainFromUrlWithFragment) {
  std::string input = absl::StrCat(kValidUrl, "#foo");
  auto actual = GetDomainFromUrl(input);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(kValidDomain, *actual);
}

TEST(GetDomainFromUrlTest, ReturnsDomainFromComplexUrl) {
  std::string input = absl::StrCat(kValidUrl, "/path/to/2?query=1&2#fragment");
  auto actual = GetDomainFromUrl(input);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(kValidDomain, *actual);
}

TEST(GetDomainFromUrlTest, ReturnsDomainFromComplexUrlWithNoProtocol) {
  std::string input =
      absl::StrCat(kValidDomain, "/path/to/2?query=1&2#fragment");
  auto actual = GetDomainFromUrl(input);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(kValidDomain, *actual);
}

TEST(GetDomainFromUrlTest, ReturnsErrorForMalformedUrl) {
  std::string input = absl::StrCat(kInvalidUrl);
  auto actual = GetDomainFromUrl(input);
  EXPECT_FALSE(actual.ok());
}

TEST(StripQueryAndFragmentsFromUrlTest, ReturnsUrlWithoutQuery) {
  std::string input = absl::StrCat(kValidUrl, "/path/to/2?query=1&2");
  auto actual = StripQueryAndFragmentsFromUrl(input);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(absl::StrCat(kValidDomain, "/path/to/2"), *actual);
}

TEST(StripQueryAndFragmentsFromUrlTest, ReturnsUrlWithoutFragment) {
  std::string input = absl::StrCat(kValidUrl, "/path/to/2#fragment");
  auto actual = StripQueryAndFragmentsFromUrl(input);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(absl::StrCat(kValidDomain, "/path/to/2"), *actual);
}

TEST(StripQueryAndFragmentsFromUrlTest, ReturnsUrlWithoutQueryAndFragment) {
  std::string input = absl::StrCat(kValidUrl, "/path/to/2?query=1&2#fragment");
  auto actual = StripQueryAndFragmentsFromUrl(input);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(absl::StrCat(kValidDomain, "/path/to/2"), *actual);
}

TEST(StripQueryAndFragmentsFromUrlTest, ReturnsUrlWithoutPathQueryAndFragment) {
  std::string input = absl::StrCat(kValidUrl, "?query=1&2#fragment");
  auto actual = StripQueryAndFragmentsFromUrl(input);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(absl::StrCat(kValidDomain, "/"), *actual);
}

TEST(StripQueryAndFragmentsFromUrlTest, ReturnsDomainForSimpleUrl) {
  std::string input = absl::StrCat(kValidUrl);
  auto actual = StripQueryAndFragmentsFromUrl(input);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(absl::StrCat(kValidDomain, "/"), *actual);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
