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

#include "services/common/metric/context_test.h"

namespace privacy_sandbox::server_common::metric {

class ExperimentTest : public ContextTest {
 protected:
  void SetUp() override {
    TelemetryConfig config_proto;
    config_proto.set_mode(TelemetryConfig::EXPERIMENT);
    static BuildDependentConfig metric_config(config_proto);
    context_ = Context<metric_list_span, MockMetricRouter>::GetContext(
        &mock_metric_router_, metric_config);
  }

  void ExpectCallLogSafe() {
    EXPECT_CALL(
        mock_metric_router_,
        LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntApproximateCounter)),
                Eq(1), _, ElementsAre(Pair("Debugging", "Yes"))))
        .WillOnce(Return(absl::OkStatus()));
    EXPECT_CALL(
        mock_metric_router_,
        LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntApproximateCounter2)),
                Eq(2), _, ElementsAre(Pair("Debugging", "Yes"))))
        .WillOnce(Return(absl::OkStatus()));
  }
};

TEST_F(ExperimentTest, LogAfterDecrypt) {
  context_->SetDecrypted();
  ErrorLogSafeAfterDecrypt();
  ExpectCallLogSafe();
  CHECK_OK(context_->LogMetric<kIntApproximateCounter>(1));
  CHECK_OK(context_->LogMetricDeferred<kIntApproximateCounter2>(
      []() mutable { return 2; }));
}

class CompareTest : public ExperimentTest {
 protected:
  void SetUp() override {
    TelemetryConfig config_proto;
    config_proto.set_mode(TelemetryConfig::COMPARE);
    static BuildDependentConfig metric_config(config_proto);
    context_ = Context<metric_list_span, MockMetricRouter>::GetContext(
        &mock_metric_router_, metric_config);
  }
};

TEST_F(CompareTest, LogAfterDecrypt) {
  context_->SetDecrypted();
  ErrorLogSafeAfterDecrypt();
  ExpectCallLogSafe();
  LogUnSafeForApproximate();
}

}  // namespace privacy_sandbox::server_common::metric
