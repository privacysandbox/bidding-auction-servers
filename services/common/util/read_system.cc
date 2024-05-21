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
#include <fstream>
#include <iostream>
#include <vector>

#include "absl/log/absl_log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace privacy_sandbox::server_common {

namespace {

constexpr int kLogInterval = 600;

std::vector<size_t> SystemCpuTime() {
  std::ifstream proc_stat("/proc/stat");
  if (!proc_stat) {
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "failed to open /proc/stat; " << strerror(errno);
    return {};
  }
  proc_stat.ignore(5, ' ');  // Skip the 'cpu' prefix.
  std::vector<size_t> cpu_times;
  for (size_t time; proc_stat >> time; cpu_times.push_back(time)) {
  }
  return cpu_times;
}

std::vector<std::string> SelfStat() {
  std::ifstream proc_self_stat("/proc/self/stat");
  if (!proc_self_stat) {
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "failed to open /proc/self/stat; " << strerror(errno);
    return {};
  }
  std::vector<std::string> self_stat_fields;
  for (std::string field; proc_self_stat >> field;
       self_stat_fields.push_back(field)) {
  }
  return self_stat_fields;
}
}  // namespace

namespace internal {

Utilization ReadCpuTime(const std::vector<size_t>& cpu_times,
                        const std::vector<std::string>& self_stat_fields) {
  static size_t idle_time = 0;
  static size_t total_time = 0;
  static size_t self_time = 0;
  Utilization ret{0, 0};

  constexpr int kCpuIdle = 3;
  if (cpu_times.size() < kCpuIdle + 1) {
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "unknown /proc/stat format " << absl::StrJoin(cpu_times, " ");
    return ret;
  }
  // calculate and update idle_time
  size_t idle_time_diff =
      cpu_times[kCpuIdle] >= idle_time ? cpu_times[kCpuIdle] - idle_time : 0;
  idle_time = cpu_times[kCpuIdle];

  // calculate and update total_time
  size_t total_time_new =
      std::accumulate(cpu_times.begin(), cpu_times.end(), 0UL);
  size_t total_time_diff =
      total_time_new >= total_time ? total_time_new - total_time : 0;
  total_time = total_time_new;

  ret.total = total_time_diff == 0
                  ? 0
                  : 1.0 * (total_time_diff - idle_time_diff) / total_time_diff;

  constexpr int kStatTotal = 52;
  if (self_stat_fields.size() < kStatTotal) {
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "unknown /proc/self/stat format "
        << absl::StrJoin(self_stat_fields, " ");
    return ret;
  }

  constexpr int kUtime = 13, kStime = 14;
  auto utime = std::stoull(self_stat_fields[kUtime]);
  auto stime = std::stoull(self_stat_fields[kStime]);
  // calculate and update self_time
  size_t self_time_new = utime + stime;
  size_t self_time_diff = self_time_new - self_time;
  self_time = self_time_new;
  ret.self = total_time_diff == 0 ? 0 : 1.0 * self_time_diff / total_time_diff;

  return ret;
}

}  // namespace internal

absl::flat_hash_map<std::string, double> GetCpu() {
  struct sysinfo info;
  memset(&info, 0, sizeof(info));
  absl::flat_hash_map<std::string, double> ret;
  if (sysinfo(&info) < 0) {
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "sysinfo() failed; " << strerror(errno);
  } else {
    ret["total load"] =
        (1.0 * info.loads[0] / (1 << SI_LOAD_SHIFT)) / get_nprocs();
  }
  internal::Utilization cpu_utilization =
      internal::ReadCpuTime(SystemCpuTime(), SelfStat());
  ret["total utilization"] = cpu_utilization.total;
  ret["main process utilization"] = cpu_utilization.self;
  ret["total cpu cores"] = static_cast<double>(get_nprocs());
  return ret;
}

absl::flat_hash_map<std::string, double> GetThread() {
  absl::flat_hash_map<std::string, double> ret;

  FILE* fd = fopen("/proc/self/status", "r");
  if (fd == nullptr) {
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "Failed to open /proc/self/status; " << strerror(errno);
    return ret;
  }

  char buff[256];
  while (fgets(buff, sizeof(buff), fd) != nullptr) {
    if (absl::StartsWith(buff, "Threads:")) {
      int thread_count;
      if (sscanf(buff, "Threads: %d", &thread_count) == 1) {
        ret["thread count"] = static_cast<double>(thread_count);
      } else {
        ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
            << "Failed to parse thread count from /proc/self/status";
      }
      break;
    }
  }
  fclose(fd);

  if (ret.empty()) {
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "Thread count information not found in /proc/self/status";
  }
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
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "failed to open /proc/meminfo; " << strerror(errno);
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
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "failed to open /proc/self/status; " << strerror(errno);
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
