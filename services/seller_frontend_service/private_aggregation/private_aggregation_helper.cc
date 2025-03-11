// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "services/seller_frontend_service/private_aggregation/private_aggregation_helper.h"

#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_join.h"
#include "services/common/compression/gzip.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/private_aggregation/private_aggregation_post_auction_util.h"
#include "services/common/util/error_accumulator.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/util/cbor_common_util.h"
#include "src/logger/request_context_logger.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {
constexpr int kInt64Size = sizeof(int64_t);
constexpr absl::string_view kReservedWinEvent = "reserved.win";
constexpr absl::string_view kReservedLossEvent = "reserved.loss";
constexpr absl::string_view kReservedAlwaysEvent = "reserved.always";
constexpr absl::string_view kReservedPrefix = "reserved.";
constexpr int kBucketSizeInBytes = 16;  // 128 bits

// Filters the referenced AdWithBid parameter to select win, custom, and
// always contributions. Edits the passed AdWithBid input parameter.
void FilterWinContributions(AdWithBid& ad_with_bid) {
  std::vector<PrivateAggregateContribution> winning_contributions;
  for (auto& contribution :
       *ad_with_bid.mutable_private_aggregation_contributions()) {
    if (contribution.event().event_type() == EventType::EVENT_TYPE_WIN ||
        contribution.event().event_type() == EventType::EVENT_TYPE_CUSTOM ||
        contribution.event().event_type() == EventType::EVENT_TYPE_ALWAYS) {
      winning_contributions.push_back(std::move(contribution));
    }
  }
  ad_with_bid.clear_private_aggregation_contributions();

  for (auto& win_contribution : winning_contributions) {
    *ad_with_bid.add_private_aggregation_contributions() =
        std::move(win_contribution);
  }
}

// Filters the referenced AdWithBid parameter to select loss and always
// contributions. Edits the passed AdWithBid input parameter.
void FilterLossContributions(AdWithBid& ad_with_bid) {
  std::vector<PrivateAggregateContribution> losing_contributions;
  for (auto& contribution :
       *ad_with_bid.mutable_private_aggregation_contributions()) {
    if (contribution.event().event_type() == EventType::EVENT_TYPE_LOSS ||
        contribution.event().event_type() == EventType::EVENT_TYPE_ALWAYS) {
      losing_contributions.push_back(std::move(contribution));
    }
  }

  ad_with_bid.clear_private_aggregation_contributions();

  for (auto& loss_contribution : losing_contributions) {
    *ad_with_bid.add_private_aggregation_contributions() =
        std::move(loss_contribution);
  }
}

BaseValues GetBaseValues(ScoreAdsResponse::AdScore& high_score) {
  BaseValues base_values;
  float highest_scoring_other_bid = 0.0;
  if (!high_score.ig_owner_highest_scoring_other_bids_map().empty()) {
    auto iterator =
        high_score.ig_owner_highest_scoring_other_bids_map().begin();
    if (!iterator->second.values().empty()) {
      highest_scoring_other_bid =
          iterator->second.values().Get(0).number_value();
    }
  }
  base_values.highest_scoring_other_bid = highest_scoring_other_bid;

  if (high_score.incoming_bid_in_seller_currency() > 0) {
    base_values.winning_bid = high_score.incoming_bid_in_seller_currency();
  } else {
    base_values.winning_bid = high_score.buyer_bid();
  }
  return base_values;
}

std::vector<PrivateAggregateContribution> GetProcessedAndFilteredContributions(
    AdWithBid& ad_with_bid, BaseValues& base_values) {
  std::vector<PrivateAggregateContribution> processed_filtered_contributions;
  for (PrivateAggregateContribution& contribution :
       *ad_with_bid.mutable_private_aggregation_contributions()) {
    absl::StatusOr<int> signal_value_status =
        GetPrivateAggregationValuePostAuction(base_values,
                                              contribution.value());
    absl::StatusOr<Bucket128Bit> signal_bucket_status =
        GetPrivateAggregationBucketPostAuction(base_values,
                                               contribution.bucket());
    if (signal_value_status.ok() && signal_bucket_status.ok()) {
      if (contribution.value().has_extended_value()) {
        contribution.mutable_value()->clear_extended_value();
      }
      contribution.mutable_value()->set_int_value(signal_value_status.value());
      if (contribution.bucket().has_signal_bucket()) {
        contribution.mutable_bucket()->clear_signal_bucket();
      }
      *contribution.mutable_bucket()->mutable_bucket_128_bit() =
          signal_bucket_status.value();
      contribution.clear_event();
      processed_filtered_contributions.push_back(std::move(contribution));
    } else {
      PS_VLOG(4) << "Private Aggregation Contribution dropped.";
    }
  }
  return processed_filtered_contributions;
}

absl::StatusOr<PrivateAggregationBucket> GetBucketFromByteString(
    const std::string& byte_string) {
  if (byte_string.size() != kBucketSizeInBytes) {
    return absl::InternalError("Byte string size is not exactly 16 bytes.");
  }

  PrivateAggregationBucket private_aggregation_bucket;
  Bucket128Bit bucket_128_bit;
  int64_t upper_int = 0;
  int64_t lower_int = 0;
  // Copy the chunks from the byte string, interpreting them as big-endian.
  std::memcpy(&upper_int, byte_string.data(), kInt64Size);
  std::memcpy(&lower_int, byte_string.data() + kInt64Size, kInt64Size);

  // Convert the big-endian values to host-endian and add them to the bucket.
  bucket_128_bit.add_bucket_128_bits(absl::big_endian::ToHost64(lower_int));
  bucket_128_bit.add_bucket_128_bits(absl::big_endian::ToHost64(upper_int));

  *private_aggregation_bucket.mutable_bucket_128_bit() =
      std::move(bucket_128_bit);

  return private_aggregation_bucket;
}

std::string GetEvent(const PrivateAggregateContribution* contribution) {
  switch (contribution->event().event_type()) {
    case EventType::EVENT_TYPE_WIN:
      return kReservedWinEvent.data();
    case EventType::EVENT_TYPE_LOSS:
      return kReservedLossEvent.data();
    case EventType::EVENT_TYPE_ALWAYS:
      return kReservedAlwaysEvent.data();
    case EventType::EVENT_TYPE_CUSTOM:
      return contribution->event().event_name();
    default:
      return "";
  }
}

absl::Status CborSerializePAggContributionList(
    const std::vector<const PrivateAggregateContribution*>& contributions,
    ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_contributions(
      cbor_new_definite_array(contributions.size()));
  for (const PrivateAggregateContribution* contribution : contributions) {
    ScopedCbor serialized_pagg_contribution(
        cbor_new_definite_map(kNumContributionKeys));
    absl::Status contribution_status = CborSerializePAggContribution(
        *contribution, error_handler, **serialized_pagg_contribution);
    if (!contribution_status.ok()) {
      PS_LOG(ERROR) << "Serialization failed for PrivateAggregateContribution:"
                    << contribution_status;
      continue;
    }
    if (!cbor_array_push(*serialized_contributions,
                         *serialized_pagg_contribution)) {
      PS_LOG(ERROR) << "Serialization failed for PrivateAggregateContribution:";
      continue;
    }
  }
  struct cbor_pair kv = {.key = cbor_move(cbor_build_stringn(
                             "contributions", sizeof("contributions") - 1)),
                         .value = *serialized_contributions};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        absl::StrCat("Failed to serialize PAggContributions to CBOR")));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

PrivateAggregationEvent DecodePrivateAggEvent(absl::string_view event_name) {
  absl::flat_hash_map<std::string, EventType> event_type_map = {
      {kReservedWinEvent.data(), EventType::EVENT_TYPE_WIN},
      {kReservedLossEvent.data(), EventType::EVENT_TYPE_LOSS},
      {kReservedAlwaysEvent.data(), EventType::EVENT_TYPE_ALWAYS}};
  PrivateAggregationEvent event;
  if (event_name.empty()) {
    event.set_event_type(EventType::EVENT_TYPE_UNSPECIFIED);
  } else if (auto it = event_type_map.find(event_name);
             it != event_type_map.end()) {
    event.set_event_type(it->second);
  } else {
    event.set_event_type(EventType::EVENT_TYPE_CUSTOM);
    event.set_event_name(event_name);
  }
  return event;
}

absl::StatusOr<PrivateAggregateContribution> CborDecodePAggContribution(
    cbor_item_t* serialized_pagg_contribution) {
  PrivateAggregateContribution pagg_contribution;
  absl::Span<struct cbor_pair> contribution_map(
      cbor_map_handle(serialized_pagg_contribution),
      cbor_map_size(serialized_pagg_contribution));
  for (const cbor_pair& bucket_value_pair : contribution_map) {
    std::string pagg_contribution_key;
    PS_ASSIGN_OR_RETURN(pagg_contribution_key,
                        CheckTypeAndCborDecodeString(bucket_value_pair.key));
    const int index =
        FindItemIndex(kPaggContributionKeys, pagg_contribution_key);
    switch (index) {
      case 0: {  // kValue.
        if (!cbor_is_int(bucket_value_pair.value)) {
          return absl::InvalidArgumentError(
              "Error decoding PrivateAggregationValue. int value not found in "
              "the cbor encoded string.");
        }
        PrivateAggregationValue private_aggregation_value;
        private_aggregation_value.set_int_value(
            cbor_get_int(bucket_value_pair.value));
        *pagg_contribution.mutable_value() =
            std::move(private_aggregation_value);
        break;
      }
      case 1: {  // kBucket.
        if (!cbor_isa_bytestring(bucket_value_pair.value)) {
          return absl::InvalidArgumentError(
              "Error decoding PrivateAggregationBucket. bytestring not found "
              "in the cbor encoded string.");
        }

        absl::StatusOr<PrivateAggregationBucket> bucket =
            GetBucketFromByteString(
                CborDecodeByteString(bucket_value_pair.value));
        if (!bucket.ok()) {
          return absl::InvalidArgumentError(
              "Error decoding PrivateAggregationBucket");
        }
        *pagg_contribution.mutable_bucket() = *std::move(bucket);
        break;
      }
      default:
        PS_VLOG(5) << "Serialized CBOR paggContribution has unexpected key: "
                   << pagg_contribution_key;
    }
  }
  return pagg_contribution;
}

absl::StatusOr<std::vector<PrivateAggregateContribution>>
CborDecodePAggContributionList(
    absl::string_view event_name, std::optional<int> ig_idx,
    cbor_item_t& serialized_pagg_contributions_list) {
  std::vector<PrivateAggregateContribution> pagg_contributions_list;
  absl::Span<cbor_item_t*> contributions(
      cbor_array_handle(&serialized_pagg_contributions_list),
      cbor_array_size(&serialized_pagg_contributions_list));
  for (size_t i = 0; i < cbor_array_size(&serialized_pagg_contributions_list);
       i++) {
    absl::StatusOr<PrivateAggregateContribution> contribution =
        CborDecodePAggContribution(contributions[i]);
    if (!contribution.ok()) {
      PS_LOG(ERROR) << "Serialization failed for PrivateAggregateContribution:"
                    << contribution.status();
      continue;
    }
    if (!event_name.empty()) {
      *contribution->mutable_event() = DecodePrivateAggEvent(event_name);
    }
    if (ig_idx) {
      contribution->set_ig_idx(*ig_idx);
    }
    pagg_contributions_list.push_back(std::move(*contribution));
  }
  return pagg_contributions_list;
}

}  // namespace

void HandlePrivateAggregationContributions(
    const absl::flat_hash_map<InterestGroupIdentity, int>&
        interest_group_index_map,
    ScoreAdsResponse::AdScore& high_score,
    BuyerBidsResponseMap& shared_buyer_bids_map) {
  for (auto& [ig_owner, get_bid_response] : shared_buyer_bids_map) {
    PrivateAggregateReportingResponse reporting_response;
    reporting_response.set_adtech_origin(ig_owner);

    for (auto& ad_with_bid : *get_bid_response->mutable_bids()) {
      if (ad_with_bid.private_aggregation_contributions().empty()) {
        continue;
      }
      const std::string& ig_name = ad_with_bid.interest_group_name();
      int ig_idx = 0;
      InterestGroupIdentity ig = {.interest_group_owner = ig_owner,
                                  .interest_group_name = ig_name};
      const auto& ig_itr = interest_group_index_map.find(ig);
      if (ig_itr != interest_group_index_map.end()) {
        ig_idx = ig_itr->second;
      } else {
        PS_LOG(ERROR) << "Skipping PrivateAggregateContributions."
                      << "IG index not found for interest group owner:"
                      << ig_owner << " and interest group name" << ig_name;
        continue;
      }
      BaseValues base_values = GetBaseValues(high_score);

      // Filtering.
      if (high_score.interest_group_owner() == ig_owner &&
          ig_name == high_score.interest_group_name()) {
        FilterWinContributions(ad_with_bid);
      } else {
        FilterLossContributions(ad_with_bid);

        // Iterate over ad_rejection_reasons to find the appropriate rejection
        // reason.
        for (const auto& rejection : high_score.ad_rejection_reasons()) {
          if (rejection.interest_group_owner() == ig_owner &&
              rejection.interest_group_name() == ig_name) {
            base_values.reject_reason = rejection.rejection_reason();
            break;
          }
        }
      }
      if (ad_with_bid.private_aggregation_contributions().empty()) {
        continue;
      }

      std::vector<PrivateAggregateContribution>
          processed_filtered_contributions =
              GetProcessedAndFilteredContributions(ad_with_bid, base_values);

      for (PrivateAggregateContribution& contribution :
           processed_filtered_contributions) {
        contribution.set_ig_idx(ig_idx);
        *reporting_response.add_contributions() = std::move(contribution);
      }
    }
    if (!reporting_response.contributions().empty()) {
      *high_score.add_top_level_contributions() = std::move(reporting_response);
    }
  }
}

// Groups PrivateAggregateContributions per AdTech
ContributionsPerAdTechMap GroupContributionsByAdTech(
    int per_adtech_paapi_contributions_limit,
    const PrivateAggregateReportingResponses& responses) {
  absl::flat_hash_map<std::string,
                      std::vector<const PrivateAggregateContribution*>>
      contributions_map;
  for (const auto& response : responses) {
    const std::string& adtech_origin = response.adtech_origin();
    auto& contributions_list =
        contributions_map
            .try_emplace(adtech_origin,
                         std::vector<const PrivateAggregateContribution*>())
            .first->second;
    for (const auto& contribution : response.contributions()) {
      if (contributions_list.size() < per_adtech_paapi_contributions_limit) {
        contributions_list.push_back(&contribution);
      }
    }
  }
  return contributions_map;
}

std::string ConvertIntArrayToByteString(
    const PrivateAggregationBucket& bucket) {
  int64_t lower_int = 0;
  int64_t upper_int = 0;

  if (bucket.bucket_128_bit().bucket_128_bits_size() >= 1) {
    lower_int = absl::big_endian::FromHost64(
        bucket.bucket_128_bit().bucket_128_bits(0));
  }
  if (bucket.bucket_128_bit().bucket_128_bits_size() == 2) {
    upper_int = absl::big_endian::FromHost64(
        bucket.bucket_128_bit().bucket_128_bits(1));
  }

  return absl::StrJoin(
      {upper_int, lower_int}, "", [](std::string* out, int64_t val) {
        out->append(absl::string_view((char*)(&val), sizeof(val)));
      });
}

absl::Status CborSerializePAggBucket(const PrivateAggregationBucket& bucket,
                                     ErrorHandler error_handler,
                                     cbor_item_t& root) {
  if (!bucket.has_bucket_128_bit()) {
    return absl::InternalError(
        "Error serializing PrivateAggregationBucket. Bucket128Bit not "
        "present.");
  }
  std::string bucket_bytes = ConvertIntArrayToByteString(bucket);
  PS_RETURN_IF_ERROR(
      CborSerializeByteString(kBucket, bucket_bytes, error_handler, root));
  return absl::OkStatus();
}

absl::Status CborSerializePAggValue(const PrivateAggregationValue& value,
                                    ErrorHandler error_handler,
                                    cbor_item_t& root) {
  if (!value.has_int_value()) {
    return absl::InternalError(
        "Error serializing PrivateAggregationValue. int_value is not present.");
  }
  PS_RETURN_IF_ERROR(
      CborSerializeInt(kValue, value.int_value(), error_handler, root));
  return absl::OkStatus();
}

absl::Status CborSerializePAggContribution(
    const PrivateAggregateContribution& contribution,
    ErrorHandler error_handler, cbor_item_t& root) {
  if (!contribution.has_bucket() || !contribution.has_value()) {
    return absl::InternalError(
        "Error serializing PrivateAggregateContribution. Missing bucket or "
        "value.");
  }
  PS_RETURN_IF_ERROR(
      CborSerializePAggValue(contribution.value(), error_handler, root));
  PS_RETURN_IF_ERROR(
      CborSerializePAggBucket(contribution.bucket(), error_handler, root));
  return absl::OkStatus();
}

absl::Status CborSerializePAggEventContributions(
    const std::vector<const PrivateAggregateContribution*>& contributions,
    ErrorHandler error_handler, cbor_item_t& root) {
  // Group contributions by event type.
  ContributionsPerEventTypeMap grouped_contributions;
  for (const PrivateAggregateContribution* contribution : contributions) {
    grouped_contributions[GetEvent(contribution)].push_back(contribution);
  }
  ScopedCbor all_serialized_event_contributions(
      cbor_new_definite_array(grouped_contributions.size()));
  for (const auto& [event_name, contributions] : grouped_contributions) {
    ScopedCbor serialized_event_contributions(
        cbor_new_definite_map(kNumEventContributionKeys));
    // If the event does not start with "reserved.", it is a custom event.
    // Only custome event's name should be set in the response.
    if (strncmp(event_name.data(), kReservedPrefix.data(),
                kReservedPrefix.length()) != 0) {
      PS_RETURN_IF_ERROR(CborSerializeString(kEvent, event_name, error_handler,
                                             **serialized_event_contributions));
    }
    absl::Status contribution_status = CborSerializePAggContributionList(
        contributions, error_handler, **serialized_event_contributions);
    if (!contribution_status.ok()) {
      PS_LOG(ERROR) << "Failed to serialize list of contributions in "
                       "paggEventContributions"
                    << contribution_status;
      continue;
    }
    if (!cbor_array_push(*all_serialized_event_contributions,
                         *serialized_event_contributions)) {
      PS_LOG(ERROR) << "Failed to serialize list of contributions in "
                       "paggEventContributions";
      continue;
    }
  }
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(kEventContributions,
                                          sizeof(kEventContributions) - 1)),
      .value = *all_serialized_event_contributions};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        absl::StrCat("Failed to serialize paggEventContributions to CBOR")));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<PrivateAggregateContribution>>
CborDecodePAggEventContributions(
    std::optional<int> ig_idx,
    cbor_item_t& serialized_pagg_event_contributions) {
  absl::Span<cbor_item_t*> event_contributions(
      cbor_array_handle(&serialized_pagg_event_contributions),
      cbor_array_size(&serialized_pagg_event_contributions));
  std::vector<PrivateAggregateContribution> pagg_contributions;
  for (size_t i = 0; i < cbor_array_size(&serialized_pagg_event_contributions);
       i++) {
    std::string event_name;
    absl::Span<struct cbor_pair> contribution_map(
        cbor_map_handle(event_contributions[i]),
        cbor_map_size(event_contributions[i]));
    for (const cbor_pair& event_contributions_pair : contribution_map) {
      std::string event_contributions_key;
      PS_ASSIGN_OR_RETURN(
          event_contributions_key,
          CheckTypeAndCborDecodeString(event_contributions_pair.key));

      const int index =
          FindItemIndex(kPaggEventContributionKeys, event_contributions_key);
      switch (index) {
        case 0: {  // kEvent
          PS_ASSIGN_OR_RETURN(event_name, CheckTypeAndCborDecodeString(
                                              event_contributions_pair.value));
          break;
        }
        case 1: {  // kContributions
          if (!cbor_isa_array(event_contributions_pair.value)) {
            return absl::InvalidArgumentError(
                "Error decoding PrivateAggregationContributions");
          }
          absl::StatusOr<std::vector<PrivateAggregateContribution>>
              contributions = CborDecodePAggContributionList(
                  event_name, ig_idx, *event_contributions_pair.value);
          pagg_contributions.insert(
              pagg_contributions.end(),
              std::make_move_iterator(contributions->begin()),
              std::make_move_iterator(contributions->end()));
          break;
        }
        default:
          PS_VLOG(5)
              << "Serialized CBOR paggEventContribution has unexpected key: "
              << event_contributions_key;
      }
    }
  }
  return pagg_contributions;
}

absl::Status CborSerializeIgContributions(
    const std::vector<const PrivateAggregateContribution*>& contributions,
    ErrorHandler error_handler, cbor_item_t& root) {
  // Group contributions by ig type.
  ContributionsPerIgTypeMap grouped_contributions;
  std::vector<const PrivateAggregateContribution*> contributions_with_no_ig_idx;
  for (const PrivateAggregateContribution* contribution : contributions) {
    if (contribution->has_ig_idx()) {
      grouped_contributions[contribution->ig_idx()].push_back(contribution);
    } else {
      contributions_with_no_ig_idx.push_back(contribution);
    }
  }
  int ig_contributions_size = 0;
  if (!grouped_contributions.empty()) {
    ig_contributions_size = grouped_contributions.size();
  }
  if (!contributions_with_no_ig_idx.empty()) {
    ig_contributions_size++;
  }
  ScopedCbor all_serialized_ig_contributions(
      cbor_new_definite_array(ig_contributions_size));
  for (const auto& [ig_idx, contributions] : grouped_contributions) {
    ScopedCbor serialized_ig_contributions(
        cbor_new_definite_map(kNumIgContributionKeys));
    PS_RETURN_IF_ERROR(CborSerializeInt(kIgIndex, ig_idx, error_handler,
                                        **serialized_ig_contributions));
    absl::Status contribution_status = CborSerializePAggEventContributions(
        contributions, error_handler, **serialized_ig_contributions);
    if (!contribution_status.ok()) {
      PS_LOG(ERROR) << "Failed to serialize list of contributions in "
                       "igContributions: "
                    << contribution_status;
      continue;
    }
    if (!cbor_array_push(*all_serialized_ig_contributions,
                         *serialized_ig_contributions)) {
      PS_LOG(ERROR) << "Failed to serialize list of contributions in "
                       "igContributions";
      continue;
    }
  }
  if (!contributions_with_no_ig_idx.empty()) {
    ScopedCbor serialized_ig_contributions(
        cbor_new_definite_map(kNumIgContributionKeysWithoutIgIdx));
    absl::Status contribution_status = CborSerializePAggEventContributions(
        contributions_with_no_ig_idx, error_handler,
        **serialized_ig_contributions);
    if (!contribution_status.ok()) {
      PS_LOG(ERROR) << "Failed to serialize list of contributions in "
                       "igContributions: "
                    << contribution_status;
    }
    if (!cbor_array_push(*all_serialized_ig_contributions,
                         *serialized_ig_contributions)) {
      PS_LOG(ERROR) << "Failed to serialize list of contributions in "
                       "igContributions";
      error_handler(grpc::Status(
          grpc::INTERNAL,
          absl::StrCat("Failed to serialize igContributions to CBOR")));
      return absl::InternalError("");
    }
  }

  struct cbor_pair kv = {.key = cbor_move(cbor_build_stringn(
                             kIgContributions, sizeof(kIgContributions) - 1)),
                         .value = *all_serialized_ig_contributions};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        absl::StrCat("Failed to serialize igContributions to CBOR")));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<PrivateAggregateContribution>>
CborDecodePAggIgContributions(cbor_item_t& serialized_ig_contributions) {
  cbor_item_t* serialized_ig_contributions_ptr = &serialized_ig_contributions;
  absl::Span<cbor_item_t*> ig_contributions(
      cbor_array_handle(serialized_ig_contributions_ptr),
      cbor_array_size(serialized_ig_contributions_ptr));
  std::vector<PrivateAggregateContribution> pagg_contributions;
  for (int i = 0; i < cbor_array_size(serialized_ig_contributions_ptr); i++) {
    std::optional<int> ig_idx;
    absl::Span<struct cbor_pair> contribution_map(
        cbor_map_handle(ig_contributions[i]),
        cbor_map_size(ig_contributions[i]));
    for (const cbor_pair& ig_contributions_pair : contribution_map) {
      std::string ig_contributions_key;
      PS_ASSIGN_OR_RETURN(ig_contributions_key, CheckTypeAndCborDecodeString(
                                                    ig_contributions_pair.key));

      const int index =
          FindItemIndex(kPaggIgContributionKeys, ig_contributions_key);
      switch (index) {
        case 0: {  // kIgIndex
          if (!cbor_is_int(ig_contributions_pair.value)) {
            return absl::InvalidArgumentError(
                "Error decoding ig_index in igContributions");
          }
          ig_idx = cbor_get_int(ig_contributions_pair.value);
          break;
        }
        case 1: {  // kEventContributions
          if (!cbor_isa_array(ig_contributions_pair.value)) {
            return absl::InvalidArgumentError(
                "Error decoding eventContributions in igContributions");
          }
          absl::StatusOr<std::vector<PrivateAggregateContribution>>
              contributions = CborDecodePAggEventContributions(
                  ig_idx, *ig_contributions_pair.value);
          pagg_contributions.insert(
              pagg_contributions.end(),
              std::make_move_iterator(contributions->begin()),
              std::make_move_iterator(contributions->end()));
          break;
        }
        default:
          PS_VLOG(5) << "Serialized CBOR igContributions has unexpected key: "
                     << ig_contributions_key;
      }
    }
  }
  return pagg_contributions;
}

absl::Status CborSerializePAggResponse(
    const PrivateAggregateReportingResponses& responses,
    int per_adtech_paapi_contributions_limit, ErrorHandler error_handler,
    cbor_item_t& root) {
  // Group contributions by AdTech.
  ContributionsPerAdTechMap grouped_contributions = GroupContributionsByAdTech(
      per_adtech_paapi_contributions_limit, responses);
  ScopedCbor all_serialized_adtech_contributions(
      cbor_new_definite_array(grouped_contributions.size()));
  for (const auto& [adtech_origin, contributions] : grouped_contributions) {
    ScopedCbor serialized_adtech_contributions(
        cbor_new_definite_map(kNumPAggResponseKeys));
    absl::Status contribution_status = CborSerializeIgContributions(
        contributions, error_handler, **serialized_adtech_contributions);
    if (!contribution_status.ok()) {
      PS_LOG(ERROR) << "Failed to serialize list of contributions in "
                       "paggResponse:"
                    << contribution_status;
      continue;
    }
    PS_RETURN_IF_ERROR(CborSerializeString(kReportingOrigin, adtech_origin,
                                           error_handler,
                                           **serialized_adtech_contributions));
    if (!cbor_array_push(*all_serialized_adtech_contributions,
                         *serialized_adtech_contributions)) {
      PS_LOG(ERROR) << "Failed to serialize list of contributions in "
                       "paggResponse";
      continue;
    }
  }
  if (grouped_contributions.empty() ||
      cbor_array_size(*all_serialized_adtech_contributions) == 0) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        absl::StrCat("No contributions to serialize in paggResponse")));
    return absl::InternalError("");
  }
  struct cbor_pair kv = {.key = cbor_move(cbor_build_stringn(
                             kPAggResponse, sizeof(kPAggResponse) - 1)),
                         .value = *all_serialized_adtech_contributions};
  if (!cbor_map_add(&root, kv)) {
    error_handler(
        grpc::Status(grpc::INTERNAL,
                     absl::StrCat("Failed to serialize paggResponse to CBOR")));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::StatusOr<PrivateAggregateReportingResponses> CborDecodePAggResponse(
    cbor_item_t& serialized_adtech_contributions) {
  cbor_item_t* serialized_adtech_contributions_ptr =
      &serialized_adtech_contributions;
  if (!cbor_isa_array(serialized_adtech_contributions_ptr)) {
    return absl::InternalError(
        "Unexpected cbor found for serialized_adtech_contributions. Expected "
        "type is an array");
  }
  absl::Span<cbor_item_t*> adtech_contributions(
      cbor_array_handle(serialized_adtech_contributions_ptr),
      cbor_array_size(serialized_adtech_contributions_ptr));

  PrivateAggregateReportingResponses pagg_responses;
  for (int i = 0; i < cbor_array_size(serialized_adtech_contributions_ptr);
       i++) {
    absl::Span<struct cbor_pair> contribution_map(
        cbor_map_handle(adtech_contributions[i]),
        cbor_map_size(adtech_contributions[i]));
    PrivateAggregateReportingResponse pagg_response;
    for (const cbor_pair& adtech_contributions_pair : contribution_map) {
      std::string adtech_contributions_key;
      PS_ASSIGN_OR_RETURN(
          adtech_contributions_key,
          CheckTypeAndCborDecodeString(adtech_contributions_pair.key));
      const int index =
          FindItemIndex(kPaggResponseKeys, adtech_contributions_key);
      switch (index) {
        case 0: {  // kIgContributions
          if (!cbor_isa_array(adtech_contributions_pair.value)) {
            return absl::InvalidArgumentError("Error decoding igContributions");
          }
          absl::StatusOr<std::vector<PrivateAggregateContribution>>
              contributions = CborDecodePAggIgContributions(
                  *adtech_contributions_pair.value);
          if (!contributions.ok()) {
            PS_LOG(ERROR) << "Failed to decode list of contributions in "
                             "paggResponse:"
                          << contributions.status();
            continue;
          }
          pagg_response.mutable_contributions()->Add(
              std::make_move_iterator((*contributions).begin()),
              std::make_move_iterator((*contributions).end()));
          break;
        }
        case 1: {  // kReportingOrigin
          if (!cbor_isa_string(adtech_contributions_pair.value)) {
            return absl::InvalidArgumentError(
                "Error decoding reportingOrigin in paggResponse");
          }
          pagg_response.set_adtech_origin(
              CborDecodeString(adtech_contributions_pair.value));
          break;
        }
        default:
          PS_VLOG(5) << "Serialized CBOR paggResponse has unexpected key: "
                     << adtech_contributions_key;
      }
    }
    pagg_responses.Add(std::move(pagg_response));
  }
  return pagg_responses;
}

void HandlePrivateAggregationContributionsForGhostWinner(
    const absl::flat_hash_map<InterestGroupIdentity, int>&
        interest_group_index_map,
    ScoreAdsResponse::AdScore& ghost_winning_score,
    BuyerBidsResponseMap& shared_buyer_bids_map) {
  for (const auto& [ig_owner, get_bid_response] : shared_buyer_bids_map) {
    if (ig_owner != ghost_winning_score.interest_group_owner()) {
      continue;
    }
    PrivateAggregateReportingResponse reporting_response;
    reporting_response.set_adtech_origin(ig_owner);

    for (auto& ad_with_bid : *get_bid_response->mutable_bids()) {
      if (ad_with_bid.private_aggregation_contributions().empty()) {
        continue;
      }
      const std::string& ig_name = ad_with_bid.interest_group_name();
      if (ig_name != ghost_winning_score.interest_group_name()) {
        continue;
      }
      int ig_idx = 0;
      InterestGroupIdentity ig = {.interest_group_owner = ig_owner,
                                  .interest_group_name = ig_name};
      const auto& ig_itr = interest_group_index_map.find(ig);
      if (ig_itr != interest_group_index_map.end()) {
        ig_idx = ig_itr->second;
      } else {
        continue;
      }

      // Filtering.
      FilterLossContributions(ad_with_bid);
      if (ad_with_bid.private_aggregation_contributions().empty()) {
        continue;
      }
      BaseValues base_values;
      base_values.reject_reason =
          SellerRejectionReason::DID_NOT_MEET_THE_KANONYMITY_THRESHOLD;
      std::vector<PrivateAggregateContribution>
          processed_filtered_contributions =
              GetProcessedAndFilteredContributions(ad_with_bid, base_values);
      if (!processed_filtered_contributions.empty()) {
        for (auto& contribution : processed_filtered_contributions) {
          contribution.set_ig_idx(ig_idx);
          *reporting_response.add_contributions() = std::move(contribution);
        }
        *ghost_winning_score.add_top_level_contributions() = reporting_response;
        break;
      }
    }
    if (!ghost_winning_score.top_level_contributions().empty()) {
      break;
    }
  }
}
}  // namespace privacy_sandbox::bidding_auction_servers
