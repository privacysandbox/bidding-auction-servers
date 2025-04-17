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

#ifndef SERVICES_COMMON_CLIENTS_CONFIG_TRUSTED_SERVER_CONFIG_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_CONFIG_TRUSTED_SERVER_CONFIG_CLIENT_H_

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/btree_map.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "services/common/loggers/request_log_context.h"
#include "src/core/interface/type_def.h"
#include "src/public/cpio/interface/parameter_client/parameter_client_interface.h"
#include "src/telemetry/flag/telemetry_flag.h"

#include "parc_parameter_client.h"

namespace privacy_sandbox::bidding_auction_servers {

// Sentinel value to be used as the corresponding value for keys in
// client_config_map_ that need to be fetched.
inline constexpr char kEmptyValue[] = "";
inline constexpr char kTrue[] = "true";
inline constexpr char kFalse[] = "false";

// Config client to hold the values for all config flags, from the command line
// or a cloud metadata store. Values from both sources are coalesced into this
// class.
class TrustedServersConfigClient {
 public:
  // Initializes a new instance of TrustedServersConfigClient.
  //
  // @param config_entries_map a map containing <parameter_name,
  // parameter_value> entries for all config flags needed by the service.
  // The values can be set by the client, for example - parsed from
  // command line flags. The class will query the cloud metadata store for each
  // key and if a value is found, it will override the pre-populated values.
  // This provides a way to make the configurations optional and provide
  // default values in the code in case not specified in the cloud metadata.
  // This further enables the cloud metadata to only contain the values that
  // are mandatory or need to be overridden.
  explicit TrustedServersConfigClient(
      absl::Span<const absl::string_view> all_flags);

  absl::Status Init(
      std::string_view config_param_prefix,
      std::unique_ptr<ParcParameterClient> parc_parameter_client) noexcept;

  // Creates and initializes the config client to fetch config values
  // from the cloud metadata store.
  absl::Status Init(std::string_view config_param_prefix,
                    std::unique_ptr<google::scp::cpio::ParameterClientInterface>
                        parameter_client) noexcept;

  // Checks if a parameter is present in the config client.
  bool HasParameter(absl::string_view name) const noexcept;

  // Fetches the string value for the specified config parameter.
  absl::string_view GetStringParameter(absl::string_view name) const noexcept;

  // Fetches the boolean value for the specified config parameter.
  bool GetBooleanParameter(absl::string_view name) const noexcept;

  // Fetches the int value for the specified config parameter.
  int GetIntParameter(absl::string_view name) const noexcept;

  // Fetches the int64 value for the specified config parameter.
  int64_t GetInt64Parameter(absl::string_view name) const noexcept;

  // Fetches the double value for the specified config parameter.
  double GetDoubleParameter(absl::string_view name) const noexcept;

  // Fetches the float value for the specified config parameter.
  float GetFloatParameter(absl::string_view name) const noexcept;

  // Fetches custom flag value for the specified config parameter.
  template <typename T>
  T GetCustomParameter(absl::string_view name) const noexcept {
    T flag_parsed;
    std::string err;
    CHECK(AbslParseFlag(config_entries_map_.at(name), &flag_parsed, &err))
        << err;
    return flag_parsed;
  }

  // Sets `config_entries_map_` if flag is set.
  template <typename T>
  void SetFlag(const absl::Flag<std::optional<T>>& flag,
               absl::string_view config_name) {
    std::optional<T> flag_value = absl::GetFlag(flag);
    if (flag_value) {
      config_entries_map_[config_name] = absl::StrCat(*flag_value);
    }
  }
  template <>
  void SetFlag(const absl::Flag<std::optional<bool>>& flag,
               absl::string_view config_name) {
    std::optional<bool> flag_value = absl::GetFlag(flag);
    if (flag_value) {
      config_entries_map_[config_name] = *flag_value ? kTrue : kFalse;
    }
  }
  template <>
  void SetFlag(
      const absl::Flag<std::optional<server_common::telemetry::TelemetryFlag>>&
          flag,
      absl::string_view config_name) {
    std::optional<server_common::telemetry::TelemetryFlag> flag_value =
        absl::GetFlag(flag);
    if (flag_value) {
      config_entries_map_[config_name] = AbslUnparseFlag(*flag_value);
    }
  }

  // For overriding flag values, regardless of their value on the command line
  // or Terraform. Call this method after Init(), else the value set via this
  // method may be overriden.
  void SetOverride(absl::string_view flag_value,
                   absl::string_view config_name) {
    PS_LOG(INFO, SystemLogContext()) << absl::StrFormat(
        "Overriding flag (flag name: %s, overriden value: %s)", config_name,
        flag_value);
    config_entries_map_[config_name] = flag_value;
  }

  std::string DebugString() {
    return absl::StrJoin(config_entries_map_, "\n", absl::PairFormatter("="));
  }

 private:
  std::unique_ptr<google::scp::cpio::ParameterClientInterface>
      cpio_parameter_client_;
  std::unique_ptr<ParcParameterClient> parc_parameter_client_;

  absl::btree_map<std::string, std::string> config_entries_map_;
  absl::Status InitAndRunConfigClient() noexcept;
};

// Attempts to initialize the configuration client.
//
// This function conditionally initializes the configuration client based on the
// `try_init` flag. If `try_init` is false, it does nothing and returns
// absl::OkStatus(). If `try_init` is true, it attempts to initialize the
// configuration client using either ParcParameterClient (if enable_parc=true)
// or CPIO's ParameterClient.
//
// Args:
//   try_init: A boolean indicating whether to attempt initialization.
//   config_client: The TrustedServersConfigClient instance to initialize.
//   config_param_prefix: A string prefix to prepend to all parameter names
//     when fetching from the parameter store.
//
// Returns:
//   absl::OkStatus() if initialization is successful or if `try_init` is
//   false. Otherwise, returns an error status indicating the reason for
//   initialization failure.
absl::Status MaybeInitConfigClient(bool try_init,
                                   TrustedServersConfigClient& config_client,
                                   absl::string_view config_param_prefix);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_CONFIG_TRUSTED_SERVER_CONFIG_CLIENT_H_
