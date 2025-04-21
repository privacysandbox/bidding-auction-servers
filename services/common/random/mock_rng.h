// Copyright 2025 Google LLC
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
// See the License for the specific language governing per21missions and
// limitations under the License.

#ifndef SERVICES_COMMON_RANDOM_MOCK_RNG_H
#define SERVICES_COMMON_RANDOM_MOCK_RNG_H

#include <gmock/gmock.h>

#include <memory>
#include <vector>

#include "include/gtest/gtest.h"
#include "services/common/random/rng.h"

namespace privacy_sandbox::bidding_auction_servers {

class MockRandomNumberGenerator : public RandomNumberGenerator {
 public:
  MockRandomNumberGenerator() : RandomNumberGenerator() {}
  ~MockRandomNumberGenerator() override = default;

  MOCK_METHOD(int, GetUniformInt, (int lower, int upper), (override));
  MOCK_METHOD(double, GetUniformReal, (double lower, double upper), (override));
  MOCK_METHOD(void, Shuffle, (std::vector<absl::string_view> & vector),
              (override));
  MOCK_METHOD(bool, RandomSample, (int sample_rate_micro), (override));
};

class MockRandomNumberGeneratorFactory : public RandomNumberGeneratorFactory {
 public:
  MockRandomNumberGeneratorFactory() : RandomNumberGeneratorFactory() {}
  ~MockRandomNumberGeneratorFactory() override = default;

  MOCK_METHOD(std::unique_ptr<RandomNumberGenerator>, CreateRng,
              (std::size_t seed), (const override));
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_RANDOM_MOCK_RNG_H
