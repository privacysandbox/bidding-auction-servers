# Secure Invoke Tool

Secure invoke binary can be used to:

-   Send a `SelectAdRequest` to SFE.
-   Send a `GetBidsRawRequest` directly to BFE.

The tool takes as input an unencrypted request and then serializes, compresses, pads and then
encrypts it with test keys (thus the services are also expected to be running with `TEST_MODE` flag
enabled). The response returned by the target service is then similarly decrypted (with test keys),
decompressed, deserialized and printed to console.

## Example Usage

### Sending [SelectAdRequest] to SFE

```bash
# Setup arguments.
INPUT_PATH=/tmp/select_ad_request.json  # Needs to be a valid SelectAdRequest
SFE_HOST_ADDRESS=seller.domain.com  # DNS name of SFE (e.g. dns:///seller.domain.com)
CLIENT_IP=<A valid client IPv4 address>

# Run the tool with desired arguments.
bazel run //tools/secure_invoke:invoke \
    -- \
    -target_service=sfe \
    -input_file="${INPUT_PATH}" \
    -host_addr=${SFE_HOST_ADDRESS} \
    -client_ip=${CLIENT_IP}
```

Notes:

-   The input request _must_ be specified in JSON format when sending `SelectAdRequest` to an SFE.
-   The request in the file must be plaintext and should have `raw_protected_audience_input` present
    instead `protected_audience_ciphertext`.
-   Full path to the input request must be specified.

### Sending [GetBidsRawRequest] to BFE

Following example shows how protobuf based plaintext `GetBidsRawRequest` payload can be used to send
gRPC request to BFE:

```bash
# Setup arguments.
INPUT_PATH=/tmp/get_bids_request.txt  # Needs to be a valid GetBidsRawRequest
INPUT_FORMAT=PROTO
BFE_HOST_ADDRESS=buyer.domain.com  # DNS name of BFE service (Example: dns:///buyer.domain.com)
CLIENT_IP=<A valid client IPv4 address>

# Run the tool with desired arguments.
bazel run //tools/secure_invoke:invoke \
    -- \
    -target_service=bfe \
    -input_file="${INPUT_PATH}" \
    -input_format=${INPUT_FORMAT} \
    -host_addr=${BFE_HOST_ADDRESS} \
    -client_ip=${CLIENT_IP}
```

Following example shows how JSON based plaintext `GetBidsRawRequest` payload can be used to send
gRPC request to BFE:

```bash
# Setup arguments.
INPUT_PATH=/tmp/get_bids_request.json  # Needs to be a valid GetBidsRawRequest
INPUT_FORMAT=JSON
BFE_HOST_ADDRESS=buyer.domain.com  # DNS name of BFE service (Example: dns:///buyer.domain.com)
CLIENT_IP=<A valid client IPv4 address>

# Run the tool with desired arguments.
bazel run //tools/secure_invoke:invoke \
    -- \
    -target_service=bfe \
    -input_file="${INPUT_PATH}" \
    -input_format=${INPUT_FORMAT} \
    -host_addr=${BFE_HOST_ADDRESS} \
    -client_ip=${CLIENT_IP}
```

Notes:

-   `target_service` flag must be set to `bfe` when sending a `GetBidsRawRequest` to a BFE.
-   The input request can be specified either as JSON or proto.

[selectadrequest]:
    https://github.com/privacysandbox/bidding-auction-servers/blob/332e46b216bfa51873ca410a5a47f8bec9615948/api/bidding_auction_servers.proto#L225
[getbidsrawrequest]:
    https://github.com/privacysandbox/bidding-auction-servers/blob/332e46b216bfa51873ca410a5a47f8bec9615948/api/bidding_auction_servers.proto#L394
