# GenerateBid Onboarding Guide

## Step 1. Familiarize youself with the binary specification

The generateBid binary adheres to a specific protocol defined using
[Protocol Buffers (proto)](https://protobuf.dev/) objects.

### Proto Definitions

#### Binary Signature

```text
GenerateProtectedAudienceBid(GenerateProtectedAudienceBidRequest) returns (GenerateProtectedAudienceBidResponse)
```

#### GenerateProtectedAudienceBidRequest

Contains similar fields to the JS `generateBid` parameters.

**Fields**:

-   `interest_group`: Interest group information from the device.
-   `auction_signals`: Signals for auction in auction config specified by seller.
-   `per_buyer_signals`: Signals for auction buyer specified in auction config by seller.
-   `trusted_bidding_signals`: Signals for auction fetched from buyer KV server.
-   `browser/android signals`: Signals for auction prepared by device and B&A servers.
-   `server_metadata`: Signals for binary prepared by B&A servers.

For details, refer to the [protobuf interface](../specs/udf_interface.proto). A sample request is
included for debugging and testing [[json]](requests/sample.json)
[[textproto]](requests/sample.txtpb).

#### GenerateProtectedAudienceBidRequest

Contains similar values to the JS `generateBid` return object.

**Fields**:

-   `bids`: Bids for the current interest group. Can return multiple bids when k-anon verification
    is enabled. If k-anon is not enabled, Bidding service will parse the first bid and ignore the
    rest.
-   `log_messages`: Debug or error log messages returned by the binary.

For details, refer to the [protobuf interface](../specs/udf_interface.proto).

## Step 2. Develop your generateBid binary

1. Write a generateBid UDF that runs in the
   [binary execution environment](udf/Execution%20Environment%20and%20Interface.md) and follows the
   [binary communication protocol](udf/Communication%20Interface.md).

1. Leverage the [tools](/tools/) included in the SDK to aid the development and testing of the UDF.

1. Build the generateBid UDF into a standalone executable binary file.

Once the UDF has been built into a standalone binary file, see the next section for how it can be
loaded and tested in the Bidding server using a file path (for local servers only), a public HTTP/S
URL, or a cloud bucket.

## Step 3. Test with Bidding server

**Prerequisite:** Before beginning this step, make sure you have checked out the
[B&A code repo](https://github.com/privacysandbox/bidding-auction-servers). After checking out the
repo, follow the
[local testing guide](https://github.com/privacysandbox/bidding-auction-servers/blob/cbd42f292c79b33d5459887933c6e63ca2fe6944/tools/debug/README.md)
to build B&A services on your system. For step by step guidance on enrollment and deployment of B&A
services, follow the
[self-serve guide](https://github.com/privacysandbox/protected-auction-services-docs/blob/2e8d1e9f5f4302ea495c5a1a1a852fd9d01cf607/bidding_auction_services_onboarding_self_serve_guide.md).

### Local server

If you want to use a local file path, follow these steps to add it to the Bidding server container:

1. Add the binary file to the `tools/udf/generate_bid/samples` folder.
1. Add the relative path to the `sample_generate_bid_execs` target in the BUILD file in the folder.
   For example, for a file with the path
   `tools/udf/generate_bid/samples/<path>/generate_bid_binary`:

    ```python
    filegroup(
        name = "sample_generate_bid_execs",
        srcs = [
            ":sample_generate_bid_udf",
            "<path>/generate_bid_binary"
        ],
    )
    ```

1. Add `sample_generate_bid_execs_tar` to the Bidding server docker image for
   [AWS](https://github.com/privacysandbox/bidding-auction-servers/blob/cbd42f292c79b33d5459887933c6e63ca2fe6944/production/packaging/aws/bidding_service/BUILD)
   or
   [GCP](https://github.com/privacysandbox/bidding-auction-servers/blob/cbd42f292c79b33d5459887933c6e63ca2fe6944/production/packaging/gcp/bidding_service/BUILD):

    ```python
    container_image(
       name = "server_docker_image",
       ...
       tars = [
          ...
          "//tools/udf/generate_bid/samples:sample_generate_bid_execs_tar",
       ],
    )
    ```

1. The file should now be available to the Bidding server at the path
   `/sample_udf/bin/<path>/generate_bid_binary`.

To begin testing,

1. Build the code in non_prod mode as specified in the
   [local testing guide](https://github.com/privacysandbox/bidding-auction-servers/blob/cbd42f292c79b33d5459887933c6e63ca2fe6944/tools/debug/README.md).
1. Edit the `BUYER_CODE_FETCH_CONFIG` flag in the
   [bidding server start script](https://github.com/privacysandbox/bidding-auction-servers/blob/cbd42f292c79b33d5459887933c6e63ca2fe6944/tools/debug/start_bidding_byob)
   to use the executable as per the
   [BYOB deployment guide](https://github.com/privacysandbox/bidding-auction-servers/blob/cbd42f292c79b33d5459887933c6e63ca2fe6944/production/deploy/gcp/terraform/environment/demo/README.md).
1. Use the updated script to start the bidding server.
1. Continue the
   [test](https://github.com/privacysandbox/bidding-auction-servers/blob/cbd42f292c79b33d5459887933c6e63ca2fe6944/tools/debug/README.md#test-buyer-stack)
   as before from this guide (using gRPCurl or
   [secure_invoke](https://github.com/privacysandbox/bidding-auction-servers/tree/cbd42f292c79b33d5459887933c6e63ca2fe6944/tools/secure_invoke)).
1. You should see logs from the binary in the console for each IG included in the request.

### Cloud server

To deploy to a cloud server, the binary file must be available from a public HTTP/S URL or a cloud
bucket. For example, let us assume that the binary is available at
`https://example.com/generate_bid_binary`.

To begin testing,

1. Build the servers with non_prod config as specified in the cloud deployment guide.
1. Edit the `BUYER_CODE_FETCH_CONFIG` value in the
   [buyer terraform](https://github.com/privacysandbox/bidding-auction-servers/blob/e40a4fccdce168379189ab7b6b87b55b1e3f736d/production/deploy/gcp/terraform/environment/demo/buyer/buyer.tf#L76)
   as per the
   [BYOB deployment guide](https://github.com/privacysandbox/bidding-auction-servers/blob/cbd42f292c79b33d5459887933c6e63ca2fe6944/production/deploy/gcp/terraform/environment/demo/README.md#bring-your-own-binary-byob-flags).
1. Deploy the servers using terraform apply.
1. Test the setup using gRPCurl or
   [secure_invoke](https://github.com/privacysandbox/bidding-auction-servers/tree/cbd42f292c79b33d5459887933c6e63ca2fe6944/tools/secure_invoke).
1. You should see logs from the binary in the cloud console for each IG included in the request.

## Step 4. Deploy to production

generateBid BYOB support is only available for functional testing and we do not recommend enabling
this in production. Scaled support will be available in the future.
