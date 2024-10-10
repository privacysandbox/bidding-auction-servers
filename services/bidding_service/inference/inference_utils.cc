//  Copyright 2023 Google LLC
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

#include "services/bidding_service/inference/inference_utils.h"

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>

#include "absl/base/const_init.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/pointer.h"
#include "rapidjson/writer.h"
#include "services/bidding_service/inference/inference_flags.h"
#include "services/common/clients/code_dispatcher/request_context.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/metric/server_definition.h"
#include "services/common/util/request_response_constants.h"
#include "src/roma/interface/roma.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"
#include "utils/error.h"
#include "utils/file_util.h"
#include "utils/inference_error_code.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

void LogMetrics(
    const google::protobuf::Map<std::string, MetricValueList>& metrics_map,
    metric::BiddingContext* metric_context, RequestLogContext& log_context) {
  for (const auto& [key, metric_value_list] : metrics_map) {
    absl::Status log_status;
    if (key == "kInferenceRequestCount") {
      for (const auto& metric_value : metric_value_list.metrics()) {
        log_status =
            metric_context->AccumulateMetric<metric::kInferenceRequestCount>(
                metric_value.value());
      }
    } else if (key == "kInferenceRequestDuration") {
      for (const auto& metric_value : metric_value_list.metrics()) {
        log_status =
            metric_context->AccumulateMetric<metric::kInferenceRequestDuration>(
                metric_value.value());
      }
    } else if (key == "kInferenceRequestSize") {
      for (const auto& metric_value : metric_value_list.metrics()) {
        log_status =
            metric_context->AccumulateMetric<metric::kInferenceRequestSize>(
                metric_value.value());
      }
    } else if (key == "kInferenceResponseSize") {
      for (const auto& metric_value : metric_value_list.metrics()) {
        log_status =
            metric_context->AccumulateMetric<metric::kInferenceResponseSize>(
                metric_value.value());
      }
    } else if (key == "kInferenceErrorCountByErrorCode") {
      for (const auto& metric_value : metric_value_list.metrics()) {
        log_status =
            metric_context
                ->AccumulateMetric<metric::kInferenceErrorCountByErrorCode>(
                    metric_value.value(), metric_value.partition());
      }
    } else if (key == "kInferenceRequestCountByModel") {
      for (const auto& metric_value : metric_value_list.metrics()) {
        log_status =
            metric_context
                ->AccumulateMetric<metric::kInferenceRequestCountByModel>(
                    metric_value.value(), metric_value.partition());
      }
    } else if (key == "kInferenceRequestDurationByModel") {
      for (const auto& metric_value : metric_value_list.metrics()) {
        log_status =
            metric_context
                ->AccumulateMetric<metric::kInferenceRequestDurationByModel>(
                    metric_value.value(), metric_value.partition());
      }
    } else if (key == "kInferenceRequestFailedCountByModel") {
      for (const auto& metric_value : metric_value_list.metrics()) {
        log_status =
            metric_context
                ->AccumulateMetric<metric::kInferenceRequestFailedCountByModel>(
                    metric_value.value(), metric_value.partition());
      }
    } else if (key == "kInferenceRequestBatchCountByModel") {
      for (const auto& metric_value : metric_value_list.metrics()) {
        log_status =
            metric_context
                ->AccumulateMetric<metric::kInferenceRequestBatchCountByModel>(
                    metric_value.value(), metric_value.partition());
      }
    } else {
      log_status = absl::NotFoundError("Unrecognized metric key: " + key);
    }
    if (!log_status.ok()) {
      PS_LOG(ERROR, log_context)
          << "Failed to log metric for " << key << ": " << log_status.message();
    }
  }
}

SandboxExecutor& Executor() {
  // TODO(b/314976301): Use absl::NoDestructor<T> when it becomes available.
  // Static object will be lazily initiated within static storage.

  // TODO(b/317124648): Pass a SandboxExecutor object via Roma's `TMetadata`.
  static SandboxExecutor* executor = new SandboxExecutor(
      *absl::GetFlag(FLAGS_inference_sidecar_binary_path),
      {*absl::GetFlag(FLAGS_inference_sidecar_runtime_config)});
  return *executor;
}

std::shared_ptr<grpc::Channel> InferenceChannel(
    const SandboxExecutor& executor) {
  // TODO(b/314976301): Use absl::NoDestructor<T> when it becomes available.
  // Static object will be lazily initiated within static storage.

  // TODO(b/317124648): Pass a gRPC channel object via Roma's `TMetadata`.
  static std::shared_ptr<grpc::Channel> client_channel =
      grpc::CreateInsecureChannelFromFd("GrpcChannel",
                                        executor.FileDescriptor());
  return client_channel;
}

std::unique_ptr<InferenceService::StubInterface> CreateInferenceStub() {
  return InferenceService::NewStub(InferenceChannel(Executor()));
}

absl::Status RegisterModelsFromLocal(const std::vector<std::string>& paths) {
  if (paths.size() == 0 || (paths.size() == 1 && paths[0].empty())) {
    return absl::NotFoundError("No model to register in local disk");
  }

  SandboxExecutor& executor = Executor();
  std::unique_ptr<InferenceService::StubInterface> stub =
      InferenceService::NewStub(InferenceChannel(executor));

  for (const auto& path : paths) {
    RegisterModelRequest register_request;
    PS_RETURN_IF_ERROR(PopulateRegisterModelRequest(path, register_request));
    grpc::ClientContext context;
    RegisterModelResponse register_response;
    grpc::Status status =
        stub->RegisterModel(&context, register_request, &register_response);

    if (!status.ok()) {
      return server_common::ToAbslStatus(status);
    }
  }
  return absl::OkStatus();
}

absl::Status RegisterModelsFromBucket(
    absl::string_view bucket_name, const std::vector<std::string>& paths,
    const std::vector<BlobFetcher::Blob>& blobs) {
  if (bucket_name.empty()) {
    return absl::InvalidArgumentError("Cloud bucket name is not set");
  }
  if (paths.size() == 0 || blobs.size() == 0 ||
      (paths.size() == 1 && paths[0].empty())) {
    return absl::NotFoundError("No model to register in the cloud bucket");
  }

  SandboxExecutor& executor = Executor();
  std::unique_ptr<InferenceService::StubInterface> stub =
      InferenceService::NewStub(InferenceChannel(executor));

  for (const auto& model_path : paths) {
    RegisterModelRequest request;
    request.mutable_model_spec()->set_model_path(model_path);
    PS_VLOG(10) << "model_path: " << model_path;

    for (const BlobFetcher::Blob& blob : blobs) {
      if (absl::StartsWith(blob.path, model_path)) {
        (*request.mutable_model_files())[blob.path] = blob.bytes;
        PS_VLOG(10) << "model_files: " << blob.path;
      }
    }

    grpc::ClientContext context;
    RegisterModelResponse response;
    grpc::Status status = stub->RegisterModel(&context, request, &response);

    if (!status.ok()) {
      return server_common::ToAbslStatus(status);
    }
  }
  // TODO(b/316960066): Handles register models response once the proto has been
  // fleshed out.
  return absl::OkStatus();
}

void RunInference(
    google::scp::roma::FunctionBindingPayload<RomaRequestSharedContextBidding>&
        wrapper) {
  absl::Time start_inference_execution_time = absl::Now();
  const std::string& payload = wrapper.io_proto.input_string();

  SandboxExecutor& executor = Executor();
  std::unique_ptr<InferenceService::StubInterface> stub =
      InferenceService::NewStub(InferenceChannel(executor));

  PS_VLOG(kNoisyInfo) << "RunInference input: " << payload;
  PredictRequest predict_request;
  predict_request.set_input(payload);

  absl::StatusOr<std::shared_ptr<RomaRequestContextBidding>>
      roma_request_context = wrapper.metadata.GetRomaRequestContext();

  if (roma_request_context.ok()) {
    // Check if it is a Protected Audience request and the build flavor is prod
    if ((*roma_request_context)->IsProtectedAudienceRequest() &&
        PS_IS_PROD_BUILD) {
      PS_LOG(ERROR, (*roma_request_context)->GetLogContext())
          << "Inference is not supported for Protected Audience requests in "
             "production build.";
      return;
    }
    predict_request.set_is_consented((*roma_request_context)->IsConsented());
  }

  grpc::ClientContext context;
  PredictResponse predict_response;
  grpc::Status rpc_status =
      stub->Predict(&context, predict_request, &predict_response);
  if (rpc_status.ok()) {
    wrapper.io_proto.set_output_string(predict_response.output());
    PS_VLOG(10) << "Inference response received: "
                << predict_response.DebugString();
    if (roma_request_context.ok()) {
      RequestLogContext& log_context = (*roma_request_context)->GetLogContext();
      PS_VLOG(kNoisyInfo, log_context)
          << "Inference sidecar consented debugging log: "
          << predict_response.debug_info();

      if (auto inference_metric_context =
              (*roma_request_context)->GetMetricContext();
          inference_metric_context.ok()) {
        auto metric_context = inference_metric_context.value();
        LogMetrics(predict_response.metrics_list(), metric_context,
                   log_context);
        int inference_execution_time_ms =
            (absl::Now() - start_inference_execution_time) /
            absl::Milliseconds(1);
        LogIfError(
            metric_context
                ->AccumulateMetric<metric::kBiddingInferenceRequestDuration>(
                    inference_execution_time_ms));
      }
    }
    return;
  }
  absl::Status status = server_common::ToAbslStatus(rpc_status);
  wrapper.io_proto.set_output_string(CreateBatchErrorString(
      {.error_type = Error::GRPC,
       .description = absl::StrCat("Code: ", status.code(),
                                   ", Message: ", status.message())}));
  if (roma_request_context.ok()) {
    PS_LOG(ERROR, (*roma_request_context)->GetLogContext())
        << "Response error: " << status.message();

    if (auto inference_metric_context =
            (*roma_request_context)->GetMetricContext();
        inference_metric_context.ok()) {
      auto metric_context = inference_metric_context.value();
      LogIfError(
          metric_context
              ->AccumulateMetric<metric::kInferenceRequestFailedCountByStatus>(
                  1, StatusCodeToString(status.code())));
    }
  }
}

std::string GetModelResponseToJson(const GetModelPathsResponse& response) {
  rapidjson::Document document;
  document.SetArray();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

  for (const ModelSpec& spec : response.model_specs()) {
    rapidjson::Value value;
    value.SetString(spec.model_path().c_str(), spec.model_path().length(),
                    allocator);
    document.PushBack(value, allocator);
  }

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  document.Accept(writer);

  return strbuf.GetString();
}

void GetModelPaths(
    google::scp::roma::FunctionBindingPayload<RomaRequestSharedContextBidding>&
        wrapper) {
  SandboxExecutor& executor = Executor();
  std::unique_ptr<InferenceService::StubInterface> stub =
      InferenceService::NewStub(InferenceChannel(executor));

  PS_VLOG(kNoisyInfo) << "GetModelPaths called";
  GetModelPathsRequest get_model_paths_request;

  grpc::ClientContext context;
  GetModelPathsResponse get_model_paths_response;
  grpc::Status rpc_status = stub->GetModelPaths(
      &context, get_model_paths_request, &get_model_paths_response);
  if (rpc_status.ok()) {
    wrapper.io_proto.set_output_string(
        GetModelResponseToJson(get_model_paths_response));
    PS_VLOG(10) << "GetModelPaths response received: "
                << get_model_paths_response.DebugString();
    return;
  }

  absl::Status status = server_common::ToAbslStatus(rpc_status);
  PS_LOG(ERROR, SystemLogContext())
      << "GetModelPaths response error: " << status.message();
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
