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

#ifndef SERVICES_BIDDING_SERVICE_EGRESS_FEATURES_UNSIGNED_INT_FEATURE_H_
#define SERVICES_BIDDING_SERVICE_EGRESS_FEATURES_UNSIGNED_INT_FEATURE_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "services/bidding_service/egress_features/egress_feature.h"

namespace privacy_sandbox::bidding_auction_servers {

// Class representing an unsigned feature type.
class UnsignedIntFeature : public EgressFeature {
 public:
  ~UnsignedIntFeature() override = default;
  explicit UnsignedIntFeature(uint32_t size);
  explicit UnsignedIntFeature(const UnsignedIntFeature& other);

  absl::string_view Type() const override;

  absl::StatusOr<std::vector<bool>> Serialize() override;

  absl::StatusOr<std::unique_ptr<EgressFeature>> Copy() const override;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_EGRESS_FEATURES_UNSIGNED_INT_FEATURE_H_