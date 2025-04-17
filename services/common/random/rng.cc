// Copyright 2025 Google LLC
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

#include "services/common/random/rng.h"

#include <vector>

#include "absl/random/bit_gen_ref.h"
#include "absl/random/discrete_distribution.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"

namespace privacy_sandbox::bidding_auction_servers {

int RandomNumberGenerator::GetUniformInt(int lower, int upper) {
  return std::uniform_int_distribution<int>(lower, upper)(generator_);
}

double RandomNumberGenerator::GetUniformReal(double lower, double upper) {
  return std::uniform_real_distribution<double>(lower, upper)(generator_);
}

void RandomNumberGenerator::Shuffle(std::vector<absl::string_view>& vector) {
  std::shuffle(vector.begin(), vector.end(), generator_);
}

bool RandomNumberGenerator::RandomSample(int sample_rate_micro) {
  absl::discrete_distribution dist(
      {1e6 - sample_rate_micro, (double)sample_rate_micro});
  absl::BitGenRef bit_gen_ref(generator_);
  return dist(bit_gen_ref);
}

std::unique_ptr<RandomNumberGenerator> RandomNumberGeneratorFactory::CreateRng(
    std::size_t seed) const {
  return std::make_unique<RandomNumberGenerator>(seed);
}

}  // namespace privacy_sandbox::bidding_auction_servers
