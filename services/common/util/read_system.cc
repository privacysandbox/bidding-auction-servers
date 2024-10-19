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

void SystemMetrics::SetInferencePid(pid_t pid) {
  absl::MutexLock lock(&mu_);
  inference_pid_ = pid;
}

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

std::vector<std::string> StatFields(absl::string_view pid) {
  std::string path = absl::StrCat("/proc/", pid, "/stat");
  std::ifstream proc_stat(path);
  if (!proc_stat) {
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "failed to open path: " << path << strerror(errno);
    return {};
  }
  std::vector<std::string> stat_fields;
  for (std::string field; proc_stat >> field; stat_fields.push_back(field)) {
  }
  return stat_fields;
}
}  // namespace

namespace internal {

Utilization ReadCpuTime(
    const std::vector<size_t>& cpu_times,
    const std::vector<std::string>& self_stat_fields,
    std::optional<std::vector<std::string>> process_stat_fields) {
  static size_t idle_time = 0;
  static size_t total_time = 0;
  static size_t self_time = 0;
  static size_t process_time = 0;
  Utilization ret{0, 0, std::nullopt};

  constexpr int kCpuIdle = 3;
  constexpr int kUtime = 13, kStime = 14;
  constexpr int kStatTotal = 52;

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

  if (self_stat_fields.size() < kStatTotal) {
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "unknown /proc/self/stat format "
        << absl::StrJoin(self_stat_fields, " ");
    return ret;
  }

  auto CalculateUtilization = [&](size_t& process_time_to_update,
                                  const std::vector<std::string>& stat_fields) {
    auto utime = std::stoull(stat_fields[kUtime]);
    auto stime = std::stoull(stat_fields[kStime]);
    size_t process_time_new = utime + stime;
    size_t process_time_diff = process_time_new - process_time_to_update;
    process_time_to_update = process_time_new;
    return total_time_diff == 0 ? 0 : 1.0 * process_time_diff / total_time_diff;
  };

  ret.self = CalculateUtilization(self_time, self_stat_fields);

  if (process_stat_fields && process_stat_fields->size() >= kStatTotal) {
    ret.process = CalculateUtilization(process_time, *process_stat_fields);
  }

  return ret;
}

}  // namespace internal

absl::flat_hash_map<std::string, double> SystemMetrics::GetCpu()
    ABSL_LOCKS_EXCLUDED(mu_) {
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

  absl::MutexLock lock(&mu_);
  internal::Utilization cpu_utilization = internal::ReadCpuTime(
      SystemCpuTime(), StatFields("self"),
      inference_pid_ == -1 ? std::optional<std::vector<std::string>>{}
                           : StatFields(std::to_string(inference_pid_)));

  if (cpu_utilization.process.has_value()) {
    ret["inference process utilization"] = *cpu_utilization.process;
  }
  ret["total utilization"] = cpu_utilization.total;
  ret["main process utilization"] = cpu_utilization.self;
  ret["total cpu cores"] = static_cast<double>(get_nprocs());

  return ret;
}

absl::flat_hash_map<std::string, double> SystemMetrics::GetThread()
    ABSL_LOCKS_EXCLUDED(mu_) {
  absl::flat_hash_map<std::string, double> ret;

  auto ReadThreadCount = [&ret](absl::string_view pid,
                                absl::string_view label) {
    std::string path = absl::StrCat("/proc/", pid, "/status");
    FILE* fd = fopen(path.c_str(), "r");
    if (fd == nullptr) {
      ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
          << "Failed to open " << path << "; " << strerror(errno);
      return;
    }

    char buff[256];

    while (fgets(buff, sizeof(buff), fd) != nullptr) {
      if (absl::StartsWith(buff, "Threads:")) {
        int thread_count;
        if (sscanf(buff, "Threads: %d", &thread_count) == 1) {
          ret[std::string(label)] = static_cast<double>(thread_count);
        } else {
          ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
              << "Failed to parse thread count from " << path;
        }
        break;
      }
    }
    fclose(fd);
  };

  // Read thread count for the main process
  ReadThreadCount("self", "main process thread count");
  if (ret.empty()) {
    ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
        << "Thread count information not found in /proc/self/status";
  }
  absl::MutexLock lock(&mu_);
  // Optionally read thread count for the inference process
  if (inference_pid_ != -1) {
    ReadThreadCount(std::to_string(inference_pid_),
                    "inference process thread count");
  }
  return ret;
}

absl::flat_hash_map<std::string, double> SystemMetrics::GetMemory()
    ABSL_LOCKS_EXCLUDED(mu_) {
  absl::flat_hash_map<std::string, double> ret;
  char buff[256];
  char name[64];
  int64_t value = 0;
  char unit[16];

  // Read general memory stats
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

  auto ReadProcessMemory = [&ret](absl::string_view pid,
                                  absl::string_view label) {
    char buff[256];
    char name[64];
    int64_t value = 0;
    char unit[16];
    std::string path = absl::StrCat("/proc/", pid, "/status");
    FILE* fd = fopen(path.c_str(), "r");
    if (fd == nullptr) {
      ABSL_LOG_EVERY_N_SEC(ERROR, kLogInterval)
          << "Failed to open " << path << "; " << strerror(errno);
      return;
    }

    while (fgets(buff, sizeof(buff), fd) != nullptr) {
      if (absl::StartsWith(buff, "VmRSS:")) {
        sscanf(buff, "%s %lu %s", name, &value, unit);
        ret[std::string(label)] = value;
        break;
      }
    }
    fclose(fd);
  };

  // Read memory stats for the main process
  ReadProcessMemory("self", "main process");
  absl::MutexLock lock(&mu_);
  // Optionally read memory stats for the inference process
  if (inference_pid_ != -1) {
    ReadProcessMemory(std::to_string(inference_pid_), "inference process");
  }
  return ret;
}

}  // namespace privacy_sandbox::server_common
