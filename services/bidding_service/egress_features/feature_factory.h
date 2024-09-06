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

#ifndef SERVICES_BIDDING_SERVICE_EGRESS_FEATURE_FACTORY_H_
#define SERVICES_BIDDING_SERVICE_EGRESS_FEATURE_FACTORY_H_

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "include/rapidjson/document.h"
#include "services/bidding_service/egress_features/egress_feature.h"

namespace privacy_sandbox::bidding_auction_servers {

// Creates an egress feature instance based on the name and the size.
absl::StatusOr<std::unique_ptr<EgressFeature>> CreateEgressFeature(
    absl::string_view name, uint32_t size,
    std::shared_ptr<rapidjson::Value> value);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_EGRESS_FEATURE_FACTORY_H_
