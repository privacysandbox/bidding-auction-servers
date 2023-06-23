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

#include "services/common/util/examine_stack.h"

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
constexpr int kDefaultStackTraceDepth = 32;
// The %p field width for printf() functions is two characters per byte,
// and two extra for the leading "0x".
constexpr int kPrintfPointerFieldWidth = 2 + 2 * sizeof(void*);
constexpr char kStackTracePrefix[] = "    ";
void DebugWriteToString(const char* data, void* str) {
  reinterpret_cast<std::string*>(str)->append(data);
}
}  // namespace
// Print a program counter and its symbol name.
static void DumpPCAndSymbol(DebugWriter* writerfn, void* arg, void* pc,
                            const char* const prefix, bool short_format) {
  char tmp[1024];
  const char* symbol = "(unknown)";
  // Symbolizes the previous address of pc because pc may be in the
  // next function.  The overrun happens when the function ends with
  // a call to a function annotated noreturn (e.g. CHECK).
  // If symbolization of pc-1 fails, also try pc on the off-chance
  // that we crashed on the first instruction of a function (that
  // actually happens very often for e.g. __restore_rt).
  const uintptr_t prev_pc = reinterpret_cast<uintptr_t>(pc) - 1;
  if (absl::Symbolize(reinterpret_cast<char*>(prev_pc), tmp, sizeof(tmp)) ||
      absl::Symbolize(pc, tmp, sizeof(tmp))) {
    symbol = tmp;
  }
  char buf[1024];
  if (short_format) {
    snprintf(buf, sizeof(buf), " %s;", symbol);
  } else {
    snprintf(buf, sizeof(buf), "%s@ %*p  %s\n", prefix,
             kPrintfPointerFieldWidth, pc, symbol);
  }
  writerfn(buf, arg);
}
// Dump current stack trace as directed by writerfn.
// Make sure this function is not inlined to avoid skipping too many top frames.
ABSL_ATTRIBUTE_NOINLINE
void DumpStackTrace(int skip_count, DebugWriter* writerfn, void* arg,
                    bool short_format) {
  // Print stack trace
  void* stack[kDefaultStackTraceDepth];
  int depth = absl::GetStackTrace(stack, ABSL_ARRAYSIZE(stack), skip_count + 1);
  for (int i = 0; i < depth; i++) {
    DumpPCAndSymbol(writerfn, arg, stack[i],
                    short_format ? "" : kStackTracePrefix, short_format);
  }
}
std::string CurrentStackTrace(bool short_format) {
  std::string result = "Stacktrace:";
  if (!short_format) {
    result += "\n";
  }
  DumpStackTrace(1, DebugWriteToString, &result, short_format);
  return result;
}
}  // namespace privacy_sandbox::bidding_auction_servers
