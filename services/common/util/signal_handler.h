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
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "src/util/status_macro/examine_stack.h"

namespace privacy_sandbox::bidding_auction_servers {

inline void SignalHandler(int sig) {
  fprintf(stderr, "Error: signal %d:\n", sig);
  fprintf(stderr, " %s\n\n",
          privacy_sandbox::server_common::CurrentStackTrace().c_str());
  // more details but it's not async signal safe (it uses malloc() internally)
  fprintf(stderr, "stack trace with more details : \n");
  constexpr size_t kMaxStack = 128;
  void* callstack[kMaxStack];
  int nFrames = backtrace(callstack, kMaxStack);
  char** symbols = backtrace_symbols(callstack, nFrames);
  for (int i = 0; i < nFrames; i++) {
    Dl_info info;
    if (dladdr(callstack[i], &info)) {
      char* demangled = NULL;
      int status;
      demangled = abi::__cxa_demangle(info.dli_sname, NULL, 0, &status);
      fprintf(stderr, "%-3d %0*p %s + %zd\n", i, 2 + sizeof(void*) * 2,
              callstack[i], status == 0 ? demangled : info.dli_sname,
              (char*)callstack[i] - (char*)info.dli_saddr);
      free(demangled);
    } else {
      fprintf(stderr, "%-3d %0*p\n", i, 2 + sizeof(void*) * 2, callstack[i]);
    }
  }
  free(symbols);
  if (nFrames == kMaxStack) fprintf(stderr, "[truncated]\n");
  exit(1);
}

}  // namespace privacy_sandbox::bidding_auction_servers
