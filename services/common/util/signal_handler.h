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

#include <cxxabi.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <array>
#include <string>
#include <utility>

#include "include/libunwind.h"

namespace privacy_sandbox::bidding_auction_servers {

inline void SignalHandler(int sig) {
  fprintf(stderr, "Recived signal: %d:\n", sig);
  unw_cursor_t cursor;
  unw_context_t context;
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);
  constexpr int kMaxFrames = 128;
  constexpr int kMaxSymbolSize = 256;
  int frame_count = 0;
  while (unw_step(&cursor) && ++frame_count <= kMaxFrames) {
    unw_word_t ip, sp, off;

    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    unw_get_reg(&cursor, UNW_REG_SP, &sp);

    std::string symbol(kMaxSymbolSize + 1, '\0');
    unw_get_proc_name(&cursor, symbol.data(), kMaxSymbolSize, &off);

    int status;
    char* demangled_name =
        abi::__cxa_demangle(symbol.c_str(), nullptr, nullptr, &status);
    if (status == 0) {
      fprintf(stderr,
              "#%-2d 0x%016" PRIxPTR " sp=0x%016" PRIxPTR " %s + 0x%" PRIxPTR
              "\n",
              frame_count, ip, sp, demangled_name, off);
      free(demangled_name);
    } else {
      fprintf(stderr,
              "#%-2d 0x%016" PRIxPTR " sp=0x%016" PRIxPTR " %s + 0x%" PRIxPTR
              "\n",
              frame_count, ip, sp, symbol.c_str(), off);
    }
  }

  if (frame_count > kMaxFrames) {
    fprintf(stderr, "[truncated]\n");
  }
  exit(1);
}

inline void RegisterCommonSignalHandlersHelper() {
  constexpr std::array<int, 7> kHandledSignals = {
      SIGABRT, SIGSEGV, SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGTRAP};
  for (auto sig : kHandledSignals) {
    signal(sig, SignalHandler);
  }
}

// Registers common signal handlers helpful for the
// service usability.
void RegisterCommonSignalHandlers();

}  // namespace privacy_sandbox::bidding_auction_servers
