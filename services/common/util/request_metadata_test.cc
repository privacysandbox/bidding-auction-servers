/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "services/common/util/request_metadata.h"

#include "include/gtest/gtest.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

template <std::size_t N>
std::array<std::pair<std::string_view, std::string_view>, N> CreateKeyPairsMap(
    const RequestMetadata& metadata,
    const std::vector<std::string>& new_headers) {
  std::array<std::pair<std::string_view, std::string_view>, N> keys_map;
  int i = 0;
  for (const auto& it : metadata) {
    if (i >= N) {
      continue;
    }
    keys_map[i] = std::make_pair(it.first.data(), new_headers[i].data());
    i++;
  }
  return keys_map;
}

std::multimap<grpc::string_ref, grpc::string_ref> CreateGrpcMetadata(
    const RequestMetadata& metadata) {
  std::multimap<grpc::string_ref, grpc::string_ref> client_metadata;
  for (const auto& it : metadata) {
    client_metadata.insert({it.first, it.second});
  }
  return client_metadata;
}

template <std::size_t N>
void VerifyOnlyKeysInArrayMappedFromGrpcContextToMetadata(
    const RequestMetadata& original_metadata,
    const RequestMetadata& received_metadata,
    const std::array<std::pair<std::string_view, std::string_view>, N>&
        key_mapping) {
  ASSERT_EQ(received_metadata.size(), key_mapping.size());
  for (auto const& key_pair : key_mapping) {
    auto& source_key = key_pair.first;
    auto& target_key = key_pair.second;
    // find target key in received metadata
    auto const& target_metadata_itr = received_metadata.find(target_key.data());
    ASSERT_TRUE(target_metadata_itr != received_metadata.end());

    // find source key in source metadata
    auto const& source_metadata_itr = original_metadata.find(source_key.data());
    ASSERT_TRUE(source_metadata_itr != original_metadata.end());

    // value of target key in received metadata must be same as value of source
    //  key in original map
    EXPECT_EQ(target_metadata_itr->second, source_metadata_itr->second);
  }
}

TEST(GrpcMetadataToRequestMetadata, MapsKeysInArrayFromGrpcContextToMetadata) {
  RequestMetadata original = MakeARandomMap(2);
  std::vector<std::string> new_keys = {MakeARandomString(),
                                       MakeARandomString()};

  std::array old_keys_to_new_keys = CreateKeyPairsMap<2>(original, new_keys);
  std::multimap grpc_metadata = CreateGrpcMetadata(original);

  absl::StatusOr<RequestMetadata> output =
      GrpcMetadataToRequestMetadata(grpc_metadata, old_keys_to_new_keys);
  ASSERT_TRUE(output.ok()) << output.status();
  VerifyOnlyKeysInArrayMappedFromGrpcContextToMetadata(original, *output,
                                                       old_keys_to_new_keys);
}

TEST(GrpcMetadataToRequestMetadata, IgnoresKeysFromGrpcContextNotInArray) {
  RequestMetadata original = MakeARandomMap(2);
  std::vector<std::string> new_keys = {MakeARandomString()};

  auto old_keys_to_new_keys = CreateKeyPairsMap<1>(original, new_keys);
  auto grpc_metadata = CreateGrpcMetadata(original);

  absl::StatusOr<RequestMetadata> output =
      GrpcMetadataToRequestMetadata(grpc_metadata, old_keys_to_new_keys);
  ASSERT_TRUE(output.ok()) << output.status();
  VerifyOnlyKeysInArrayMappedFromGrpcContextToMetadata(original, *output,
                                                       old_keys_to_new_keys);
}

TEST(GrpcMetadataToRequestMetadata, HandlesEmptyInput) {
  RequestMetadata empty_map;
  std::vector<std::string> new_keys = {MakeARandomString()};

  RequestMetadata sample = MakeARandomMap(1);
  auto old_keys_to_new_keys = CreateKeyPairsMap<1>(sample, new_keys);
  auto grpc_metadata = CreateGrpcMetadata(empty_map);

  absl::StatusOr<RequestMetadata> output =
      GrpcMetadataToRequestMetadata(grpc_metadata, old_keys_to_new_keys);
  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_EQ(empty_map, *output);
}

TEST(GrpcMetadataToRequestMetadata, IsValidHeader) {
  EXPECT_FALSE(IsValidHeader("valid", "invalid \xC3 \xA9"));
  EXPECT_FALSE(IsValidHeader("\xF9 (invalid) \xD9", "valid"));
  EXPECT_FALSE(IsValidHeader("INVALID", "valid"));
  EXPECT_TRUE(IsValidHeader("-valid_", "V A L I D !@#$%^&*()_+"));
  EXPECT_TRUE(IsValidHeader(
      "x-user-agent",
      "Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 (KHTML, like Gecko) "
      "Chrome/121.0.6167.101 Mobile Safari/537.36,gzip(foo)"));
  EXPECT_TRUE(IsValidHeader("x-bna-client-ip",
                            "0000:000:zz0z:000:00zz:00z0:z000:0000"));
  EXPECT_TRUE(IsValidHeader("x-accept-language",
                            "pt-BR,pt;q=0.9,en-US;q=0.8,en;q=0.7"));
}

TEST(RequestMetadataToHttpHeaders, ConvertsMapToHeaders) {
  RequestMetadata original = MakeARandomMap(2);
  std::vector<std::string> expected;
  for (auto const& it : original) {
    expected.emplace_back(absl::StrCat(it.first, ":", it.second));
  }
  std::vector<std::string> output = RequestMetadataToHttpHeaders(original);

  EXPECT_EQ(output, expected);
}

TEST(RequestMetadataToHttpHeaders, HandlesEmptyInput) {
  RequestMetadata original;
  std::vector<std::string> expected;
  std::vector<std::string> output = RequestMetadataToHttpHeaders(original);

  EXPECT_EQ(output, expected);
}

TEST(MetadataMapToHttpHeadersWithMandatory, InsertsMandatoryHeaders) {
  RequestMetadata original = MakeARandomMap(2);
  std::vector<std::string> mandatory_headers = {MakeARandomString(),
                                                MakeARandomString()};
  std::vector<std::string> expected;

  std::array<std::string_view, 2> mandatory_headers_input;
  int i = 0;
  for (auto const& it : mandatory_headers) {
    mandatory_headers_input[i++] = it;
    expected.emplace_back(absl::StrCat(it, ":"));
  }
  for (auto const& it : original) {
    expected.emplace_back(absl::StrCat(it.first, ":", it.second));
  }

  std::vector<std::string> output =
      RequestMetadataToHttpHeaders(original, mandatory_headers_input);

  EXPECT_EQ(output, expected);
}

TEST(MetadataMapToHttpHeadersWithMandatory, HandlesEmptyInput) {
  RequestMetadata original;
  std::vector<std::string> expected;
  std::vector<std::string> output =
      RequestMetadataToHttpHeaders<0>(original, {});

  EXPECT_EQ(output, expected);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
