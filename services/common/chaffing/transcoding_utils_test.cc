//  Copyright 2024 Google LLC
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

#include "services/common/chaffing/transcoding_utils.h"

#include <gmock/gmock-matchers.h>

#include <string>

#include <include/gmock/gmock-actions.h>

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(TranscodingUtilsTest, VerifySuccessfulEncodeDecode_Uncompressed) {
  GetBidsRequest::GetBidsRawRequest raw_request;
  raw_request.set_is_chaff(true);
  raw_request.mutable_log_context()->set_generation_id("testGenerationId");

  absl::StatusOr<std::string> encoded_payload = EncodeAndCompressGetBidsPayload(
      raw_request, CompressionType::kUncompressed);
  ASSERT_TRUE(encoded_payload.ok()) << encoded_payload.status();

  auto decoded_payload =
      DecodeGetBidsPayload<GetBidsRequest::GetBidsRawRequest>(*encoded_payload);
  ASSERT_TRUE(decoded_payload.ok());

  google::protobuf::util::MessageDifferencer differencer;
  EXPECT_TRUE(differencer.Equals(decoded_payload->get_bids_proto, raw_request));
  EXPECT_EQ(decoded_payload->compression_type, CompressionType::kUncompressed);
  EXPECT_EQ(decoded_payload->version, 0);
  EXPECT_EQ(decoded_payload->payload_length,
            raw_request.SerializeAsString().length());
}

TEST(TranscodingUtilsTest, VerifySuccessfulEncodeDecode_Gzip) {
  GetBidsRequest::GetBidsRawRequest raw_request;
  raw_request.set_is_chaff(true);
  raw_request.mutable_log_context()->set_generation_id("testGenerationId");

  absl::StatusOr<std::string> encoded_payload =
      EncodeAndCompressGetBidsPayload(raw_request, CompressionType::kGzip);
  ASSERT_TRUE(encoded_payload.ok());

  auto decoded_payload =
      DecodeGetBidsPayload<GetBidsRequest::GetBidsRawRequest>(*encoded_payload);
  ASSERT_TRUE(decoded_payload.ok()) << decoded_payload.status();

  google::protobuf::util::MessageDifferencer differencer;
  EXPECT_TRUE(differencer.Equals(decoded_payload->get_bids_proto, raw_request));
  EXPECT_EQ(decoded_payload->compression_type, CompressionType::kGzip);
  EXPECT_EQ(decoded_payload->version, 0);
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
