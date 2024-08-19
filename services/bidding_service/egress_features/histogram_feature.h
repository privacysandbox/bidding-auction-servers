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

#ifndef SERVICES_BIDDING_SERVICE_EGRESS_FEATURES_HISTOGRAM_FEATURE_H_
#define SERVICES_BIDDING_SERVICE_EGRESS_FEATURES_HISTOGRAM_FEATURE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "include/rapidjson/document.h"
#include "services/bidding_service/egress_features/egress_feature.h"

namespace privacy_sandbox::bidding_auction_servers {

// Class representing a histogram feature type.
class HistogramFeature : public EgressFeature {
 public:
  ~HistogramFeature() override = default;
  explicit HistogramFeature(uint32_t size,
                            std::shared_ptr<rapidjson::Value> schema_value);
  explicit HistogramFeature(const HistogramFeature& other);
  uint32_t Size() const override;

  absl::string_view Type() const override;

  // Converts the egress feature into a byte-string.
  absl::StatusOr<std::vector<bool>> Serialize() override;

  absl::StatusOr<std::unique_ptr<EgressFeature>> Copy() const override;

 private:
  // Size of histogram in bits. This is calculated using the
  // schema of the histogram feature.
  mutable std::optional<uint32_t> histogram_size_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_EGRESS_FEATURES_HISTOGRAM_FEATURE_H_
