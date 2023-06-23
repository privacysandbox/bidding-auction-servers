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

#include <thread>

#include "absl/strings/string_view.h"
#include "absl/synchronization/blocking_counter.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/bidding_service/benchmarking/bidding_benchmarking_logger.h"
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/bidding_adtech_code_wrapper.h"
#include "services/bidding_service/bidding_service.h"
#include "services/common/clients/code_dispatcher/code_dispatch_client.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using ::google::protobuf::TextFormat;

// Must be ample time for generateBid() to complete, otherwise we risk
// flakiness. In production, generateBid() should run in no more than a few
// hundred milliseconds.
constexpr int kGenerateBidExecutionTimeSeconds = 2;

// While Roma demands JSON input and enforces it strictly, we follow the
// javascript style guide for returning objects here, so object keys are
// unquoted on output even though they MUST be quoted on input.
constexpr absl::string_view js_code = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid( interest_group,
                          auction_signals,
                          buyer_signals,
                          trusted_bidding_signals,
                          device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      //Reshaped into an AdWithBid.
      return {
        render: interest_group.ads[0].renderUrl,
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_requiring_user_bidding_signals = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid( interest_group,
                          auction_signals,
                          buyer_signals,
                          trusted_bidding_signals,
                          device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      // Test that user bidding signals are present
      let length = interest_group.userBiddingSignals.length;

      //Reshaped into an AdWithBid.
      return {
        render: interest_group.ads[0].renderUrl,
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_requiring_parsed_user_bidding_signals =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid( interest_group,
                          auction_signals,
                          buyer_signals,
                          trusted_bidding_signals,
                          device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      let ubs = interest_group.userBiddingSignals;
      if ((ubs.someId === 1789) && (ubs.name === "winston")
          && ((ubs.years[0] === 1776) && (ubs.years[1] === 1868))) {
        //Reshaped into an AdWithBid.
        return {
          render: interest_group.ads[0].renderUrl,
          ad: {"arbitraryMetadataField": 1},
          bid: bid,
          allowComponentAuction: false
        };
      }
    }
  )JS_CODE";

constexpr absl::string_view js_code_requiring_trusted_bidding_signals =
    R"JS_CODE(
    function generateBid( interest_group,
                          auction_signals,
                          buyer_signals,
                          trusted_bidding_signals,
                          device_signals) {
      const bid = Math.floor(Math.random() * 10 + 1);

      //Reshaped into an AdWithBid.
      return {
        render: interest_group.ads[0].renderUrl,
        ad: {"tbsLength": Object.keys(trusted_bidding_signals).length},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_requiring_trusted_bidding_signals_keys =
    R"JS_CODE(
    function generateBid( interest_group,
                          auction_signals,
                          buyer_signals,
                          trusted_bidding_signals,
                          device_signals) {
      const bid = Math.floor(Math.random() * 10 + 1);

      //Reshaped into an AdWithBid.
      return {
        render: interest_group.ads[0].renderUrl,
        ad: {"tbskLength": interest_group.trustedBiddingSignalsKeys.length},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_debug_urls = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid( interest_group,
                          auction_signals,
                          buyer_signals,
                          trusted_bidding_signals,
                          device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      forDebuggingOnly.reportAdAuctionLoss("https://example-dsp.com/debugLoss");
      forDebuggingOnly.reportAdAuctionWin("https://example-dsp.com/debugWin");

      //Reshaped into an AdWithBid.
      return {
        render: interest_group.ads[0].renderUrl,
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_throws_exception = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid( interest_group,
                          auction_signals,
                          buyer_signals,
                          trusted_bidding_signals,
                          device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));
      throw new Error('Exception message');
    }
  )JS_CODE";

constexpr absl::string_view js_code_throws_exception_with_debug_urls =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid( interest_group,
                          auction_signals,
                          buyer_signals,
                          trusted_bidding_signals,
                          device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      forDebuggingOnly.reportAdAuctionLoss("https://example-dsp.com/debugLoss");
      forDebuggingOnly.reportAdAuctionWin("https://example-dsp.com/debugWin");
      throw new Error('Exception message');
    }
  )JS_CODE";

void SetupV8Dispatcher(V8Dispatcher* dispatcher,
                       absl::string_view adtech_code_blob) {
  DispatchConfig config;
  ASSERT_TRUE(dispatcher->Init(config).ok());
  std::string wrapper_js_blob =
      GetWrappedAdtechCodeForBidding(adtech_code_blob);
  ASSERT_TRUE(dispatcher->LoadSync(1, wrapper_js_blob).ok());
}

void BuildGenerateBidsRequestFromBrowser(
    GenerateBidsRequest* request,
    absl::flat_hash_map<std::string, google::protobuf::ListValue>*
        interest_group_to_ad,
    int desired_bid_count = 5, bool set_enable_debug_reporting = false) {
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  raw_request.set_enable_debug_reporting(set_enable_debug_reporting);
  for (int i = 0; i < desired_bid_count; i++) {
    auto interest_group = MakeARandomInterestGroupForBiddingFromBrowser();
    *raw_request.mutable_interest_group_for_bidding()->Add() = *interest_group;
    interest_group_to_ad->try_emplace(interest_group->name(),
                                      interest_group->ads());
  }
  raw_request.set_bidding_signals(MakeRandomTrustedBiddingSignals(raw_request));
  *request->mutable_raw_request() = raw_request;
}

class GenerateBidsReactorIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    server_common::metric::ServerConfig config_proto;
    config_proto.set_mode(server_common::metric::ServerConfig::PROD);
    metric::BiddingContextMap(
        server_common::metric::BuildDependentConfig(config_proto));
  }
};

TEST_F(GenerateBidsReactorIntegrationTest, GeneratesBidsByInterestGroupCode) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, js_code);

  GenerateBidsRequest request;
  absl::flat_hash_map<std::string, google::protobuf::ListValue>
      interest_group_to_ad;
  BuildGenerateBidsRequestFromBrowser(&request, &interest_group_to_ad);
  GenerateBidsResponse response;

  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<BiddingBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<BiddingNoOpLogger>();
        }
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };

  BiddingService service(std::move(generate_bids_reactor_factory), nullptr,
                         nullptr, BiddingServiceRuntimeConfig());
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  EXPECT_GT(response.raw_response().bids_size(), 0);
  for (const auto& ad_with_bid : response.raw_response().bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    std::string expected_render_url =
        interest_group_to_ad.at(ad_with_bid.interest_group_name())
            .values(0)
            .struct_value()
            .fields()
            .at("renderUrl")
            .string_value();
    EXPECT_GT(ad_with_bid.render().length(), 0);
    EXPECT_EQ(ad_with_bid.render(), expected_render_url);
    // Expected false because it is expected to be present and was manually set
    // to false.
    EXPECT_FALSE(ad_with_bid.allow_component_auction());
    ASSERT_TRUE(ad_with_bid.ad().has_struct_value());
    EXPECT_EQ(ad_with_bid.ad().struct_value().fields_size(), 1);
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("arbitraryMetadataField")
                  .number_value(),
              1.0);
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsWithParsedUserBiddingSignals) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, js_code_requiring_parsed_user_bidding_signals);

  GenerateBidsRequest request;
  absl::flat_hash_map<std::string, google::protobuf::ListValue>
      interest_group_to_ad;
  BuildGenerateBidsRequestFromBrowser(&request, &interest_group_to_ad);
  GenerateBidsResponse response;
  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<BiddingBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<BiddingNoOpLogger>();
        }
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };

  BiddingService service(std::move(generate_bids_reactor_factory), nullptr,
                         nullptr, BiddingServiceRuntimeConfig());
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  EXPECT_GT(response.raw_response().bids_size(), 0);
  for (const auto& ad_with_bid : response.raw_response().bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    std::string expected_render_url =
        interest_group_to_ad.at(ad_with_bid.interest_group_name())
            .values(0)
            .struct_value()
            .fields()
            .at("renderUrl")
            .string_value();
    EXPECT_GT(ad_with_bid.render().length(), 0);
    EXPECT_EQ(ad_with_bid.render(), expected_render_url);
    // Expected false because it is expected to be present and was manually set
    // to false.
    EXPECT_FALSE(ad_with_bid.allow_component_auction());
    ASSERT_TRUE(ad_with_bid.ad().has_struct_value());
    EXPECT_EQ(ad_with_bid.ad().struct_value().fields_size(), 1);
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("arbitraryMetadataField")
                  .number_value(),
              1.0);
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(GenerateBidsReactorIntegrationTest, ReceivesTrustedBiddingSignals) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, js_code_requiring_trusted_bidding_signals);

  int desired_bid_count = 5;
  GenerateBidsRequest request;
  absl::flat_hash_map<std::string, google::protobuf::ListValue>
      interest_group_to_ad;
  BuildGenerateBidsRequestFromBrowser(&request, &interest_group_to_ad);
  ASSERT_GT(request.raw_request().bidding_signals().length(), 0);

  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger =
            std::make_unique<BiddingNoOpLogger>();
        return new GenerateBidsReactor(client, request, response,
                                       std::move(benchmarking_logger), nullptr,
                                       nullptr, runtime_config);
      };
  GenerateBidsResponse response;
  BiddingService service(std::move(generate_bids_reactor_factory), nullptr,
                         nullptr, BiddingServiceRuntimeConfig());
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  EXPECT_GT(response.raw_response().bids_size(), 0);
  for (const auto& ad_with_bid : response.raw_response().bids()) {
    ASSERT_TRUE(ad_with_bid.ad().struct_value().fields().find("tbsLength") !=
                ad_with_bid.ad().struct_value().fields().end());
    // One signal key per IG.
    EXPECT_EQ(
        ad_with_bid.ad().struct_value().fields().at("tbsLength").number_value(),
        1);
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(GenerateBidsReactorIntegrationTest, ReceivesTrustedBiddingSignalsKeys) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher,
                    js_code_requiring_trusted_bidding_signals_keys);

  GenerateBidsRequest request;
  absl::flat_hash_map<std::string, google::protobuf::ListValue>
      interest_group_to_ad;
  BuildGenerateBidsRequestFromBrowser(&request, &interest_group_to_ad);
  ASSERT_GT(request.raw_request()
                .interest_group_for_bidding(0)
                .trusted_bidding_signals_keys_size(),
            0);

  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger =
            std::make_unique<BiddingNoOpLogger>();
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };
  GenerateBidsResponse response;
  BiddingService service(std::move(generate_bids_reactor_factory), nullptr,
                         nullptr, BiddingServiceRuntimeConfig());
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  EXPECT_GT(response.raw_response().bids_size(), 0);
  for (const auto& ad_with_bid : response.raw_response().bids()) {
    ASSERT_TRUE(ad_with_bid.ad().struct_value().fields().find("tbskLength") !=
                ad_with_bid.ad().struct_value().fields().end());
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("tbskLength")
                  .number_value(),
              request.raw_request()
                  .interest_group_for_bidding(0)
                  .trusted_bidding_signals_keys_size());
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

/*
 * This test exists to demonstrate that if an AdTech's script expects a
 * property to be present in the interest group, but that property is set to a
 * value which the protobuf serializer serializes to an empty string, then that
 * property WILL BE OMITTED from the serialized interest_group passed to the
 * generateBid() script, and the script will CRASH.
 * It so happens that the SideLoad Data provider provided such a value for
 * interestGroup.userBiddingSignals when userBiddingSignals are not present,
 * and the generateBid() script with which we test requires the
 * .userBiddingSignals property to be present and crashes when it is absent.
 */
TEST_F(GenerateBidsReactorIntegrationTest,
       FailsToGenerateBidsWhenMissingUserBiddingSignals) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, js_code_requiring_user_bidding_signals);
  int desired_bid_count = 5;
  GenerateBidsRequest request;
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  absl::flat_hash_map<std::string, google::protobuf::ListValue>
      interest_group_to_ad;
  for (int i = 0; i < desired_bid_count; i++) {
    auto interest_group = MakeARandomInterestGroupForBidding(false, true);
    *raw_request.mutable_interest_group_for_bidding()->Add() = *interest_group;
    interest_group_to_ad.try_emplace(interest_group->name(),
                                     interest_group->ads());

    ASSERT_TRUE(interest_group->user_bidding_signals().empty());
  }
  GenerateBidsResponse response;
  *request.mutable_raw_request() = raw_request;
  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<BiddingBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<BiddingNoOpLogger>();
        }
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };
  BiddingService service(std::move(generate_bids_reactor_factory), nullptr,
                         nullptr, BiddingServiceRuntimeConfig());
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  ASSERT_TRUE(response.IsInitialized());
  ASSERT_TRUE(response.raw_response().IsInitialized());

  // All instances of the script should have crashed; no bids should have been
  // generated.
  ASSERT_EQ(response.raw_response().bids_size(), 0);
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(GenerateBidsReactorIntegrationTest, GeneratesBidsFromDevice) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, js_code);
  int desired_bid_count = 1;
  GenerateBidsRequest request;
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  absl::flat_hash_map<std::string, google::protobuf::ListValue>
      interest_group_to_ad;
  for (int i = 0; i < desired_bid_count; i++) {
    auto interest_group = MakeAnInterestGroupForBiddingSentFromDevice();
    ASSERT_EQ(interest_group->ads().values_size(), 2);
    *raw_request.mutable_interest_group_for_bidding()->Add() = *interest_group;
    interest_group_to_ad.try_emplace(interest_group->name(),
                                     interest_group->ads());
    // This fails in production, the user Bidding Signals are not being set.
    // use logging to figire out why.
  }
  raw_request.set_bidding_signals(MakeRandomTrustedBiddingSignals(raw_request));
  GenerateBidsResponse response;
  *request.mutable_raw_request() = raw_request;
  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<BiddingBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<BiddingNoOpLogger>();
        }
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };
  BiddingService service(std::move(generate_bids_reactor_factory), nullptr,
                         nullptr, BiddingServiceRuntimeConfig());
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  EXPECT_EQ(response.raw_response().bids_size(), 1);
  for (const auto& ad_with_bid : response.raw_response().bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    std::string expected_render_url =
        interest_group_to_ad.at(ad_with_bid.interest_group_name())
            .values(0)
            .struct_value()
            .fields()
            .at("renderUrl")
            .string_value();
    EXPECT_GT(ad_with_bid.render().length(), 0);
    EXPECT_EQ(ad_with_bid.render(), expected_render_url);
    // Expected false because it is expected to be present and was manually set
    // to false.
    EXPECT_FALSE(ad_with_bid.allow_component_auction());
    ASSERT_TRUE(ad_with_bid.ad().has_struct_value());
    EXPECT_EQ(ad_with_bid.ad().struct_value().fields_size(), 1);
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("arbitraryMetadataField")
                  .number_value(),
              1.0);
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

void GenerateBidDebugReportingTestHelper(
    GenerateBidsResponse* response, const absl::string_view js_blob,
    bool enable_debug_reporting, bool enable_buyer_debug_url_generation) {
  int desired_bid_count = 5;
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, js_blob);
  GenerateBidsRequest request;
  absl::flat_hash_map<std::string, google::protobuf::ListValue>
      interest_group_to_ad;
  BuildGenerateBidsRequestFromBrowser(&request, &interest_group_to_ad,
                                      desired_bid_count,
                                      enable_debug_reporting);

  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger;
        benchmarking_logger = std::make_unique<BiddingBenchmarkingLogger>(
            FormatTime(absl::Now()));
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };

  const BiddingServiceRuntimeConfig& runtime_config = {
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation};
  BiddingService service(std::move(generate_bids_reactor_factory), nullptr,
                         nullptr, std::move(runtime_config));
  service.GenerateBids(&context, &request, response);
  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(GenerateBidsReactorIntegrationTest, BuyerDebugUrlGenerationDisabled) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = false;
  GenerateBidDebugReportingTestHelper(&response, js_code_with_debug_urls,
                                      enable_debug_reporting,
                                      enable_buyer_debug_url_generation);

  EXPECT_GT(response.raw_response().bids_size(), 0);
  for (const auto& adWithBid : response.raw_response().bids()) {
    EXPECT_GT(adWithBid.bid(), 0);
    EXPECT_FALSE(adWithBid.has_debug_report_urls());
  }
}

TEST_F(GenerateBidsReactorIntegrationTest, EventLevelDebugReportingDisabled) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = false;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidDebugReportingTestHelper(&response, js_code_with_debug_urls,
                                      enable_debug_reporting,
                                      enable_buyer_debug_url_generation);
  EXPECT_GT(response.raw_response().bids_size(), 0);
  for (const auto& adWithBid : response.raw_response().bids()) {
    EXPECT_GT(adWithBid.bid(), 0);
    EXPECT_FALSE(adWithBid.has_debug_report_urls());
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsReturnDebugReportingUrls) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidDebugReportingTestHelper(&response, js_code_with_debug_urls,
                                      enable_debug_reporting,
                                      enable_buyer_debug_url_generation);
  EXPECT_GT(response.raw_response().bids_size(), 0);
  for (const auto& adWithBid : response.raw_response().bids()) {
    EXPECT_GT(adWithBid.bid(), 0);
    EXPECT_EQ(adWithBid.debug_report_urls().auction_debug_win_url(),
              "https://example-dsp.com/debugWin");
    EXPECT_EQ(adWithBid.debug_report_urls().auction_debug_loss_url(),
              "https://example-dsp.com/debugLoss");
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsReturnDebugReportingUrlsWhenScriptCrashes) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidDebugReportingTestHelper(
      &response, js_code_throws_exception_with_debug_urls,
      enable_debug_reporting, enable_buyer_debug_url_generation);
  EXPECT_GT(response.raw_response().bids_size(), 0);
  for (const auto& adWithBid : response.raw_response().bids()) {
    EXPECT_EQ(adWithBid.bid(), 0);
    EXPECT_EQ(adWithBid.debug_report_urls().auction_debug_win_url(),
              "https://example-dsp.com/debugWin");
    EXPECT_EQ(adWithBid.debug_report_urls().auction_debug_loss_url(),
              "https://example-dsp.com/debugLoss");
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       NoGenerateBidsResponseIfNoDebugUrlsAndScriptCrashes) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidDebugReportingTestHelper(&response, js_code_throws_exception,
                                      enable_debug_reporting,
                                      enable_buyer_debug_url_generation);
  EXPECT_EQ(response.raw_response().bids_size(), 0);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
