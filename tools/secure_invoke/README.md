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

A sample request is as follows:

```json
{
    "auction_config": {
        "auction_signals": "{}",
        "buyer_list": ["Placeholder-Should-Match-With-The-Keys-In-BUYER_SERVER_HOSTS-In-SFE"],
        "buyer_timeout_ms": 100000,
        "per_buyer_config": {
            "Placeholder-Should-Match-With-The-Keys-In-BUYER_SERVER_HOSTS-In-SFE": {
                "buyer_debug_id": "Buyer-Debug-ID-Useful-For-Debugging",
                "buyer_signals": "{}"
            }
        },
        "seller": "Placeholder-Should-Match-With-seller_origin_domain-In-SFE-Config",
        "seller_debug_id": "Seller-Debug-ID-Useful-For-Debugging",
        "seller_signals": "[]"
    },
    "client_type": "CLIENT_TYPE_BROWSER",
    "raw_protected_audience_input": {
        "generation_id": "A-UUID",
        "publisher_name": "example.com",
        "raw_buyer_input": {
            "Placeholder-Should-Match-With-Keys-In-BUYER_SERVER_HOSTS-In-SFE": {
                "interest_groups": [
                    {
                        "ad_render_ids": [
                            "test_id-That-Will-Be-Used-To-Generated-Ad-Render-Id-In-GenerateBid"
                        ],
                        "bidding_signals_keys": [
                            "Sample-key-To-Be-Used-To-Lookup-Real-Time-Signals-From-Buyer-KV"
                        ],
                        "browser_signals": {
                            "bid_count": "1",
                            "join_count": "2",
                            "prev_wins": "[]"
                        },
                        "name": "Test-Interest-Group-Name",
                        "user_bidding_signals": "[]"
                    }
                ]
            }
        }
    }
}
```

Notes:

-   Hosts in `buyer_list` must be a subset of keys in `BUYER_SERVER_HOST`
    [map](https://github.com/privacysandbox/bidding-auction-servers/blob/290329503f5f5b57acb3ddc3b0fe79502cd7da05/production/deploy/gcp/terraform/environment/demo/seller/seller.tf#L66)
    configured in SFE terraform config (when running on cloud) OR key in `buyer_server_hosts`
    [map](https://github.com/privacysandbox/bidding-auction-servers/blob/290329503f5f5b57acb3ddc3b0fe79502cd7da05/tools/debug/start_sfe#L31)
    (when running locally).
-   The key in `per_buyer_config` and `raw_buyer_input` should also match with the buyer domain name
    as mentioned in the last point (about `buyer_list`)
-   The `seller` field must match with
    [`SELLER_ORIGIN_DOMAIN`](https://github.com/privacysandbox/bidding-auction-servers/blob/290329503f5f5b57acb3ddc3b0fe79502cd7da05/production/deploy/gcp/terraform/environment/demo/seller/seller.tf#L64)
    (when running on cloud) OR
    [`seller_origin_domain`](https://github.com/privacysandbox/bidding-auction-servers/blob/290329503f5f5b57acb3ddc3b0fe79502cd7da05/tools/debug/start_sfe#L29)
    (when running locally).
-   When sending requests to a locally running service, a `-insecure` param also needs to be added
    to the secure_invoke CLI.

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
(For local services, use: SFE_HOST_ADDRESS=localhost:50053)
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
INPUT_PATH=/tmp/get_bids_request.json  # Needs to be a valid GetBidsRawRequest
INPUT_FORMAT=PROTO
BFE_HOST_ADDRESS=buyer.domain.com  # DNS name of BFE service (Example: dns:///buyer.domain.com)
(For local runs services, use: BFE_HOST_ADDRESS=localhost:50051)
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

#### Sending ProtectedAudience [GetBidsRawRequest] to BFE as JSON

Following example shows how JSON based plaintext `GetBidsRawRequest` payload can be used to send
gRPC request to BFE:

```bash
# Setup arguments.
INPUT_PATH=/tmp/get_bids_request.json  # Needs to be a valid GetBidsRawRequest
INPUT_FORMAT=JSON
BFE_HOST_ADDRESS=buyer.domain.com  # DNS name of BFE service (Example: dns:///buyer.domain.com)
(For local runs services, use: BFE_HOST_ADDRESS=localhost:50051)
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
-   When sending requests to a locally running service, a `-insecure` param also needs to be added
    to the secure_invoke CLI.

A sample request is as follows:

```json
{
    "client_type": "CLIENT_TYPE_BROWSER",
    "buyer_input": {
        "interest_groups": [
            {
                "name": "Test-Interest-Group-Name",
                "bidding_signals_keys": "Sample-key-To-Be-Used-To-Lookup-Real-Time-Signals-From-Buyer-KV",
                "ad_render_ids": [
                    "Sample-key-To-Be-Used-To-Lookup-Real-Time-Signals-From-Buyer-KV"
                ],
                "user_bidding_signals": "[]",
                "browser_signals": {
                    "join_count": "1",
                    "bid_count": "2",
                    "prev_wins": "[]"
                }
            }
        ]
    },
    "auction_signals": "{}",
    "buyer_signals": "{}",
    "seller": "Placeholder-Should-Match-With-seller_origin_domain-In-SFE-Config",
    "publisher_name": "example.com",
    "log_context": {
        "generation_id": "A-UUID",
        "adtech_debug_id": "A-Debug-ID-Useful-For-Debugging"
    },
    "consented_debug_config": {
        "is_consented": true,
        "token": "123456"
    }
}
```

#### Sending ProtectedAppSignals [GetBidsRawRequest] to BFE as JSON

Following example shows how json based plaintext `GetBidsRawRequest` payload can be used to send
gRPC request to BFE:

```bash
# Setup arguments.
INPUT_PATH=/tmp/get_bids_request.proto# Needs to be a valid GetBidsRawRequest
INPUT_FORMAT=JSON
BFE_HOST_ADDRESS=buyer.domain.com  # DNS name of BFE service (Example: dns:///buyer.domain.com)
(For local runs services, use: BFE_HOST_ADDRESS=localhost:50051)
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

A sample request is as follows:

```json
{
    "client_type": "CLIENT_TYPE_ANDROID",
    "protected_app_signals_buyer_input": {
        "protected_app_signals": {
            "app_install_signals": "/w=="
        }
    },
    "auction_signals": "{}",
    "buyer_signals": "{}",
    "seller": "Placeholder-Should-Match-With-seller_origin_domain-In-SFE-Config",
    "publisher_name": "example.com",
    "log_context": {
        "generation_id": "A-UUID",
        "adtech_debug_id": "A-Debug-ID-Useful-For-Debugging"
    },
    "consented_debug_config": {
        "is_consented": true,
        "token": "123456"
    }
}
```

#### Sending ProtectedAppSignals [GetBidsRawRequest] to BFE as PROTO

Following example shows how proto based plaintext `GetBidsRawRequest` payload can be used to send
gRPC request to BFE:

```bash
# Setup arguments.
INPUT_PATH=/tmp/get_bids_request.proto# Needs to be a valid GetBidsRawRequest
INPUT_FORMAT=PROTO
BFE_HOST_ADDRESS=buyer.domain.com  # DNS name of BFE service (Example: dns:///buyer.domain.com)
(For local runs services, use: BFE_HOST_ADDRESS=localhost:50051)
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

A sample request is as follows:

```proto
buyer_input { }
auction_signals: "{}"
seller: "Placeholder-Should-Match-With-seller_origin_domain-In-SFE-Config"
publisher_name: "example.com"
log_context { generation_id: "A-UUID" adtech_debug_id: "A-Debug-ID-Useful-For-Debugging" }
protected_app_signals_buyer_input { protected_app_signals { app_install_signals: "some-binary-string" } }
client_type: CLIENT_TYPE_ANDROID
consented_debug_config: { is_consented: true token: "123456" }
```

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
# Setup arguments (and replace the placeholders).
INPUT_PATH="<Input request path>"
SFE_HOST_ADDRESS="<DNS of SFE>"
CLIENT_IP="<client-ip>"
LIVE_KEY_ENDPOINT="<URL from which public keys can be fetched>"

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

Notes about `LIVE_KEY_ENDPOINT`:

-   `LIVE_KEY_ENDPOINT` refers to the public key hosting service endpoint from where public keys are
    fetched.
-   For AWS, use the following:
    LIVE_KEY_ENDPOINT="<https://publickeyservice.pa.aws.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys>"
-   For GCP, use the following:
    LIVE_KEY_ENDPOINT="<https://publickeyservice.pa.gcp.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys>"
-   We will add/update public key endpoints as support for other cloud platforms is added.

### Interacting with Local Servers

If you want to send a request to servers running on localhost, make sure to specify the
`DOCKER_NETWORK=host` environment variable. See
[here](https://github.com/privacysandbox/bidding-auction-servers/blob/main/tools/debug/README.md#test-buyer-stack)
for more detail.

Also, note when sending requests to services running on localhost, `-insecure` flag needs to be
added to the commands.

Finally, when sending requests to local services, use `localhost:50053` and `localhost:50051` for
SFE and BFE host addresses (i.e. `-host_addr` param) respectively.
