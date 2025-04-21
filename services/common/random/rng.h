/*
 * Copyright 2025 Google LLC
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

#ifndef SERVICES_COMMON_RANDOM_RNG_H
#define SERVICES_COMMON_RANDOM_RNG_H

#include <algorithm>
#include <memory>
#include <random>
#include <vector>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// Wrapper over std::mt19937 that can be seeded for deterministic RNG.
class RandomNumberGenerator {
 public:
  RandomNumberGenerator() = default;
  explicit RandomNumberGenerator(std::size_t seed) : generator_(seed) {}
  virtual ~RandomNumberGenerator() {}

  // Returns a uniform random integer within the inclusive range [lower, upper].
  virtual int GetUniformInt(int lower, int upper);

  // Returns a uniform random double within the range [lower, upper).
  virtual double GetUniformReal(double lower, double upper);

  // Shuffles the elements in the vector.
  virtual void Shuffle(std::vector<absl::string_view>& vector);

  // Shuffles the elements in the vector.
  template <typename T>
  void Shuffle(std::vector<T>& vector) {
    std::shuffle(vector.begin(), vector.end(), generator_);
  }

  // Performs random sampling based on the provided rate in parts per million.
  virtual bool RandomSample(int sample_rate_micro);

 private:
  std::mt19937 generator_;
};

class RandomNumberGeneratorFactory {
 public:
  RandomNumberGeneratorFactory() = default;
  virtual ~RandomNumberGeneratorFactory() = default;

  virtual std::unique_ptr<RandomNumberGenerator> CreateRng(
      std::size_t seed) const;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_RANDOM_RNG_H
