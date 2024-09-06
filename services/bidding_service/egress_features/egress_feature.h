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

#ifndef SERVICES_BIDDING_SERVICE_EGRESS_FEATURE_H_
#define SERVICES_BIDDING_SERVICE_EGRESS_FEATURE_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "include/rapidjson/document.h"

namespace privacy_sandbox::bidding_auction_servers {

// EgressFeature class encapsulates logic for accumulating, noising and
// serializing the features.
class EgressFeature {
 public:
  explicit EgressFeature(uint32_t size);
  explicit EgressFeature(uint32_t size,
                         std::shared_ptr<rapidjson::Value> schema_value);
  EgressFeature(const EgressFeature& other);
  virtual ~EgressFeature() = default;

  // Returns the maximum size of the feature in terms of number of bits.
  virtual uint32_t Size() const;

  // Gets type of the feature.
  virtual absl::string_view Type() const = 0;

  // Sets the value of the egress feature on this object.
  // Type verification can be skipped for homogeneous composite types (e.g.
  // a bucket which contains all bools).
  virtual absl::Status SetValue(rapidjson::Value value,
                                bool verify_type = true);

  // Converts the egress feature into a byte-string.
  virtual absl::StatusOr<std::vector<bool>> Serialize() = 0;

  // Returns the copy of the derived egress feature.
  virtual absl::StatusOr<std::unique_ptr<EgressFeature>> Copy() const = 0;

 protected:
  uint32_t size_ = 1;
  std::shared_ptr<rapidjson::Value> schema_value_ = nullptr;
  bool is_value_set_ = false;
  rapidjson::Value value_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_EGRESS_FEATURE_H_
