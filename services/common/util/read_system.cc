// Copyright 2023 Google LLC
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

#include "services/common/util/read_system.h"

#include <sys/sysinfo.h>

#include <cstring>

#include "absl/log/absl_log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace privacy_sandbox::server_common {

absl::flat_hash_map<std::string, double> GetCpu() {
  struct sysinfo info;
  memset(&info, 0, sizeof(info));
  absl::flat_hash_map<std::string, double> ret;
  if (sysinfo(&info) < 0) {
    ABSL_LOG_EVERY_N_SEC(ERROR, 600) << strerror(errno);
    return ret;
  }
  ret["sever total"] =
      (1.0 * info.loads[0] / (1 << SI_LOAD_SHIFT)) / get_nprocs();
  return ret;
}

absl::flat_hash_map<std::string, double> GetMemory() {
  absl::flat_hash_map<std::string, double> ret;
  char buff[256];
  char name[64];
  int64_t value = 0;
  char unit[16];
  FILE* fd = fopen("/proc/meminfo", "r");
  if (fd == nullptr) {
    ABSL_LOG_EVERY_N_SEC(ERROR, 600) << strerror(errno);
    return ret;
  }
  while (ret.size() < 2 && fgets(buff, sizeof(buff), fd) != nullptr) {
    if (absl::StartsWith(buff, "MemTotal") ||
        absl::StartsWith(buff, "MemAvailable")) {
      sscanf(buff, "%s %lu %s", name, &value, unit);
      ret[name] = value;
    }
  }
  fclose(fd);

  fd = fopen("/proc/self/status", "r");
  if (fd == nullptr) {
    ABSL_LOG_EVERY_N_SEC(ERROR, 600) << strerror(errno);
    return ret;
  }

  while (fgets(buff, sizeof(buff), fd) != nullptr) {
    if (absl::StartsWith(buff, "VmRSS")) {
      sscanf(buff, "%s %lu %s", name, &value, unit);
      ret["main process"] = value;
      break;
    }
  }
  fclose(fd);
  return ret;
}

}  // namespace privacy_sandbox::server_common
