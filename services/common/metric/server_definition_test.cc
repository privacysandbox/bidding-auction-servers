//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/common/metric/server_definition.h"

#include <list>

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers::metric {
namespace {

TEST(Sever, Initialization) {
  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
  MetricContextMap<google::protobuf::Message>(
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_proto));
  MetricContextMap<GetBidsRequest>(
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_proto));
  MetricContextMap<ScoreAdsRequest>(
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_proto));
  MetricContextMap<SelectAdRequest>(
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_proto));
}

TEST(Sever, GetContext) {
  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
  auto* bidding = MetricContextMap<google::protobuf::Message>(
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_proto));
  const GenerateBidsRequest request;
  EXPECT_FALSE(bidding->Get(&request).is_decrypted());
  bidding->Get(&request).SetDecrypted();
  EXPECT_TRUE(bidding->Get(&request).is_decrypted());
  CHECK_OK(bidding->Remove(&request));
  EXPECT_FALSE(bidding->Get(&request).is_decrypted());
}

constexpr bool IsInList(absl::string_view proto_elem) {
  for (auto m : kSellerRejectReasons) {
    if (proto_elem == m) {
      return true;
    }
  }
  return false;
}

TEST(Init, AllTypes) {
  std::list<SellerRejectionReason> sellerRejectionReasonProtoList;
  for (int i = SellerRejectionReason_MIN; i <= SellerRejectionReason_MAX; ++i) {
    sellerRejectionReasonProtoList.push_back(
        static_cast<SellerRejectionReason>(i));
  }
  for (const auto& value : sellerRejectionReasonProtoList) {
    EXPECT_TRUE(IsInList(ToSellerRejectionReasonString(value)));
  }
}

TEST(GetErrorListTest, ReturnsCorrectErrorList) {
  std::vector<std::string> error_list = GetErrorList();

  EXPECT_FALSE(error_list.empty()) << "Error list should not be empty";

  int num_status_codes = static_cast<int>(absl::StatusCode::kUnauthenticated) -
                         static_cast<int>(absl::StatusCode::kOk) + 1;
  EXPECT_EQ(error_list.size(), num_status_codes)
      << "Error list size is incorrect";

  absl::StatusCode code = absl::StatusCode::kOk;
  for (const std::string& error_string : error_list) {
    EXPECT_EQ(error_string, StatusCodeToString(code))
        << "Error string for status code " << static_cast<int>(code)
        << " is incorrect";
    code = static_cast<absl::StatusCode>(static_cast<int>(code) + 1);
  }
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers::metric
