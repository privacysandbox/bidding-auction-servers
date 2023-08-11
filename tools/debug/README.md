# Running servers locally

The scripts in this folder can be used to run B&A servers in a local system. This can be used for
sanity testing B&A locally or for debugging B&A code in test_mode.

The scripts use the local binaries generated with a local bazel build to start the servers on local
ports without HTTPS. The logs will be output to stderr/stdout.

## Usage

### Start Buyer stack

```bash
# Build the servers:

bazel build //services/bidding_service:server --//:instance=local --//:platform=local
bazel build //services/buyer_frontend_service:server --//:instance=local --//:platform=local

# Edit the run time flags in the scripts:
# Eg. change the biddingJsUrl in tools/debug/start_bidding for custom generateBid script.
# Eg. change the buyer_kv_server_addr in tools/debug/start_bfe for custom KV server.
# Eg. change the GLOG_v value to increase/decrease log level.

# Open two new terminals at B&A project root.
# Start the Bidding server in terminal 1:
./tools/debug/start_bidding
# You should see some logs in each server as it displays HTTP metrics for the first call to the generateBid JS endpoint and some errors for OTEL collectors not found.

# Start the BuyerFrontEnd server in terminal 2:
./tools/debug/start_bfe
# You should see some logs in each server as it displays HTTP metrics for the first call to the KV server and some errors for OTEL collectors not found.
```

### Test Local Buyer stack

You can use the [secure_invoke] tool for sending a request to the buyer stack. Example -

```bash
# Setup arguments.
INPUT_PATH=/tmp/get_bids_request.txt  # Needs to be a valid GetBidsRawRequest
INPUT_FORMAT=PROTO
CLIENT_IP=<A valid client IPv4 address>

# Run the tool with desired arguments.
./builders/tools/bazel-debian run //tools/secure_invoke:invoke \
    -- \
    -target_service=bfe \
    -input_file="/src/workspace/${INPUT_PATH}" \
    -input_format=${INPUT_FORMAT} \
    -host_addr="localhost:50051" \
    -client_ip=${CLIENT_IP} \
    -insecure=true
```

### Start Seller stack

```bash
# Build the servers:

bazel build //services/auction_service:server --//:instance=local --//:platform=local
bazel build //services/seller_frontend_service:server --//:instance=local --//:platform=local

# Edit the run time flags in the scripts:
# Eg. change the auctionJsUrl in tools/debug/start_auction for custom scoreAd script.
# Eg. change the key_value_signals_host in tools/debug/start_sfe for custom KV server.
# Eg. change the GLOG_v value to increase/decrease log level.

# Open two new terminals at B&A project root.
# Start the Auction server in terminal 1:
./tools/debug/start_auction
# You should see some logs in each server as it displays HTTP metrics for the first call to the scoreAd JS endpoint and some errors for OTEL collectors not found.

# Start the SellerFrontEnd server in terminal 2:
./tools/debug/start_sfe
# You should see some logs in each server as it displays HTTP metrics for the first call to the KV server and some errors for OTEL collectors not found.
```

### Test Local Seller stack

You can use the [secure_invoke] tool for sending a request to the seller stack. Example -

```bash
# Setup arguments.
INPUT_PATH=select_ad_request.json  # Needs to be a valid plaintext SelectAdRequest in the root of the B&A project (i.e. the path is .../bidding-auction-server/select_ad_request.json)
CLIENT_IP=<A valid client IPv4 address>

# Run the tool with desired arguments.
./builders/tools/bazel-debian run //tools/secure_invoke:invoke \
    -- \
    -target_service=sfe \
    -input_file="/src/workspace/${INPUT_PATH}" \
    -host_addr="localhost:50053" \
    -client_ip=${CLIENT_IP} \
    -insecure=true
```

Notes:

-   Change log level with the GLOG_v environment variable before starting the servers. To display
    all logs, set the level to 10.
-   You can only connect single instances of each server locally without a load balancer.
-   If you need to change the default port numbers for any servers, make sure and update the scripts
    for the server connecting to this updated server as well. For example, if the port for BFE
    changes, the address must also be updated in tools/debug/start_sfe.
-   Start Bidding server before BuyerFrontEnd server, so that BuyerFrontEnd can connect to Bidding
    on startup. Otherwise, requests might fail.
-   Start Auction server before SellerFrontEnd server, so that SellerFrontEnd can connect to Auction
    on startup. Otherwise, requests might fail.
-   Start Buyer stack before seller stack, so that SellerFrontEnd server can connect to buyer stack
    on startup. Otherwise, requests might fail.

[secure_invoke]:
    https://github.com/privacysandbox/bidding-auction-servers/tree/main/tools/secure_invoke
