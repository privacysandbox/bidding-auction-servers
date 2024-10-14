/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TOOLS_SECURE_INVOKE_FLAGS_H_
#define TOOLS_SECURE_INVOKE_FLAGS_H_

#include <optional>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

ABSL_DECLARE_FLAG(std::string, input_file);
ABSL_DECLARE_FLAG(std::string, input_format);
ABSL_DECLARE_FLAG(std::string, json_input_str);
ABSL_DECLARE_FLAG(std::string, op);
ABSL_DECLARE_FLAG(std::string, host_addr);
ABSL_DECLARE_FLAG(std::string, client_ip);
ABSL_DECLARE_FLAG(std::string, client_accept_language);
ABSL_DECLARE_FLAG(std::string, client_user_agent);
ABSL_DECLARE_FLAG(std::string, client_type);
ABSL_DECLARE_FLAG(bool, insecure);
ABSL_DECLARE_FLAG(std::string, target_service);
ABSL_DECLARE_FLAG(std::string, public_key);
ABSL_DECLARE_FLAG(std::string, private_key);
ABSL_DECLARE_FLAG(std::string, key_id);
ABSL_DECLARE_FLAG(bool, enable_debug_reporting);
ABSL_DECLARE_FLAG(std::optional<bool>, enable_debug_info);
ABSL_DECLARE_FLAG(std::optional<bool>, enable_unlimited_egress);
ABSL_DECLARE_FLAG(std::string, pas_buyer_input_json);

#endif  // TOOLS_SECURE_INVOKE_FLAGS_H_
