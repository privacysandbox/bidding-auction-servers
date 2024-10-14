# Changelog

All notable changes to this project will be documented in this file. See [commit-and-tag-version](https://github.com/absolute-version/commit-and-tag-version) for commit guidelines.

## 4.1.0 (2024-10-10)


### ⚠ BREAKING CHANGES

* All sellers and buyers must update the js_num_workers parameter to udf_num_workers in terraform
* Sellers must wait for all the integrated buyers to update to 4.1.0 before updating to accomodate serialization changes in the GetBidsRequest.

### Features

* [BYOB] Add buyer code fetcher for executables
* [BYOB] Add dispatch client for generateBid
* [BYOB] Add GCP build setup for Bidding service
* [BYOB] Add gRPC reactor for generateBid binary
* [BYOB] Add info about runtime flags to demo/README.md
* [BYOB] Add interface for clients that load code
* [BYOB] Add to Bidding service critical path
* Add a generic model validator interface for Tensorflow and PyTorch
* Add PrivateAggregation protos to generateBid spec
* add project setup scripts
* add proto support for updateIfOlderThanMs
* BFE parses perInterestGroupData, returns updateIfOlderThanMs data to SFE
* Compress SFE <> BFE comms
* Create new inference dashboard for GCP
* Enable chaffing on SFE prod mode builds
* Enable connecting to TKV via an External LB
* Enable dynamic partition for inference metrics
* Enable model validation for PyTorch
* Enable TCMalloc for B&A inference
* Export all available models as metric
* Freeze a TensorFlow model ([bcc798c]( )), closes [#7]( )
* Implement a PyTorch model validator
* Implement a TensorFlow model validator
* Rename JS_NUM_WORKERS to UDF_NUM_WORKERS
* SFE sends updateIfOlderThanMs data to client
* update start_bidding with minimal defaults and local file flag

### Bug Fixes

* BYOB generateBid should only parse first bid from binary response
* Ensure gRPC calls don't hang on error
* Fix js_execution.error.count metric
* Fix memory leak in BFE client
* Fix readonly declarations in shell script
* Fix version bump in release script
* Load request files in the init function
* Reduce absl::GetFlag() call in GetBidsReactor
* Renames prev_wins_ms to prev_wins
* Set inference sidecar GRPC keepalive timeout
* update tls cert commands for GCP setup
* Use std::hash instead of absl::hash

## 4.0.0 (2024-09-09)


### ⚠ BREAKING CHANGES

* Move serialization of bidding signals per IG from Bidding to BFE

### Features

* [IsolateBuyerAndSellerCodeExecution] Add wrapper for buyer's reportWin() udf
* [PAS threat mitigation] Add support for egressVector and temporaryUnlimittedEgressVector by replacing the $extraArgs
* [Private Aggregation] Add all the contributions from Auction Service to AuctionResult
* [Private Aggregation] Add integration test to verify parsing of PAAPI event
* [Private Aggregation] Add PrivateAggregateReportingResponse to AuctionResult proto
* [Private Aggregation] Convert 128 bit bucket into array of 64 bit integer in wrapper
* [Private Aggregation] Convert bucket offset object to SignalBucket
* [Private Aggregation] Convert value object in contribution to SignalValue
* [PrivateAggregation] Add PrivateAggregateReportingResponse in Auction Service response
* [PrivateAggregation] Append private aggregation wrapper to existing seller's wrapper
* [reporting] Enable reportResult execution for topLevelSeller
* Add `common.privateAggregation.createContribution` function for creating a contribution object.
* Add `common.privateAggregation.ReservedEvent` object that holds reserved events string constants.
* Add a script to freeze a given TensorFlow model
* Add a script to list all TensorFlow ops
* Add a script to list all Torchscript ops
* Add AppendAdEventContributionsToPaggResponse with SignalValue only
* Add auction_service.privateAggregation.contributeToHistogram/OnEvent
* Add bucket protos (Bucket128Bit, BucketOffset, SignalBucket, PrivateAggregationBucket) and add bucket and value fields to PrivateAggregateContribution
* Add chaffing feature flags in SFE/BFE.
* Add checksum capability to model loading
* Add common utils to create test private aggregation contribution and response
* Add config flag enable_private_aggregate_reporting to bidding service's runtime_config
* Add config flag enable_private_aggregation_generation to auction service's runtime_config
* Add ConvertSignalValueToInt for calculating final value from contribution's base value, scale, and offset.
* Add distribution of IGs related metrics
* Add enable_private_aggregate_reporting for GetSellerWrappedCode and unit tests that uses GetSellerWrappedCode
* Add event code and private aggregation objects for contributeToHistogramOnEvent, also added tests for event field
* add EventMessage to log context
* Add HandlePrivateAggregateReporting with support of SignalValue only
* Add HandlePrivateAggregationContributions, iterate over AdWithBids, filter contributions, post processing, test
* Add helper functions convertEventToInt and convertBaseValueToInt and tests for them for contributeToHistogramOnEvent for bidding service
* Add Inference Metrics like count, duration, error and size to TF & Pytorch sidecars.
* Add Inference Request count, duration and failure metric partitioned by model path for Tensorflow and Pytorch sidecars.
* Add metrics for inference model fetching and registration
* add option to switch DebugInfo in secure invoke encrypt
* Add parser function for Private Aggregation's Signal Bucket Object
* Add parser function for Private Aggregation's Signal Value Object
* Add PrivateAggregateContribution proto with event field and enum EventType to the .proto file
* Add proto messages for SignalValue, PrivateAggregationValue, and BaseValue enum.
* Add request creation timestamp to ProtectedAuctionInput
* Add request/response to ExecuteInternal on async clients
* Add support for collecting metrics from the Roma callbacks.
* Add tee-container-log-redirect option in terraform
* Add ToBaseValue Helper Function to convert corresponding base value strings to BaseValue Enum
* Add ToEventTypeString for converting EventType enum into its corresponding string in JSON object returned by ROMA
* Add utility function to calculate Signal Bucket's final value post auction ([3da9ab5]( )), closes [1#L192]( )
* Add utility function to parse and return BucketOffset from rapidjson document
* Add wrapper and test files with method headers for contributeToHistogramOnEvent for bidding service
* Added `isValidCustomEvent` validation function for common.privateAggregation
* Added isValidValue validation function for common.privateAggregation
* Added new files for JS private aggregation util for bidding service
* Bash script for ASG and Cloud Map Custom HealthChecks
* Bid Currency Support for Top-Level Seller in Server-Orchestrated Multi-Seller Auctions
* Change js helper convertEventToInt to mapEventToEnum to return string corresponding to enum instead of int
* configured public key urls are verified against an explicit allowlist
* Consented request replace enable_adtech_code_logging in Bidding Server
* contributeToHistogram with test for bidding service
* contributeToHistogramOnEvent with tests, using common private aggregation helpers
* convert all remaining docker images into OCI
* Convert Private Aggregation wrapper functions from .js to C++ string
* Create a periodic model fetcher library for inference
* Create declarative spec for generateBid
* Create inference model store for model management
* Create logging library for inference consented logs
* Debug Reporting for Bid Currency
* Declare inference JS error schema
* Declare schema for model fetching metadata file
* decrease aws/build_and_test duration by ~75%
* Enable blob fetcher to only fetch blobs with given prefixes
* Enable dynamic model loading for inference
* Enable Protected App Signals by Default
* Enable Service Mesh on AWS by Default
* export non-privacy log through otel with safe system context
* Feature Flag for TLS in Service Mesh
* Force chaffing to enabled for prod builds on BFE + update common repo dep
* Force the ML model reset with the probability of 0.1%
* Forward per request consented debugging config to roma callback
* Freeze TorchScript models before serving
* Handle chaff and new request format on BFE
* Implement aggregated Error Reporting for Tensorflow and Pytorch sidecars.
* Implement AWS Cloud Un-Map
* Implement new SFE <> BFE request format for chaffing
* Implement ParseAndProcessContribution with SignalValue only
* Implement the probabilistic model reset for PyTorch
* Implement the probabilistic model reset for TensorFlow.
* Instrument inference JS error for the PyTorch sidecar
* Integrate with Trusted KV Server in Mesh
* Load models using model metadata config for the periodic model fetcher
* Load Test Flag for AWS
* Log consented debugging information in inference sidecar
* log EventMessage in servers
* log udf log in EventMessage for non_prod debug_info
* Make chaffing flag configurable via Terraform
* move ig metric to bfe
* Move serialization of bidding signals per IG from Bidding to BFE
* Propagate inference error back to JS caller
* Remove enableAdtechCodeLogging flag value
* Remove Envoy Access Logging
* Route consented inference requests to a consented model store
* send chaff requests from SFE
* Service Mesh in AWS
* Support CPU isolation in the inference sidecar
* Unit test for loading parsing fake contributions in Roma for bidding service
* Unpad KV server responses
* update code/cloud build to use tags
* Update Demo Terraform Configs with values for integrating TEE KV Servers into Mesh
* Upgrade AWS Provider for Terraform from v3.xx to v4.xx
* Use gRPC for AWS Service Mesh Envoy HCs
* Write a fake generateBid() script which calls privateAggregation.contributeToHistogramOnEvent(event, <fake contribution>)
* write event message to gcs for consented request
* write event message to s3 in aws for consented request


### Bug Fixes

* [IsolateBuyerAndSellerCodeExecution] Add a new class for buyer's reporting code fetch and load
* [IsolateBuyerAndSellerCodeExecution] Add a new code wrapper with only scoreAd and reportResult
* [IsolateBuyerAndSellerCodeExecution] Add config flag to enable seller and buyer code isolation
* [IsolateBuyerAndSellerCodeExecution] Modify seller_udf_manager to fetch and load buyer udfs
* [IsolateBuyerAndSellerCodeExecution] Refactor the code fetch files.
* Add BidCurrency SUT to Pre-Submit
* add check back in default grpc client
* Add createContribution inside contributeToHistogram/OnEvent and fixed typo with createContribution's value conditional statement.
* Add DebugInfo pointer for debugging log into RomaRequestContext
* Add generation_id to chaff requests
* Add handling when rejection reason is specified as base value but not available and statuscode documentation for HandlePrivateAggregationValuePostAuction
* Adds domain equality validations to buyer reporting UDF URL
* Align plaintext buyer request for basic SUT with encrypted version
* Allow up to 15 buyers when chaffing enabled
* chaffing bugs ([027d5bb]( )), closes [/github.com/abseil/abseil-cpp/blob/master/absl/container/internal/raw_hash_set.h#L1572]( )
* Changed base value string to be converted in ToBaseValue function and test cases
* Changed raw string delimiter JSCODE to JS_CODE for consistency
* Changed the input format of HandlePrivateAggregationReporting, added support for parsing PAgg bucket,  and add the logic for getting required BaseValues (winning-bid, highest-scoring-other-bid, rejection-reason).
* clean up log verbosity 3
* Correct output_filter typo
* do not try to impersonate service accounts if TEST_MODE=true
* Don't set chaff size for non-chaff requests
* Eliminate Terraform Error Message about empty Authority Field
* Enable threat mitigation with seller and buyer code isolation
* Ensure instance id is set in logs on AWS when not using mesh.
* Execute Callback for empty HTTP request vector
* Fix bugs in reportWin URL validation in auction service
* Fix release notes by adding a dedicated tag on main to generate the changelog
* make num_chaff_requests not have a static lower bound
* Make terraform for aws delete cloud maps without error
* Populate temp rc file for inference sidecar
* Populate temp rc file for inference sidecar
* Populate temp rc file for inference sidecar
* Redirect misleading log to /dev/null
* Refactored conversion of private aggregation value post auction into returning absl::StatusOr<int> instead of implicitly editing PrivateAggregationValue that was passed in.
* Revert wrk2 test runner to an earlier version
* scorecard.yaml version updates
* shuffle request order (real and chaff) on SFE
* truncate curl failure info for fail count metric
* undo rules_oci migration to fix hash stability issues
* Update setup_2 and demo terraform configs to valid state
* Update SUTs to include and test Experiment Group ID
* Upgrades google terraform plugin to 5.31.0 to fix crash


### Documentation

* Add PAS input example to BFE
* Document how to deployment B&A without inference
* Update inference_sidecar README with model size limits
* Updated expected format for INFERENCE_MODEL_BUCKET_PATHS in README

## 3.11.0 (2024-08-19)


### Features

* [IsolateBuyerAndSellerCodeExecution] Add wrapper for buyer's reportWin() udf
* [PrivateAggregation] Add PrivateAggregateReportingResponse in Auction Service response
* [PrivateAggregation] Append private aggregation wrapper to existing seller's wrapper
* [reporting] Enable reportResult execution for topLevelSeller
* Add `common.privateAggregation.createContribution` function for creating a contribution object.
* Add `common.privateAggregation.ReservedEvent` object that holds reserved events string constants.
* Add auction_service.privateAggregation.contributeToHistogram/OnEvent
* Add bucket protos (Bucket128Bit, BucketOffset, SignalBucket, PrivateAggregationBucket) and add bucket and value fields to PrivateAggregateContribution
* Add chaffing feature flags in SFE/BFE.
* Add checksum capability to model loading
* Add config flag enable_private_aggregate_reporting to bidding service's runtime_config
* Add config flag enable_private_aggregation_generation to auction service's runtime_config
* Add ConvertSignalValueToInt for calculating final value from contribution's base value, scale, and offset.
* Add distribution of IGs related metrics
* Add enable_private_aggregate_reporting for GetSellerWrappedCode and unit tests that uses GetSellerWrappedCode
* Add event code and private aggregation objects for contributeToHistogramOnEvent, also added tests for event field
* add EventMessage to log context
* Add helper functions convertEventToInt and convertBaseValueToInt and tests for them for contributeToHistogramOnEvent for bidding service
* Add Inference Metrics like count, duration, error and size to TF & Pytorch sidecars.
* add option to switch DebugInfo in secure invoke encrypt
* Add PrivateAggregateContribution proto with event field and enum EventType to the .proto file
* Add proto messages for SignalValue, PrivateAggregationValue, and BaseValue enum.
* Add request creation timestamp to ProtectedAuctionInput
* Add request/response to ExecuteInternal on async clients
* Add support for collecting metrics from the Roma callbacks.
* Add tee-container-log-redirect option in terraform
* Add wrapper and test files with method headers for contributeToHistogramOnEvent for bidding service
* Added `isValidCustomEvent` validation function for common.privateAggregation
* Added isValidValue validation function for common.privateAggregation
* Added new files for JS private aggregation util for bidding service
* Bash script for ASG and Cloud Map Custom HealthChecks
* Bid Currency Support for Top-Level Seller in Server-Orchestrated Multi-Seller Auctions
* Change js helper convertEventToInt to mapEventToEnum to return string corresponding to enum instead of int
* configured public key urls are verified against an explicit allowlist
* Consented request replace enable_adtech_code_logging in Bidding Server
* contributeToHistogram with test for bidding service
* contributeToHistogramOnEvent with tests, using common private aggregation helpers
* Convert Private Aggregation wrapper functions from .js to C++ string
* Create a periodic model fetcher library for inference
* Create inference model store for model management
* Create logging library for inference consented logs
* Debug Reporting for Bid Currency
* Declare inference JS error schema
* Declare schema for model fetching metadata file
* decrease aws/build_and_test duration by ~75%
* Enable blob fetcher to only fetch blobs with given prefixes
* Enable dynamic model loading for inference
* Enable Service Mesh on AWS by Default
* export non-privacy log through otel with safe system context
* Feature Flag for TLS in Service Mesh
* Force chaffing to enabled for prod builds on BFE + update common repo dep
* Force the ML model reset with the probability of 0.1%
* Forward per request consented debugging config to roma callback
* Handle chaff and new request format on BFE
* Implement AWS Cloud Un-Map
* Implement new SFE <> BFE request format for chaffing
* Implement the probabilistic model reset for PyTorch
* Implement the probabilistic model reset for TensorFlow.
* Instrument inference JS error for the PyTorch sidecar
* Integrate with Trusted KV Server in Mesh
* Load models using model metadata config for the periodic model fetcher
* Load Test Flag for AWS
* Log consented debugging information in inference sidecar
* log EventMessage in servers
* log udf log in EventMessage for non_prod debug_info
* Make chaffing flag configurable via Terraform
* move ig metric to bfe
* Propagate inference error back to JS caller
* Remove enableAdtechCodeLogging flag value
* Remove Envoy Access Logging
* Route consented inference requests to a consented model store
* send chaff requests from SFE
* Service Mesh in AWS
* Support CPU isolation in the inference sidecar
* Unit test for loading parsing fake contributions in Roma for bidding service
* Unpad KV server responses
* update code/cloud build to use tags
* Update Demo Terraform Configs with values for integrating TEE KV Servers into Mesh
* Upgrade AWS Provider for Terraform from v3.xx to v4.xx
* Use gRPC for AWS Service Mesh Envoy HCs
* Write a fake generateBid() script which calls privateAggregation.contributeToHistogramOnEvent(event, <fake contribution>)


### Bug Fixes

* [IsolateBuyerAndSellerCodeExecution] Add a new class for buyer's reporting code fetch and load
* [IsolateBuyerAndSellerCodeExecution] Add a new code wrapper with only scoreAd and reportResult
* [IsolateBuyerAndSellerCodeExecution] Add config flag to enable seller and buyer code isolation
* [IsolateBuyerAndSellerCodeExecution] Modify seller_udf_manager to fetch and load buyer udfs
* [IsolateBuyerAndSellerCodeExecution] Refactor the code fetch files.
* Add BidCurrency SUT to Pre-Submit
* add check back in default grpc client
* Add createContribution inside contributeToHistogram/OnEvent and fixed typo with createContribution's value conditional statement.
* Add DebugInfo pointer for debugging log into RomaRequestContext
* Add generation_id to chaff requests
* Adds domain equality validations to buyer reporting UDF URL
* Align plaintext buyer request for basic SUT with encrypted version
* Allow up to 15 buyers when chaffing enabled
* chaffing bugs ([027d5bb]( )), closes [/github.com/abseil/abseil-cpp/blob/master/absl/container/internal/raw_hash_set.h#L1572]( )
* Changed raw string delimiter JSCODE to JS_CODE for consistency
* clean up log verbosity 3
* Correct output_filter typo
* do not try to impersonate service accounts if TEST_MODE=true
* Don't set chaff size for non-chaff requests
* Eliminate Terraform Error Message about empty Authority Field
* Ensure instance id is set in logs on AWS when not using mesh.
* Execute Callback for empty HTTP request vector
* Fix bugs in reportWin URL validation in auction service
* Fix release notes by adding a dedicated tag on main to generate the changelog
* make num_chaff_requests not have a static lower bound
* Make terraform for aws delete cloud maps without error
* Redirect misleading log to /dev/null
* scorecard.yaml version updates
* shuffle request order (real and chaff) on SFE
* truncate curl failure info for fail count metric
* Update setup_2 and demo terraform configs to valid state
* Update SUTs to include and test Experiment Group ID
* Upgrades google terraform plugin to 5.31.0 to fix crash


### Documentation

* Add PAS input example to BFE
* Document how to deployment B&A without inference
* Update inference_sidecar README with model size limits
* Updated expected format for INFERENCE_MODEL_BUCKET_PATHS in README

## 3.10.0 (2024-07-13)


### Features

* [IsolateBuyerAndSellerCodeExecution] Add wrapper for buyer's reportWin() udf
* [reporting] Enable reportResult execution for topLevelSeller
* Add chaffing feature flags in SFE/BFE.
* Add config flag enable_private_aggregation_generation to auction service's runtime_config
* Add distribution of IGs related metrics
* add EventMessage to log context
* Add request creation timestamp to ProtectedAuctionInput
* Add request/response to ExecuteInternal on async clients
* Add support for collecting metrics from the Roma callbacks.
* Add tee-container-log-redirect option in terraform
* Bash script for ASG and Cloud Map Custom HealthChecks
* Bid Currency Support for Top-Level Seller in Server-Orchestrated Multi-Seller Auctions
* configured public key urls are verified against an explicit allowlist
* Consented request replace enable_adtech_code_logging in Bidding Server
* Create inference model store for model management
* Create logging library for inference consented logs
* Debug Reporting for Bid Currency
* decrease aws/build_and_test duration by ~75%
* Feature Flag for TLS in Service Mesh
* Force the ML model reset with the probability of 0.1%
* Forward per request consented debugging config to roma callback
* Handle chaff and new request format on BFE
* Implement AWS Cloud Un-Map
* Implement new SFE <> BFE request format for chaffing
* Implement the probabilistic model reset for PyTorch
* Implement the probabilistic model reset for TensorFlow.
* Integrate with Trusted KV Server in Mesh
* Load Test Flag for AWS
* Log consented debugging information in inference sidecar
* log EventMessage in servers
* log udf log in EventMessage for non_prod debug_info
* Remove enableAdtechCodeLogging flag value
* Remove Envoy Access Logging
* Route consented inference requests to a consented model store
* send chaff requests from SFE
* Service Mesh in AWS
* Support CPU isolation in the inference sidecar
* Unpad KV server responses
* update code/cloud build to use tags
* Upgrade AWS Provider for Terraform from v3.xx to v4.xx
* Use gRPC for AWS Service Mesh Envoy HCs


### Bug Fixes

* [IsolateBuyerAndSellerCodeExecution] Add a new class for buyer's reporting code fetch and load
* [IsolateBuyerAndSellerCodeExecution] Add a new code wrapper with only scoreAd and reportResult
* [IsolateBuyerAndSellerCodeExecution] Add config flag to enable seller and buyer code isolation
* [IsolateBuyerAndSellerCodeExecution] Modify seller_udf_manager to fetch and load buyer udfs
* [IsolateBuyerAndSellerCodeExecution] Refactor the code fetch files.
* Add BidCurrency SUT to Pre-Submit
* add check back in default grpc client
* Add DebugInfo pointer for debugging log into RomaRequestContext
* Add generation_id to chaff requests
* Adds domain equality validations to buyer reporting UDF URL
* Align plaintext buyer request for basic SUT with encrypted version
* clean up log verbosity 3
* do not try to impersonate service accounts if TEST_MODE=true
* Eliminate Terraform Error Message about empty Authority Field
* Ensure instance id is set in logs on AWS when not using mesh.
* Execute Callback for empty HTTP request vector
* make num_chaff_requests not have a static lower bound
* Make terraform for aws delete cloud maps without error
* Redirect misleading log to /dev/null
* scorecard.yaml version updates
* shuffle request order (real and chaff) on SFE
* Update setup_2 and demo terraform configs to valid state
* Upgrades google terraform plugin to 5.31.0 to fix crash

## 3.9.0 (2024-06-25)


### Features

* [IsolateBuyerAndSellerCodeExecution] Add wrapper for buyer's reportWin() udf
* [reporting] Enable reportResult execution for topLevelSeller
* Add chaffing feature flags in SFE/BFE.
* add EventMessage to log context
* Add request creation timestamp to ProtectedAuctionInput
* Add request/response to ExecuteInternal on async clients
* Add tee-container-log-redirect option in terraform
* Bash script for ASG and Cloud Map Custom HealthChecks
* Consented request replace enable_adtech_code_logging in Bidding Server
* Create inference model store for model management
* Create logging library for inference consented logs
* Debug Reporting for Bid Currency
* decrease aws/build_and_test duration by ~75%
* Feature Flag for TLS in Service Mesh
* Force the ML model reset with the probability of 0.1%
* Forward per request consented debugging config to roma callback
* Implement AWS Cloud Un-Map
* Implement new SFE <> BFE request format for chaffing
* Implement the probabilistic model reset for PyTorch
* Implement the probabilistic model reset for TensorFlow.
* Load Test Flag for AWS
* Log consented debugging information in inference sidecar
* log EventMessage in servers
* log udf log in EventMessage for non_prod debug_info
* Remove enableAdtechCodeLogging flag value
* Remove Envoy Access Logging
* Route consented inference requests to a consented model store
* send chaff requests from SFE
* Service Mesh in AWS
* Support CPU isolation in the inference sidecar
* update code/cloud build to use tags
* Upgrade AWS Provider for Terraform from v3.xx to v4.xx
* Use gRPC for AWS Service Mesh Envoy HCs


### Bug Fixes

* [IsolateBuyerAndSellerCodeExecution] Add a new class for buyer's reporting code fetch and load
* [IsolateBuyerAndSellerCodeExecution] Add a new code wrapper with only scoreAd and reportResult
* [IsolateBuyerAndSellerCodeExecution] Add config flag to enable seller and buyer code isolation
* [IsolateBuyerAndSellerCodeExecution] Modify seller_udf_manager to fetch and load buyer udfs
* [IsolateBuyerAndSellerCodeExecution] Refactor the code fetch files.
* add check back in default grpc client
* Add DebugInfo pointer for debugging log into RomaRequestContext
* Add generation_id to chaff requests
* clean up log verbosity 3
* do not try to impersonate service accounts if TEST_MODE=true
* Eliminate Terraform Error Message about empty Authority Field
* Ensure instance id is set in logs on AWS when not using mesh.
* Execute Callback for empty HTTP request vector
* make num_chaff_requests not have a static lower bound
* scorecard.yaml version updates
* Upgrades google terraform plugin to 5.31.0 to fix crash

## 3.8.0 (2024-05-24)


### Features

* Add tee-container-log-redirect option in terraform
* Create logging library for inference consented logs
* Forward per request consented debugging config to roma callback
* Support CPU isolation in the inference sidecar


### Bug Fixes

* [IsolateBuyerAndSellerCodeExecution] Add a new code wrapper with only scoreAd and reportResult
* clean up log verbosity 3

## 3.7.0 (2024-05-13)


### Features

* Add the CPU affinity lib for inference


### Bug Fixes

* Cap debug reporting URL
* Fix the recency value passed to generateBid and scoreAds
* make aws-image-tag optional
* remove unitOverride in gcp seller dashboard terraform
* replace ABSL_LOG with PS_LOG in reactors
* update empty device signals to be an object, not string

## 3.6.0 (2024-05-01)


### Features

* [Debug Reporting] Enable debug reporting by default
* [DebugReporting] Add a log to get the libcurl queue time
* add ability to tag AMIs
* Add kv e2e tests environment
* Add markdown-link-check to pre-commit
* add support for AWS CodeBuild
* add support for GCP Cloud Build
* API Updates for Bid Currency Support for Top-Level Seller in Server-Orchestrated Multi-Seller Auctions
* Enable TCMalloc for all B&A servers
* support sending Nitro Enclave logs to AWS CloudWatch for debugging


### Bug Fixes

* [Temp] Adds buyer bid to top level auctions for Chrome < v125
* Adjust ps_vlog pre-commit hook to avoid recursing symlinks
* clean up log verbosity 2
* **deps:** Upgrade clang-format to v18
* **deps:** Upgrade pre-commit hooks
* Ensure that the debug url length is capped

## 3.5.0 (2024-04-24)


### Features

* Add runtime check for correct inference backend version
* Adds debug info to secure invoke response
* Adds implementation of GetComponentAuctionCiphertexts API
* Adds optional flag to configure cloud platforms for component sellers
* allow AWS Session Manager instance connection
* enable bucket fetching for score ad
* Enables and adds tests for GetComponentAuctionsCiphertexts API
* Enables Top Level Auctions feature
* Implement Support for Bid Currency in Reporting
* Set Seller Rejection Reason on AdWithBid for Mismatch with Expected Buyer Currency
* Set Seller Rejection Reasons on AdScore for currency-related rejectons
* Support inference runtime configurations
* Support Interest Group Origin in InterestGroup and AuctionResponse
* Top Level auction does not accept multiple results from same seller
* upload aws/gcp hashes as artifacts on release


### Bug Fixes

* Correct check condition in inference main
* Increase the gRPC recv message limit for the inference sidecar
* Pass complete ad object to scoreAd
* Remove redundant clang-tidy repo, move config to .clang-tidy
* RPC is finished properly in top level auction with reporting
* Temporarily add bazel_clang_tidy repo

## 3.4.0 (2024-04-02)


### Features

* Add Bid Currency to PAS bidMetadata for scoreAd()
* Add blob fetch library
* Adds API for generating ciphertexts for server component auctions
* Adds proto mapping functions for top level auction reactor
* Adds reactor for handling top level auction in SFE
* Adds utility for re-encrypting ciphertexts from device
* Adds validator functions for top level auction reactor
* Build version dynamically for telemetry
* Check Bid Currency on Protected App Signals AdWithBids
* create SellerCodeFetchManager for auction service UDF fetching logic
* Enable cpp_no_warn mode by default
* Enable ML inference in B&A
* Implements server component auctions feature in SFE
* Implements support for server component auctions in secure invoke
* Implements top level auction feature in auction service
* Implements top level auctions support in secure invoke


### Bug Fixes

* Add get_workspace_status script to github repo
* Create extra function to only add zone attribute to gcp deployment
* Delete Right AdWithBids for Currency Mismatch in SFE
* Do not check trailing space for patch files
* improve graceful shutdown of bidding server
* Simplify V8Dispatcher::LoadSync, switch to absl::Notification
* Update data plane library dependency
* update generate bid to receive empty json object when no device signals are present


### Dependencies

* **deps:** Upgrade build-system to 0.57.0

## 3.3.0 (2024-03-11)


### Features

* create BuyerCodeFetchManager to handle buyer udf fetching logic
* enable consented debugging in AWS deployment
* integrate buyer code fetch management
* Output raw metric for consented request

## 3.2.0 (2024-03-06)


### Features

* Add a metric attribute for instance region
* Add bid rejection reasons for bid currency
* Add bid_currency to CBOR Auction Response
* Add binary HTTP Utils
* add component_reporting_urls to SelectAdResponse
* Add KV client for TEE retrieval
* Add pylint to pre-commit
* Add TF flags for PAS KV/retrieval service
* Adds API support for server component auctions
* Adds HPKE encryption utility for server component auction
* Adds OHTTP Encryption utils for sharing with top level auction reactor
* Adds proto field to support top level auction
* Adds util for resolving AuctionScope with SelectAdRequest
* Check bid currency on AdWithBids against buyer currency in SFE
* Disable/enable core dumps based on build flavour
* Enable prettier pre-commit hook for JavaScript
* Enable prettier pre-commit hook for JSON
* Instantiate KV GRPC Client in bidding
* Integrate bidding with KV service
* Oblivious HTTP Utils
* PAS contextual ads API changes
* Pass Contextual PAS Ads from SFE -> BFE
* Relay contextual PAS Ads from BFE -> Bidding
* Support Bid Currency in the ScoreAdsReactor
* update gcp collector to use internal proxy network load
* Use GRPC client for ad retrieval
* Validate currencies in the auction config


### Bug Fixes

* Avoid race conditions when turning PyTorch models into eval mode
* correct branching condition in register model request
* Correct git submodule links
* Fix debug statements to include result status
* Fix flaky reporting_helper_test
* resolve inference sidecar path for unit test
* update kv service hash after repo name change


### Documentation

* Fix minor typos in load testing doc


### Dependencies

* **deps:** Downgrade build-system to v0.52.0
* **deps:** Upgrade build-system to 0.53.0
* **deps:** Upgrade build-system to 0.55.1
* **deps:** Upgrade build-system to 0.56.0
* **deps:** Upgrade functionaltest-system to v0.12.0

## 3.1.0 (2024-01-25)


### Features

* [reporting] Add set modifiedBid value in sellerReportingSignals for multi seller auctions
* Add  metric for size of  protected_ciphertext and auction_config
* add additional metrics to the AWS seller dashboard
* Add AWS perfgate benchmark
* Add aws s3 support to perf-test-helpers
* Add metric attribute for instance zone
* Add perfgate analyzer support
* Add Support for Buyer Experiment Group ID
* Add tf vars for otel collector template file and image uri
* API Changes for Bid Currency
* Builds AWS AMI with debug otel collector
* create AWS seller and buyer metric dashboards for monitoring
* load and make inference request with a PyTorch model
* Seller KV Experiment Group ID
* Test Bid Currency on AdWithBids returned from generateBid()


### Bug Fixes

* ad metadata string is escaped to be valid json in browser
* Add run_all_tests bazel config
* Bazel builds should ignore the cost tool
* Consider all 2XX HTTP codes as success
* **deps:** Upgrade data-plane-shared-libraries to 44d1d64 2024-01-08
* Ensure that non-200 status codes become errors
* Fix build flag and copybara rule
* Fix custom quickstore input file bug
* Fix debug statements to include result status
* Fix flaky sandbox_executor_test
* Prevent Config from being copied into RomaService
* Removes a test breaking the build
* secure_invoke client stub lifetime prolonged to wait for call to end
* Temporarily revert changes to the SUT so that it passes

## 3.0.0 (2023-12-14)


### ⚠ BREAKING CHANGES

* turn on metric noise (a minimum of 5 QPS is recommended for noised metric)
* require consented token min length 6

### Features

* Add metric attribute for B&A release version
* Add metric attribute for operator name
* Add perf-test-helpers library
* add sfe metric for request with win ad
* **component auction:** Adds test for component auction support in secure invoke
* **dep:** Update build-system to release-0.52.0
* Import perfgate exporter and uploader tars
* require consented token min length 6
* turn on metric noise (a minimum of 5 QPS is recommended for noised metric)
* Update perf environment to use custom Otel Collector
* Use hardcoded adtech code and kv mock from e2e-testing env in perf env


### Bug Fixes

* Auction service parses adMetadata object
* Consider non-positive desirability ads as rejected
* Fixes broken test
* Log JS errors conditionally
* Make the error message compatible with deterministic CBOR
* minimize secure_invoke reliance on default arguments
* Remove rejected ads from consideration in scoring
* Update B&A to integrate RomaService Changes

## 2.8.0 (2023-12-07)


### Features

* [reporting] Add noiser and bucketer for noising reporting inputs
* [reporting] Pass  modeling signals through noiser before being input to reportWin
* Add ps verbosity tf var
* Add scoring support for PAS
* Add support for new OHTTP request format.
* build PyTorch from source for B&A inference server.
* Collector script for performance testing
* **component auction:** Auction server passes top-level seller to scoreAd
* **component auction:** Auction service parses output and skips allowComponentAuctions = false ads
* **component auction:** Bidding server passes top-level seller to generateBid
* **component auction:** Bidding service parses output and skips allowComponentAuctions = false bids
* **component auction:** BuyerFrontEnd Service accepts and forwards top level seller
* **component auction:** Return error for Android device orchestrated component auctions
* **component auction:** SellerFrontEnd service accepts and forwards top level seller
* **component auction:** SFE service parses output cbor encodes for chrome
* includes git info tag for gcp docker images
* monitor key fetch metrics for B&A servers
* Partition request fail metric by the status message
* Upgrade functionaltest-system to v0.11.0


### Bug Fixes

* Add check for correct key ID in select_ad_reactor tests.
* consented logger memory leak
* Fixes test failures in select ad app reactor
* log missing key id in grpc status
* match copybara strip style for PyTorch build from source
* refactor gcp artifact build scripts to be more modular
* Remove local platform for common repo and update CloudPlatform
* Remove unnecessary reporting flags in gcp config
* update GCP build to write image_digest, not image id


### Dependencies

* **deps:** Upgrade data-plane-shared to commit 115edb3 2023-11-16

## 2.7.0 (2023-11-08)


### Features

* add debug info in sfe response
* Add reporting support for PAS
* Instrument KV request and response sizes
* Parameterize the collector startup script
* write docker sha256 image digest to file for GCP releases


### Bug Fixes

* Send chaff when no desirable ad is found

## 2.6.0 (2023-10-30)

### Features

* Add common error count by error code as a sever monitoring metric

### Dependencies

* **deps:** Upgrade build-system to 0.49.1

## 2.5.0 (2023-10-30)


### Features

* Add support for interestGroupNames
* Temporarily Hardcode Maximum Buyers Called to Two
* Use set instead of vector for Key-Value Server input key fields


### Bug Fixes

* Change Set type to bTree to ensure deterministic key order
* read PS_VERBOSITY from cloud parameter
* Update visibility target to public target in common repo


### API: Features

* **api:** Add fields for component auction support


### Documentation

* Add load testing guide

## 2.4.0 (2023-10-18)


### Features

* support passing in a custom public key/id to secure_invoke


### Bug Fixes

* [reporting] Fix the description for component_seller_reporting_urls and top_level_seller_reporting_urls in the API
* [reporting] Remove unnecessary flags in aws config
* increases limit for default gRPC message size between servers


### Dependencies

* **deps:** Upgrade build-system to 0.48.0

## 2.3.0 (2023-10-09)


### Features

* Add GLOG env vars to local functional testing auction service container
* Add PAS support in bidding service
* add ps_verbosity flag to bidding, auction
* pass a client_type url param to KV services only when
* run otel collector inside a docker image on Container Optimized OS
* Skip call to Bidding Server when no Trusted Bidding Signals present
* update the service local start scripts to use docker


### Bug Fixes

* Fixes crash in SFE/BFE due to curl handle lookup failure
* update functional test with new client type enum

## 2.2.0 (2023-09-27)


### Features

* Add more buyer flags
* add support for a dedicated non-tls healthcheck port
* add utilization in cpu metric
* allow granular GCP per-region machine config
* change GCP sfe and bfe health checks to use grpc
* simplify GCP SFE envoy config


### Bug Fixes

* Add Ad Retrieval flags for AWS
* Disable use of secure_invoke for functional testing
* Ensure the easy handle is copied
* Fixes the metadata keys sent to bidding
* Use the verbose level 1 for consented logs


### Dependencies

* **deps:** Upgrade build-system to 0.45.0

## 2.1.0 (2023-09-19)


### Features

* add boundary for unsafe histogram metric
* Add docker-based sut using data from fledge sandbox
* Add HTTP async client for ads retrieval
* Add MultiLogger as an unified logging interface
* add noise attribute to metric
* Add secure_invoke rpc invoker for rpc diff testing
* Allow clients to pass in a PublicKeyFetcher into KeyFetcherFactory.
* Allow services to override the code blob versions
* Deserialize ads retrieval response
* Log plaintext responses in B&A servers
* Log plaintext ScoreAdsRawRequest in Auction
* Remove require-ascii pre-commit check on changelog
* Skip call to Auction Service and return chaff when no scoring signals present
* Take auction type in account in GenerateBid
* Upgrade to functionaltest-system 0.9.0


### Bug Fixes

* Add a missing header file
* Fix a broken test in MultiLogger
* Revert the change in builders and testing/functionaltest-system

## 2.0.0 (2023-09-12)


### Features

* Add C++ code coverage support
* Add PUT support in HTTP client
* Add SetContext() in ConsentedDebuggingLogger
* Change secure_invoke output to be easily parsable by automated scripts
* Log plaintext GenerateBidsRawRequest in Bidding
* override metric's default public partition
* PAS support for BFE
* turn on external load balancer logging by default


### Bug Fixes

* Change generate_bid_code_version from int to string in API
* [logging] Check for log fields in the response json from reporting scripts
* [logging] VLOG the logs, errors and warnings exported from Roma for reporting.
* [reporting] Add ad_cost and interest_group_name inputs to the reportWin function input
* Add copyright line in license block
* Add missing adCost to buyer's browser signals in the code wrapper
* Allow ScoreAd to return a number
* Check rapidjson property presence before access
* Fix invalidCode error when buyer's script is not a correct expression
* Fixes a comment in proto
* revert documentation update until the build_and_test_all_in_docker script is updated
* safe metric should not LogUnsafe
* Set the log context in BFE
* Updates the client types comments


### cleanup

* Change generate_bid_code_version from int to string in API

## 1.2.0 (2023-09-01)


### Features

* Add code blob flags for bidding/auction
* Log plaintext GetBidsRawRequest in BFE
* PAS support in SFE
* Propagate consented debug config from BFE to Bidding
* Propagate consented debug config from SFE to BFE & Auction


### Bug Fixes

* [reporting] Change the order of cbor encoding for win reporting urls in the response.
* [reporting] Populate win reporting urls for seller in top_level_seller_reporting_urls instead of component_seller_reporting_urls
* [reporting] Remove unnecessary signalsForWinner in the response from reporting wrapper function
* Disable enableSellerDebugUrlGeneration flag by default

## 1.1.0 (2023-08-25)


### Features

* Add B&A App Install API
* Add ConsentedDebuggingLogger to write logs via OTel
* Add feature flag enabling/disabling PAS
* Add IG origin to AuctionResult
* add max surge to instance groups
* Add owner field for PAS Ad with bid
* Adds docker build option for local testing
* Adds PAS buyer input to GetBids
* Check debug token in ConsentedDebuggingLogger
* Include IG owner in buyer input
* Log decoded buyer inputs for consented debugging in SFE
* make export interval configurable
* make metric list configurable
* OpenTelemetry logging can be disabled via TELEMETRY_CONFIG flag
* Propagate Ad type from Auction => SFE
* update GCP terraforms to apply updates to instances without
* update LB policy to default to RATE instead of UTILIZATION
* Upgrade to functionaltest-system 0.8.0
* use private meter provider not shared with Otel api


### Bug Fixes

* Bump required tf to 1.2.3 to be compatible with replace_triggered_by
* Change componentAds -> components
* Correct GCP dashboards
* Do not set runtime flags with empty strings
* Fixes race condition in SFE reactor due to mutex lock
* Log a message in case of server flag lookup failures
* Removes callback execution from default_async_grpc_client to prevent double invocation
* specify default cpu util before sending requests to other regions
* update OTel demo documentation
* Use bazel config to set instance and platform


### Dependencies

* **deps:** Upgrade build-system to 0.43.0

## 1.0.0 (2023-08-10)


### BREAKING CHANGES

* if your generateBid() returns ad component render urls in a field named "adComponentRender". You will have to update this to return these in a field named "adComponents".
Changed name in the API. Updated in reactors. Added a unit test.

Testing: Unit test
Bug: b/294917906
Change-Id: Ie4344f55b18ef10f7a81b197ec997be393fa7368

### Features

* Adds Readme for running B&A servers locally
* Enable CBOR encode/decode for ConsentedDebugConfig
* implement dp aggregated histogram metric
* include local server start scripts
* periodic bucket fetching using BlobStorageClient


### Bug Fixes

* Correct paths to terraform modules in AWS demo.tf files
* Improve clarity of aws vs gcp cases
* Improve flag error handling
* include required dep for bucket code fetcher
* Remove enable_buyer_code_wrapper flag.
* Remove enable_seller_code_wrapper flag.
* Remove enableSellerCodeWrapper flag from aws and demo configs
* remove unnecessary flags in terraform configs
* Set cbuild flag --seccomp-unconfined


### cleanup

* Change AdWithBid.ad_component_render to .ad_components to align with OnDevice generateBid() spec


### Documentation

* Update terraform comments to communicate requirement for env name <= 3 characters

## 0.10.0 (2023-08-03)


### Features

* [reporting] Add helper function for cbor encoding and decoding of Win Reporting Urls
* [reporting] Execute reportWin script and return the urls in the response.
* [reporting] Fix reportWin function execution.
* add AWS enclave PCR0 in release notes
* Add bazel configs for roma legacy vs sandboxed
* Add OSSF Scorecard badge to top-level README
* Add OSSF Scorecard GitHub workflow
* Add the server flags to support user consented debugging
* change autoscale cpu utilization measure to GCP suggested default (60%)
* clarify default lb policy within an instance group is ROUND_ROBIN
* Enable logging in the OTel collector
* Encrypt GetBidsRequest for benchmarking
* Flag protect the opentelemetry based logging
* move observable system metric to server definition
* Upgrade data-plane-shared-libraries for Opentelemetry logs API


### Bug Fixes

* Encode Ad Component Render URLs when sending request to Seller Key-Value Server
* Fixes few ASAN issues
* Minimal CBOR encoding for uint/float
* Order the keys in the response
* Remove unwanted seller_origin flag from start_auction script
* Rename the opentelemetry endpoint flag
* secure_invoke respects --insecure flag for BFE


### API: Features

* **api:** Add the fields to support adtech-consented debugging


### Dependencies

* **deps:** Upgrade build-system to v0.39.0
* **deps:** Upgrade build-system to v0.41.0
* **deps:** upgrade opentelemetry-cpp to 1.9.1 for B&A servers


### Documentation

* update release README instructions
* Update secure_invoke README with new instructions for running.


## 0.9.0 (2023-07-20)


### Features

* [reporting] Add helper function to build the dispatch request for
* add buyerReportWinJsUrls to terraform files and enable_report_win_url_generation to BuyerCodeFetchConfig
* add cpu memory metric for debugging
* add GCP metric dashboard
* add method to accumulate metric values before logging
* changing PeriodicCodeFetcher to add wasm support and runtime config support
* load and append wasm string to the JS code wrapper
* support different instance types per service in GCP
* Upgrade build-system to release-0.31.0


### Bug Fixes

* Add seller_code_fetch_config and buyer_code_fetch_config to server start scripts
* CPU monitoring is not limited to a specific environment
* Define container image log variables once
* Don't end select ad request prematurely
* patch google dp library to replace logging with absl
* Update and read the buyer_bids_ with lock


### Dependencies

* **deps:** Upgrade build-system to v0.33.0

## 0.8.0 (2023-07-12)


### Features

* add FetchUrls utility to MultiCurlHttpFetcher
* enable counter metrics with dp aggregation
* update multi-region.tf to use prod js urls and test mode true
* use //:non_prod_build to configure build


### Bug Fixes

* Adjust all/small test configs
* Adjust sanitizer configs
* Correct example BFE hostname
* Correct license block
* Ensure --gcp-image flags are specified for gcp
* Ensure --instance flag is specified
* fix missing telemetry flag and readme
* Improve build_and_test_all_in_docker usage text

## 0.7.0 (2023-06-30)


### Features

*  [AWS] add example terraform directory with README
* [GCP] add example terraform directory with README
* Add bazel build flag --announce_rc
* add build_flavor for AWS packaging
* add build_flavor for packaging
* include coordinator and attestation support for GCP
* Upgrade build-system to release-0.30.1


### Bug Fixes

* Adjust SFE DCHECKs
* bidding_service_test
* Change PeriodicCodeFetcher to use std::string instead of absl::string_view in the parameters
* refactor the test to share initialization
* remove unnecessary flags
* TEE<>TEE fix
* temporarily eliminate requirement to have device signals to generate bids

## 0.6.0 (2023-06-23)


### Features

* add VLOG(2) for code loaded into Roma


### Bug Fixes

* Correct xtrace prompt

## 0.5.0 (2023-06-22)


### Features

* Add --instance to build_and_test_all_in_docker
* Add bazel configurations for platform and instance flags
* Add flag to config telemetry
* Add smalltests kokoro config
* changing MultiCurlHttpFetcherAsync to take a raw pointer executor.
* create a header file for PeriodicCodeFetcher object wtih constructor, necessary functions, and necessary variables
* create a source, BUILD, and test files for PeriodicCodeFetcher.
* create CodeFetcherInterface as an interface for different CodeFetcher classes.
* enforce list order in metric definition
* implement dp to aggregte (partitioned)counter metric with noise
* Implement GCP packaging scripts (including SFE envoy
* Implement Metric API used by B&A server
* Implement Metric context map
* Implement Metric router to pass safe metric to OTel
* Limit build_and_test_all_in_docker to run small tests only
* modify auction_main.cc and bidding_main.cc to integrate PeriodicCodeFetcher for code blob fetching.
* move GCP instances behind a NAT
* reactor own metric context
* remove hardcoded scoreAd.js and generateBid.js
* update TrustedServerConfigClient to work with GCP
* use telemetry flag to configure metric and trace
* Use terraform flag to specify if debug or prod confidential compute


### Bug Fixes

* [GCP] add docker redirect flag for prod images
* [GCP] specify port instead of port_name for lb healthchecks
* Add bazel coverage support
* add GCP as a bazel config
* add missing gcp flags
* auction service returns if no dispatch requests found
* BFE Segfault after grpc reactor Finish
* complete removal of sideload IG data
* do not reference ScoreAdsReactor private members after grpc::Finish
* flaky auction and bidding tests
* GCP SFE dependencies were outdated
* gcp terraform should use env variable for buyer address
* mark docker and async grpc client stub tests large
* potential SFE segfault due to logging after Finish
* Remove --without-shared-cache flag
* rename service to operator, component to service
* Replace glog with absl_log in status macro
* skip scoring signal fetch and Auction if no bids are returned
* Specify test size
* **test:** Add size to cc_test targets
* update gcp packaging script to support all repos globally
* update GCP SFE runtime flags with new values
* update init_server_basic_script to use operator
* update managed instance group healthcheck
* update pending bids so SFE does not hang
* Use --config=value in .bazelrc
* Use buf --config flag
* validate that the buyer list is not empty


### Build Tools: Features

* **build:** Emit test timeout warnings from bazel


### Build Tools: Fixes

* **build:** Add scope-based sections in release notes
* **build:** Adjust small-tests configuration
* **build:** Create all-tests configuration

## 0.4.0 (2023-06-01)

## 0.3.0 (2023-06-01)

## 0.2.0 (2023-05-22)

## 0.1.0 (2023-05-16)

### API

* Initial release
