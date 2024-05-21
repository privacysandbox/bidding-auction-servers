# Secure Invoke Tool

Secure invoke binary can be used to:

-   Send a `SelectAdRequest` to SFE.
-   Send a `GetBidsRawRequest` directly to BFE.

The tool takes as input an unencrypted request and then serializes, compresses, pads and then
encrypts it with test keys (the default uses the same keys as services running with the `TEST_MODE`
flag enabled). The response returned by the target service is then similarly decrypted,
decompressed, deserialized and printed to console.

## Example Usage

### Sending [SelectAdRequest] to SFE

This method expects a plaintext json with the following fields.

```json
 {
    "auction_config" : {
          "seller_signals": "...",
          "per_buyer_config": {"buyer1" : {
              "buyer_signals": "...",
              ....
          }}
          .....
    },
    "raw_protected_audience_input": {
        "raw_buyer_input" : {"buyer1": {
            "interest_groups": [{
                "name": "IG1",
                "bidding_signals_keys":["IG1",..],
                .....
            }]
        }},
        "publisher_name": "testPublisher.com",
        .....
    },
    "raw_component_auction_results": [
        {
            "ad_render_url": "URL",

        },
        ...
    ]
 }
```

### Top Level Auction [SelectAdRequest] to SFE

This method expects a plaintext json with the following fields.

```json
 {
    "auction_config" : {
          "seller_signals": "...",
          .....
    },
    "raw_protected_audience_input": {
        "publisher_name": "testPublisher.com",
        .....
    },
    "raw_component_auction_results": [{
            "ad_render_url": "URL",
            "interest_group_origin": "testIG.com",
            "interest_group_name": "testIG",
            "interest_group_owner": "testIG.com",
            "bidding_groups": {},
            "score": 4.9,
            "bid": 0.2,
            "bid_currency": "USD",
            "ad_metadata": "{}",
            "top_level_seller": "SFE-domain.com",
            "auction_params": {
                "component_seller": "test_seller.com"
            }
        },
        .....
    ]
 }
```

```bash
# Setup arguments.
INPUT_PATH=select_ad_request.json  # Needs to be a valid plaintext in the root of the B&A project (i.e. the path is .../bidding-auction-server/select_ad_request.json)
SFE_HOST_ADDRESS=seller.domain.com  # DNS name of SFE (e.g. dns:///seller.domain.com)
CLIENT_IP=<A valid client IPv4 address>

# Run the tool with desired arguments.
./builders/tools/bazel-debian run //tools/secure_invoke:invoke \
    -- \
    -target_service=sfe \
    -input_file="/src/workspace/${INPUT_PATH}" \
    -host_addr=${SFE_HOST_ADDRESS} \
    -client_ip=${CLIENT_IP}
```

Notes:

-   The input request _must_ be specified in JSON format when sending `SelectAdRequest` to an SFE.
-   The request in the file must be plaintext and should have `raw_protected_audience_input` present
    instead `protected_audience_ciphertext`.
-   The input MUST be _in_ the B&A repository _at the root._
    -   Why:
        -   `./builders/tools/bazel_debian` is a docker image which runs bazel with the dependencies
            needed to build and run `secure_invoke`.
        -   The docker container automatically mounts the B&A repository at the root as an
            accessible volume.
        -   Thus the input file needs to be inside the B&A repository so it is accessible.
-   You need to leave the `/src/workspace/` prefix in front of your input file name.
    -   Why:
        -   This creates an absolute path _inside the docker container_ to your input in the
            container.
        -   `bazel run` changes the working directory of the console to the location of the target
            binary, so paths relative to the console's location before `run` will not work.
        -   The docker container's working directory is `/src/workspace/` and this is where the root
            of the B&A repository is mounted.

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
./builders/tools/bazel-debian run //tools/secure_invoke:invoke \
    -- \
    -target_service=bfe \
    -input_file="/src/workspace/${INPUT_PATH}" \
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
./builders/tools/bazel-debian run //tools/secure_invoke:invoke \
    -- \
    -target_service=bfe \
    -input_file="/src/workspace/${INPUT_PATH}" \
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

### Using Custom Keys

By default, the tool uses the keys found
[here](https://github.com/privacysandbox/data-plane-shared-libraries/blob/f5fda1d87f9dc86a1dc21b92338c460b4ebdc8e6/src/encryption/key_fetcher/fake_key_fetcher_manager.h#L27-L33).

If you want to send a request to servers running keys other than the `TEST_MODE=true` keys, you'll
need to specify the keys.

The example below queries a live public key endpoint, picks the first key, and passes it to
`secure_invoke`:

```bash
# Setup arguments.
INPUT_PATH=...
SFE_HOST_ADDRESS=...
CLIENT_IP=...
LIVE_KEY_ENDPOINT="https://..."

# Setup keys.
LIVE_KEYS=$(curl ${LIVE_KEY_ENDPOINT})
PUBLIC_KEY=$(echo $LIVE_KEYS | jq -r .keys[0].key)
KEY_ID=$(echo $LIVE_KEYS | jq -r .keys[0].id)

# Run the tool with desired arguments.
./builders/tools/bazel-debian run //tools/secure_invoke:invoke \
    -- \
    -target_service=sfe \
    -input_file="/src/workspace/${INPUT_PATH}" \
    -host_addr=${SFE_HOST_ADDRESS} \
    -client_ip=${CLIENT_IP} \
    -public_key=${PUBLIC_KEY} \
    -key_id=${KEY_ID}
```

### Interacting with Local Servers

If you want to send a request to servers running on localhost, make sure to specify the
`DOCKER_NETWORK=host` environment variable. See
[here](https://github.com/privacysandbox/bidding-auction-servers/blob/main/tools/debug/README.md#test-buyer-stack)
for more detail.
