//  Copyright 2022 Google LLC
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

#ifndef SERVICES_COMMON_METRIC_DEFINITION_H_
#define SERVICES_COMMON_METRIC_DEFINITION_H_

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"

// Defines metric `Definition`, and a list of common metrics.
namespace privacy_sandbox::server_common::metric {

enum class Privacy { kNonImpacting, kImpacting };

enum class Instrument {
  kUpDownCounter,
  kPartitionedCounter,
  kHistogram,
  kGauge
};

inline constexpr std::array<double, 0> kEmptyHistogramBoundaries = {};
inline constexpr std::array<absl::string_view, 0> kEmptyPublicPartition = {};
inline constexpr absl::string_view kEmptyPartitionType;

struct DefinitionName {
  constexpr explicit DefinitionName(absl::string_view name,
                                    absl::string_view description)
      : name_(name), description_(description) {}

  absl::string_view name_;
  absl::string_view description_;
  absl::Span<const absl::string_view> public_partitions_copy_;
  absl::Span<const double> histogram_boundaries_copy_;
  double privacy_budget_weight_copy_ = 0;
};

namespace internal {
struct Partitioned {
  constexpr explicit Partitioned(
      absl::string_view partition_type = kEmptyPartitionType,
      int max_partitions_contributed = 1,
      absl::Span<const absl::string_view> public_partitions =
          kEmptyPublicPartition)
      : partition_type_(partition_type),
        max_partitions_contributed_(max_partitions_contributed),
        public_partitions_(public_partitions) {}

  absl::string_view partition_type_;
  int max_partitions_contributed_;
  absl::Span<const absl::string_view> public_partitions_;  // must be sorted
};

struct Histogram {
  constexpr explicit Histogram(
      absl::Span<const double> histogram_boundaries = kEmptyHistogramBoundaries)
      : histogram_boundaries_(histogram_boundaries) {}

  absl::Span<const double> histogram_boundaries_;  // must be sorted
};

template <typename T>
struct DifferentialPrivacy {
  constexpr explicit DifferentialPrivacy(
      T upper_bound = std::numeric_limits<T>::max(),
      T lower_bound = std::numeric_limits<T>::min(),
      double privacy_budget_weight = 1.0)
      : upper_bound_(std::max(lower_bound, upper_bound)),
        lower_bound_(std::min(lower_bound, upper_bound)),
        privacy_budget_weight_(privacy_budget_weight) {}

  T upper_bound_;
  T lower_bound_;
  // All Privacy kImpacting metrics split total privacy budget based on their
  // weight. i.e. privacy_budget = total_budget * privacy_budget_weight_ /
  // total_weight
  double privacy_budget_weight_;
};
}  // namespace internal

// `T` can be int or double
// Examples to create `Definition` of different `Privacy` and `Instrument`
//
// Definition<int, Privacy::kNonImpacting, Instrument::kUpDownCounter>
// d1(/*name*/ "d1", /*description*/ "d11");
//
// Use kEmptyPublicPartition for non-public partition.
// absl::string_view public_partitions[] = {"buyer_1", "buyer_2"};
// Definition<int, Privacy::kNonImpacting, Instrument::kPartitionedCounter> d2(
//     /*name*/ "d2", /*description*/ "d21" /*partition_type*/ "buyer_name",
//     /*public_partitions*/ public_partitions);
//
// double histogram_boundaries[] = {1, 2};
// Definition<int, Privacy::kNonImpacting, Instrument::kHistogram> d3(
//     /*name*/ "d3", /*description*/ "d31", /*histogram_boundaries*/ hb);
//
// Definition<int, Privacy::kNonImpacting, Instrument::kGauge> d4(/*name*/
// "d4", /*description*/"d41");
//
// Definition<int, Privacy::kImpacting, Instrument::kUpDownCounter> d5(
//     /*name*/ "d5", /*description*/ "d51", /*upper_bound*/ 9, /*lower_bound*/
//     1);
//
// Definition<int, Privacy::kImpacting, Instrument::kPartitionedCounter> d6(
//     /*name*/ "d6", /*description*/ "d61", /*partition_type*/ "buyer_name",
//     /*max_partitions_contributed*/ 2,
//     /*public_partitions*/ public_partitions,
//     /*upper_bound*/ 9,
//     /*lower_bound*/ 1);
//
// Definition<int, Privacy::kImpacting, Instrument::kHistogram> d7(
//     /*name*/ "d7", /*description*/ "d71", /*histogram_boundaries*/ hb,
//     /*upper_bound*/ 9,
//     /*lower_bound*/ 1);
//
// Their pointers should then be added into a list, which defines the list of
// metrics the server can log. A Span of the list is used to initialize
// 'Context'. For example:
// const DefinitionName* metric_list[] = {&d1, &d2, &d3, &d4, &d5, &d6, &d7};
// absl::Span<const DefinitionName* const> metric_list_span = metric_list;
template <typename T, Privacy privacy, Instrument instrument>
struct Definition : DefinitionName,
                    internal::Partitioned,
                    internal::Histogram,
                    internal::DifferentialPrivacy<T> {
  static_assert(std::is_same<T, int>::value || std::is_same<T, double>::value,
                "T must be int or double");
  using TypeT = T;
  Privacy type_privacy = privacy;
  Instrument type_instrument = instrument;

  using internal::DifferentialPrivacy<T>::upper_bound_;
  using internal::DifferentialPrivacy<T>::lower_bound_;
  using internal::DifferentialPrivacy<T>::privacy_budget_weight_;

  std::string DebugString() const {
    absl::string_view instrument_name;
    switch (instrument) {
      case Instrument::kUpDownCounter:
        instrument_name = "UpDownCounter";
        break;
      case Instrument::kPartitionedCounter:
        instrument_name = "Partitioned UpDownCounter";
        break;
      case Instrument::kHistogram:
        instrument_name = "Histogram";
        break;
      case Instrument::kGauge:
        instrument_name = "Gauge";
        break;
    }
    return absl::Substitute(
        "$0 $1 $2($10) $3 histogram[$4]\n partition by'$5' "
        "max_partitions_contributed:$9 "
        "partition_value[$6]\n bound[$8 ~ $7]",
        name_, description_,
        privacy == Privacy::kNonImpacting ? "Privacy NonImpacting"
                                          : "Privacy Impacting",
        instrument_name, absl::StrJoin(histogram_boundaries_, ","),
        partition_type_, absl::StrJoin(public_partitions_, ","), upper_bound_,
        lower_bound_, max_partitions_contributed_, privacy_budget_weight_);
  }

  template <Privacy non_impact = privacy, Instrument counter = instrument>
  constexpr explicit Definition(
      absl::string_view name, absl::string_view description,
      std::enable_if_t<non_impact == Privacy::kNonImpacting&& counter ==
                       Instrument::kUpDownCounter>* = nullptr)
      : DefinitionName(name, description) {}

  template <Instrument counter = instrument>
  constexpr explicit Definition(
      absl::string_view name, absl::string_view description, T upper_bound,
      T lower_bound,
      std::enable_if_t<counter == Instrument::kUpDownCounter>* = nullptr)
      : DefinitionName(name, description),
        internal::DifferentialPrivacy<T>(upper_bound, lower_bound) {
    privacy_budget_weight_copy_ = privacy_budget_weight_;
  }

  template <Privacy non_impact = privacy,
            Instrument partitioned_counter = instrument>
  constexpr explicit Definition(
      absl::string_view name, absl::string_view description,
      absl::string_view partition_type,
      absl::Span<const absl::string_view> public_partitions,
      std::enable_if_t<non_impact ==
                       Privacy::kNonImpacting&& partitioned_counter ==
                       Instrument::kPartitionedCounter>* = nullptr)
      : DefinitionName(name, description),
        internal::Partitioned(partition_type, INT_MAX, public_partitions) {
    public_partitions_copy_ = public_partitions_;
  }

  template <Instrument partitioned_counter = instrument>
  constexpr explicit Definition(
      absl::string_view name, absl::string_view description,
      absl::string_view partition_type, int max_partitions_contributed,
      absl::Span<const absl::string_view> public_partitions, T upper_bound,
      T lower_bound,
      std::enable_if_t<partitioned_counter ==
                       Instrument::kPartitionedCounter>* = nullptr)
      : DefinitionName(name, description),
        internal::Partitioned(partition_type, max_partitions_contributed,
                              public_partitions),
        internal::DifferentialPrivacy<T>(upper_bound, lower_bound) {
    public_partitions_copy_ = public_partitions_;
    privacy_budget_weight_copy_ = privacy_budget_weight_;
  }

  template <Privacy non_impact = privacy, Instrument histogram = instrument>
  constexpr explicit Definition(
      absl::string_view name, absl::string_view description,
      absl::Span<const double> histogram_boundaries,
      std::enable_if_t<non_impact == Privacy::kNonImpacting&& histogram ==
                       Instrument::kHistogram>* = nullptr)
      : DefinitionName(name, description),
        internal::Histogram(histogram_boundaries) {
    histogram_boundaries_copy_ = histogram_boundaries_;
  }

  template <Instrument histogram = instrument>
  constexpr explicit Definition(
      absl::string_view name, absl::string_view description,
      absl::Span<const double> histogram_boundaries, T upper_bound,
      T lower_bound,
      std::enable_if_t<histogram == Instrument::kHistogram>* = nullptr)
      : DefinitionName(name, description),
        internal::Histogram(histogram_boundaries),
        internal::DifferentialPrivacy<T>(upper_bound, lower_bound) {
    histogram_boundaries_copy_ = histogram_boundaries_;
    privacy_budget_weight_copy_ = privacy_budget_weight_;
  }

  template <Privacy non_impact = privacy, Instrument gauge = instrument>
  constexpr explicit Definition(
      absl::string_view name, absl::string_view description,
      std::enable_if_t<non_impact == Privacy::kNonImpacting&& gauge ==
                       Instrument::kGauge>* = nullptr)
      : DefinitionName(name, description) {}
};

// Checks if a Definition in a list
template <typename T, Privacy privacy, Instrument instrument>
constexpr bool IsInList(const Definition<T, privacy, instrument>& definition,
                        absl::Span<const DefinitionName* const> metric_list) {
  for (auto* m : metric_list) {
    if (&definition == m) {
      return true;
    }
  }
  return false;
}

// List of common metrics used by any servers
inline constexpr Definition<int, Privacy::kNonImpacting,
                            Instrument::kUpDownCounter>
    kTotalRequestFailedCount(
        "kTotalRequestFailedCount",
        "Total number of requests that resulted in failure");

inline constexpr Definition<int, Privacy::kNonImpacting,
                            Instrument::kUpDownCounter>
    kTotalRequestCount("kTotalRequestCounter",
                       "Total number of requests received by the server");

inline constexpr double kTimeHistogram[] = {50, 100, 200, 400, 800};
inline constexpr Definition<int, Privacy::kNonImpacting, Instrument::kHistogram>
    kServerTotalTimeMs("kServerTotalTimeMs",
                       "Total time taken by the server to execute the request",
                       kTimeHistogram);

inline constexpr double kSizeHistogram[] = {0, 20, 40, 80, 100, 200, 400, 800};
inline constexpr Definition<int, Privacy::kNonImpacting, Instrument::kHistogram>
    kResponseByte("kResponseByte", "Response size in bytes", kSizeHistogram);

inline constexpr Definition<int, Privacy::kNonImpacting, Instrument::kHistogram>
    kRequestByte("kRequestByte", "Request size in bytes", kSizeHistogram);
}  // namespace privacy_sandbox::server_common::metric

#endif  // SERVICES_COMMON_METRIC_DEFINITION_H_
