# Changelog

All notable changes to this project will be documented in this file. See [commit-and-tag-version](https://github.com/absolute-version/commit-and-tag-version) for commit guidelines.

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
