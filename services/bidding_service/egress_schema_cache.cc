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

#include "services/bidding_service/egress_schema_cache.h"

#include <cstdint>
#include <utility>

#include "absl/strings/string_view.h"
#include "include/rapidjson/document.h"
#include "rapidjson/document.h"
#include "services/bidding_service/egress_features/feature_factory.h"
#include "services/bidding_service/utils/egress.h"
#include "services/common/util/json_util.h"
#include "src/logger/request_context_impl.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

inline constexpr absl::string_view kCddlVersionKey = "cddl_version";
inline constexpr absl::string_view kSchemaVersionKey = "version";

// Looks at the validated JSON schema for adtech, extracts and returns all such
// feature objects.
absl::StatusOr<std::vector<std::unique_ptr<EgressFeature>>> ExtractFeatures(
    rapidjson::Document& json_doc) {
  PS_ASSIGN_OR_RETURN(auto feature_array, GetArrayMember(json_doc, "features"));
  std::vector<std::unique_ptr<EgressFeature>> extracted_features;
  extracted_features.reserve(feature_array.Size());
  int idx = 0;
  for (rapidjson::Value& feat : feature_array) {
    if (!feat.IsObject()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Feature at index ", idx, " is not an object, expected object"));
    }
    std::string name;
    uint32_t size = 1;  // Defaulting to size of a boolean
    PS_ASSIGN_IF_PRESENT(name, feat, "name", String);
    DCHECK(!name.empty());
    PS_ASSIGN_IF_PRESENT(size, feat, "size", Uint);
    PS_VLOG(5) << "Feature name: " << name << ", size: " << size;
    auto shared_schema = std::make_shared<rapidjson::Document>();
    shared_schema->CopyFrom(feat, shared_schema->GetAllocator());
    PS_ASSIGN_OR_RETURN(auto extracted_feat,
                        CreateEgressFeature(name, size, shared_schema));
    PS_VLOG(5) << "Feature name: " << name
               << ", size: " << extracted_feat->Size();
    extracted_features.emplace_back(std::move(extracted_feat));
    idx++;
  }
  return extracted_features;
}

}  // namespace

absl::Status EgressSchemaCache::Update(absl::string_view egress_schema,
                                       absl::string_view id)
    ABSL_LOCKS_EXCLUDED(mu_) {
  PS_ASSIGN_OR_RETURN(auto json_doc, ParseJsonString(egress_schema));
  PS_ASSIGN_OR_RETURN(int schema_version,
                      GetIntMember(json_doc, kSchemaVersionKey));
  PS_ASSIGN_OR_RETURN(std::string cddl_version,
                      GetStringMember(json_doc, kCddlVersionKey));
  PS_ASSIGN_OR_RETURN(auto cddl_spec, cddl_spec_cache_->Get(cddl_version));
  if (!AdtechEgresSchemaValid(egress_schema, cddl_spec)) {
    std::string err =
        absl::StrCat("Adtech egress schema id: ", id,
                     " doesn't conform with the CDDL spec: ", cddl_version);
    PS_VLOG(5) << err;
    return absl::InvalidArgumentError(std::move(err));
  }
  PS_ASSIGN_OR_RETURN(auto extracted_features, ExtractFeatures(json_doc));
  // If validation passes, extract the features from the schema into Feature
  // objects that can be used by the bidding service when it has to serialize
  // the features.
  absl::MutexLock lock(&mu_);
  version_features_[std::string(id)] =
      EgressSchemaData({.version = static_cast<uint32_t>(schema_version),
                        .features = std::move(extracted_features)});
  return absl::OkStatus();
}

EgressSchemaCache::EgressSchemaCache(
    std::unique_ptr<const CddlSpecCache> cddl_spec_cache)
    : cddl_spec_cache_(std::move(cddl_spec_cache)) {}

absl::StatusOr<EgressSchemaData> EgressSchemaCache::Get(
    absl::string_view schema_id) ABSL_LOCKS_EXCLUDED(mu_) {
  PS_VLOG(5) << "Getting schema id: " << schema_id;
  absl::ReaderMutexLock lock(&mu_);
  // If version exists, returns the previously parsed features to the caller.
  // We need to ensure that we return a copy of the schema here since the
  // caller will be updating it later on.
  auto it = version_features_.find(schema_id);
  if (it == version_features_.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Adtech schema version not found in cache: ", schema_id));
  }
  // We have to return a copy here because this cache will be called multiple
  // times by the reactors and the returned features will be updated and we
  // don't want the requests to see each other's updates.
  const int num_features = it->second.features.size();
  std::vector<std::unique_ptr<EgressFeature>> egress_features;
  egress_features.reserve(num_features);
  PS_VLOG(5) << "Creating copies of " << num_features << " egress features";
  int idx = 0;
  for (const auto& feat : it->second.features) {
    PS_VLOG(5) << "Copying feature at index: " << idx++;
    PS_ASSIGN_OR_RETURN(auto feat_copy, feat->Copy());
    egress_features.emplace_back(std::move(feat_copy));
  }
  return EgressSchemaData(
      {.version = it->second.version, .features = std::move(egress_features)});
}

}  // namespace privacy_sandbox::bidding_auction_servers
