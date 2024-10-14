## Load Testing Guide for Bidding and Auction Services

This is a guide with the recommended steps to perform load testing with Bidding and Auction
Services. This can be used to send requests to SFE at different RPS and analyze performance metrics
for driving infra capacity planning for B&A services or validating the performance of a particular
B&A service infra setup.

## Prerequisites

1.  Cloud Account (GCP/AWS)
1.  Sample SFE requests
1.  Load testing client ([wrk2](https://github.com/giltene/wrk2))

## General instructions

1.  Setup the WRK2 tool as per the instructions below.
1.  Follow the deployment guide to deploy all the four services on
    Cloud([GCP](https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_gcp_guide.md)/[AWS](https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_aws_guide.md)).

Note the following while building and deploying the services:

-   Modify the terraform configuration based on the load(QPS) you are testing with:

    -   **bidding_machine_type, bfe_machine_type, auction_machine_type, sfe_machine_type**: Specify
        the size of the machine for all the services. Note: SFE can use a standard machine while
        BFE, Bidding and Auction Services will need a high memory configuration. Bidding and Auction
        are usually recommended to be more compute optimized.
    -   **Min_replicas_per_service_region**
    -   **Max_replicas_per_service_region**
    -   **UDF_NUM_WORKERS** : Specify the number Roma workers (equal to the vCPUs in the machine)
    -   **JS_WORKER_QUEUE_LEN**: Specify the Roma queue length.

        The recommended configurations for scaling will be provided in a different explainer.

-   Build the services with your environment using the prod build:

    ```bash
    --build-flavor prod
    ```

-   Use the
    [secure_invoke tool](https://github.com/privacysandbox/bidding-auction-servers/tree/main/tools/secure_invoke)
    to verify if Ads are being returned.

-   Use WRK2 to run the load tests:

    1.  Generate 2 encrypted payloads.
    1.  Create a lua script and add the path to the payloads. Example:

```lua
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"
wrk.headers["X-User-Agent"] = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/115.0.0.0 Safari/537.36"
wrk.headers["x-accept-language"] = "en-US,en;q=0.9"
wrk.headers["X-BnA-Client-IP"] = "104.133.126.32"


f = assert(io.open("<path/to/payload1>", "rb"))
body_1 = f:read("*all")
body_1 = body_1:gsub("[\n\r]", " ")
f:close()

f = assert(io.open("<path/to/payload2>", "rb"))
body_2 = f:read("*all")
body_2 = body_2:gsub("[\n\r]", " ")
f:close()

requests = {}

init = function()
    requests[1] = wrk.format(nil, nil, nil, body_1)
    requests[2] = wrk.format(nil, nil, nil, body_2)
    end

function request()
    return requests[math.random(#requests)]
end

response = function(status, header, body)
    if status > 200 then
        print("status:" .. status .. "\n" .. body .. "\n-------------------------------------------------\n");
    end
end

```

1.  To run load test with the required number of requests use this command:

```bash
./wrk  -R 300 -t50 -c60 -d5m -s <lua file path> --u_latency  https://seller1-<env>.sfe.bas-gcp.pstest.dev/v1/selectAd
```

1.  Performance metrics can be found on GCP/AWS dashboards:
    1.  On the GCP console, go to Monitoring-> Dashboards and search for the environment the
        services are deployed on to find the buyer and seller metrics.
    1.  Look at the round trip latency using the request.duration_ms metrics.

Note: _Verify the request count and make sure request.failed_count is 0. _ IGNORE the latency
metrics from the wrk2 tool.

## Recommended load testing tool

### WRK2

[Wrk2](https://github.com/giltene/wrk2) is a modern HTTP benchmarking tool written in C language
which can be used to test the performance of an API. wrk2 allows the user to specify a maximum rate,
in requests per second.

To set up WRK2, on your local machine or cloud follow these commands:

```bash
sudo apt-get upgrade

# Install git and pull wrk2.
sudo apt-get install git
git clone https://github.com/giltene/wrk2.git

# Install your dependencies.
sudo apt-get install make
sudo apt-get install gcc
# You already have these but to build wrk2 you need the dev packages.
sudo apt-get install libssl-dev
sudo apt-get install zlib1g-dev

cd wrk2
make

# Test
./wrk
```

Note:

The tool has an issue with the way the latency is tracked. The start time of all the requests is
considered to be the start time of the 1st batch of requests which results in more latency numbers
over all. So it is recommended to rely on the latency numbers from the monitoring dashboard.

Other options(not recommended):

-   WRK
    -   This is what `wrk2` is based on. Because `wrk` does not support setting a specific maximum
        rate in requests per second, it is difficult to know exactly what load the servers are being
        put under.
-   GHZ
    -   Uses gRPC, which is nice because it allows bypassing the envoy proxy
    -   Also allows asynchronous requests which helps overload the servers effectively
    -   Additionally, for high concurrency, the tool reliably throws errors and refuses to work.
        (above 100 concurrency these errors are common, at 500 concurrency they can be observed
        every time.)
    -   For high QPS, the tool can run into issues if you're sending metadata/headers to the SFE
        service. It batches requests together which can cause it to hit the default metadata size
        limit on the gRPC channel.
