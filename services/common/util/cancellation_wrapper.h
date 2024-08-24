/*
 * Copyright 2024 Google LLC
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

#ifndef SERVICES_COMMON_UTIL_CANCELLATION_WRAPPER_H_
#define SERVICES_COMMON_UTIL_CANCELLATION_WRAPPER_H_

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/status/status.h"
#include "services/common/util/error_categories.h"

template <typename T>
struct is_status : std::false_type {};

template <>
struct is_status<absl::Status> : std::true_type {};

template <typename T>
struct is_status<absl::StatusOr<T>> : std::true_type {};

template <typename T>
constexpr bool is_status_v = is_status<T>::value;

// Macro to wrap class functions with cancellation logic.
// The wrapped function will check if cancellations are enabled and if the
// current request is cancelled. If both are true then it will finish the
// reactor and return a cancellation error status. However, if the wrapped
// function is void then it will simply return. If the wrapped function returns
// a Status or a StatusOr then it will return a Status or a StatusOr
// accordingly. Otherwise it will return the wrapped function within a StatusOr.
// First argument is the name of the resulting wrapped function. This needs to
// correspond to a class function with the same name with "Cancellable" in front
// of the name.
// Second argument is the boolean flag to enable cancellations.
// Third argument is a pointer to the grpc client context.
// Fourth argument is a function that finishes the reactor and takes in a grpc
// status as a parameter.
// Returns StatusOr of the return type of the function being wrapped, but if the
// return type is void then the resulting wrapped function is also void. And if
// the return type of the function being wrapped is a Status or a StatusOr
// already then it will return either a Status or a StatusOr respectively.
#define CLASS_CANCELLATION_WRAPPER(function, flag, context, finish)         \
  template <typename... Args>                                               \
  auto function(Args&&... args)                                             \
      -> std::conditional_t<                                                \
          std::is_void_v<decltype(this->Cancellable##function(              \
              std::forward<Args>(args)...))>,                               \
          void,                                                             \
          std::conditional_t<                                               \
              is_status_v<decltype(this->Cancellable##function(             \
                  std::forward<Args>(args)...))>,                           \
              decltype(this->Cancellable##function(                         \
                  std::forward<Args>(args)...)),                            \
              absl::StatusOr<decltype(this->Cancellable##function(          \
                  std::forward<Args>(args)...))>>> {                        \
    if ((flag) && (context)->IsCancelled()) {                               \
      finish(grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled)); \
      if constexpr (std::is_void_v<decltype(this->Cancellable##function(    \
                        std::forward<Args>(args)...))>) {                   \
        return;                                                             \
      } else {                                                              \
        return absl::CancelledError();                                      \
      }                                                                     \
    } else {                                                                \
      return this->Cancellable##function(std::forward<Args>(args)...);      \
    }                                                                       \
  }

// Function to wrap other functions, like lambdas with cancellation logic.
template <typename BaseFunctionT, typename OnCancelFunctionT>
auto CancellationWrapper(grpc::CallbackServerContext* context,
                         bool enable_cancellation, BaseFunctionT function,
                         OnCancelFunctionT on_cancel) {
  return [context, enable_cancellation,
          function = std::forward<BaseFunctionT>(function),
          on_cancel = std::forward<OnCancelFunctionT>(on_cancel)](
             auto&&... args) mutable {
    if (enable_cancellation && context->IsCancelled()) {
      on_cancel();
    } else {
      function(std::forward<decltype(args)>(args)...);
    }
  };
}

#endif  // SERVICES_COMMON_UTIL_CANCELLATION_WRAPPER_H_
