# Demo Buyer and Seller Terraform Configurations for GCP

## Overview

This directory contains examples of the buyer and seller terraform modules.

The operator of a buyer or seller service pair (SellerFrontEnd + Auction and BuyerFrontEnd +
Bidding, henceforth referred to as 'stack') will deploy the services using terraform. The
configuration of the buyer and seller modules has many different fields, so this directory is aimed
at serving as a guide for the operator trying to bring up a fully functioning stack. The seller
stack is meant to communicate with a seller ad service and buyer front ends; the buyer stack is
expected to communicate only with seller front ends.

## Local Setup

Review the public
[GCP cloud support and deployment guide](https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_gcp_guide.md).
This document will be continually updated with all GCP concerns.

### After Following [Initial GCP Setup](https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_gcp_guide.md#guide-package-deploy-and-run-a-service)

1. Follow the steps in `production/packaging/README.md` to generate and upload docker
   (cryptographically attestable) docker images. The tag of the images that you provide should match
   your environment name (see `tee-image-reference` in
   `production/deploy/gcp/terraform/services/autoscaling/main.tf` to understand how the image path
   is derived).

1. Run:

    ```bash
    gcloud auth login
    gcloud beta auth application-default login
    gcloud config set project <Your project id, found in cloud console>
    ```

1. Create two GCP secrets via the
   [Secret Manager](https://cloud.google.com/secret-manager/docs/create-secret-quickstart). These
   should match with the names found in `modules/secrets/secrets.tf`. You may use the following
   examples directly.

    1. `envoy-tls-termination-key`. Example:

    ```bash
    -----BEGIN PRIVATE KEY-----
    MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQC8Xer29Ko4WlI8
    zyGMyUdPgSrcSZn8sreNRpZ3shAwJG96XrDdmPtCaPDPw+YN0nplrYmQ5Sxceitm
    raBUl1PpEgPje/JxfLMtRsk1S7gfKMW24gqsleraV9ZBqin9EeroefmIaDv/otAK
    GjV6Ty/j5rZl49vMMLeRDs2rN9Oi9ukdaoMWNXyPNTpm1yt4b8PB+ZVVNYKHLb2r
    Hf1+Fa1NBLMixtBLd/UquNmlJzSNBoulqbBlmyObbGEwMaxI7KHNbP88YmGhp5KM
    cWo/2PC13fSM71OiuaLUoHRG9JfEWqya9NtmNNhnf1KTQXwA2u5Fe7Wc+4mRhdjP
    BOP26NSTAgMBAAECggEBALrgCg166a0CnnfJnqVHwsFzigwF0QlMXKGCGCEjvL+m
    RhqG+ry92vglmFLnLMMlv1xEcCgZ1IrigVBajKefghXGU6lJ/FrutewDP/bp6f6v
    uocXdjOGf/qiDeQTZ5i0P/Lnn9HeZzfUVMTQ/6EaEo7tAqPPDO5knpkAsLZeqk4P
    JzUaqoZdMRXuq9qmJZl9vyRj1rRqS99+JV6Oody9+SO2a6hKQqH8w7EfLTTcULqp
    ZYHXdx6CnOMudf8F7fqBLvWL0piCeyfGX3JXxmujgu4XUMf6Y/jnV2O97ZGmALYh
    S2N2huP3dloxKUacubILlruqDBtL0s/o9x4MMSyNtqECgYEA5ptbRQXT8svxJ3hB
    DkT0JMTV2NXZ4EfA5RRTMRzr0vgoryIFWq77Gu056kRtM1/ymWSJiPj18lWAraqi
    wv6ywHx8W3Pdp2vrnMsaqnCI8LkrF5iKbJF0GSSrLjckND+WFfwGGbbfqxa21peo
    8dRl2+tIwBzqaa7dtQ7vESNS9RsCgYEA0RvYi8KQXQ+w9Xelcflgs+NKc73jKC6k
    3xc+toGHgfRZK+C6vRbfF1c+Kf1JRXpOFbmpE4RjgPVdWk+Pue/6Gs6J//SxCDFF
    ZqMINB07vGpeh3AP90C6P4zc8YUsSRsw9F/hQgSDLhStWZxFqChbfdBjU5kf5asf
    /z81vjEWLekCgYBufPD12Rz7r4sThiJlW9Q96bEr+wow0zAwkdRqK5kxs4SKpJo8
    IKpe9FpTTAWmH8p0hB8BaYctXJoSmzbwhmfOodZTWuhQVvzEWuujzddOvulOnN91
    tRsTEOaTdgf6oJygW+fwWhZAOtnPZ0qi00kaXVi18yS9DfNb1JPmei49EQKBgQDP
    MpJNWcqGC8hCUf2jg4Cofm0FZoAxDpbbX0MKwCovQJki+xjNyF3h2NaF8K2rpFa+
    /CpmZmXaIEYR+Ifnq7vc2A6xihnojjnAS4cTbGwGdDeaaBXJ318tHTzILDcHcWP+
    oQqoyaPaAy8JfekfiG2vqs7gxPdwMTIRTubHwAfEEQKBgQC3XlPy/r5r7q2O5iwW
    d6UJdh0V+h4zMyenMWRixF1aokNxc9V6GZfK3Lj2tSzjCCHsUkiil8DukB4NVRGW
    KX6inN4S29QarQSdfDW2PvEko0JyfejK5VY2dl8GvGldDhNIt0i9UtXdk6bqcRpy
    lq0bHiQyycFfivMIGsQMP+qVjg==
    -----END PRIVATE KEY-----
    ```

    1. `envoy-tls-termination-cert` must be the corresponding cert. May be self-signed. Example:

    ```bash
    -----BEGIN CERTIFICATE-----
    MIIEaDCCAtCgAwIBAgIRAN1DWyoIZOr/6En495nTwsowDQYJKoZIhvcNAQELBQAw
    gZsxHjAcBgNVBAoTFW1rY2VydCBkZXZlbG9wbWVudCBDQTE4MDYGA1UECwwvZGFu
    a29jb2pAZGFua29jb2ouYy5nb29nbGVycy5jb20gKERhbmllbCBLb2NvaikxPzA9
    BgNVBAMMNm1rY2VydCBkYW5rb2NvakBkYW5rb2Nvai5jLmdvb2dsZXJzLmNvbSAo
    RGFuaWVsIEtvY29qKTAeFw0yMjExMjIyMTMxMDhaFw0yNTAyMjIyMTMxMDhaMGMx
    JzAlBgNVBAoTHm1rY2VydCBkZXZlbG9wbWVudCBjZXJ0aWZpY2F0ZTE4MDYGA1UE
    CwwvZGFua29jb2pAZGFua29jb2ouYy5nb29nbGVycy5jb20gKERhbmllbCBLb2Nv
    aikwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC8Xer29Ko4WlI8zyGM
    yUdPgSrcSZn8sreNRpZ3shAwJG96XrDdmPtCaPDPw+YN0nplrYmQ5SxceitmraBU
    l1PpEgPje/JxfLMtRsk1S7gfKMW24gqsleraV9ZBqin9EeroefmIaDv/otAKGjV6
    Ty/j5rZl49vMMLeRDs2rN9Oi9ukdaoMWNXyPNTpm1yt4b8PB+ZVVNYKHLb2rHf1+
    Fa1NBLMixtBLd/UquNmlJzSNBoulqbBlmyObbGEwMaxI7KHNbP88YmGhp5KMcWo/
    2PC13fSM71OiuaLUoHRG9JfEWqya9NtmNNhnf1KTQXwA2u5Fe7Wc+4mRhdjPBOP2
    6NSTAgMBAAGjXjBcMA4GA1UdDwEB/wQEAwIFoDATBgNVHSUEDDAKBggrBgEFBQcD
    ATAfBgNVHSMEGDAWgBQkq9fd2gnVl5uTLLvccD36dkSHGDAUBgNVHREEDTALggls
    b2NhbGhvc3QwDQYJKoZIhvcNAQELBQADggGBAJxVLCW415Lq/29QRWfidcBtGfGF
    Egf9s9j/M9YLknpRGe4OTMWMES0MFnOyxmLHKdBAxXhV0tDtmSN3TZXNtI/f0A7D
    dUJAoAJsiEwkkBIyh6Q4xLe1MR8XUVQi18DDz74VZa6ZMkffWhhoKhLA8LG35Agr
    wnWQFeBw5giO9JWTnAC5jiqtz+wMD+avspewdZlvBF0M6cmsRpVX1gkTi4Rod06O
    wMI6FHlR9P7zIEYzIIbN0129/bR1pVjOZSX+PISKhTPDYU/AvFX/L7s4Zzb9RhD2
    kSx2XwQRXQIeL7jE7uCriM6nlaiyWi86c8EhzDdvpLUxhgmgK7V2Oq7CUly6recx
    Vy1bllpso+ZrW5h0bifMRI9ShPZkBdYOfr6GPtxPHBJieTMzaQWtp4G34/e4/71+
    QJu4D31p6AhQJitOaNwng0U31E+HLJNh/hb4YEO2R6FxXnqd05AU1kGp7u6Me76N
    vhZzX/nWZUgSmC+c7FqxyP1rjnosG1NEpR7HAQ==
    -----END CERTIFICATE-----

    ```

## Configuration

Each stack has two major configuration components.

### Server Binary Runtime Flags

Numerous flags are consumed by the service binaries. The flags are specified via the terraform
`runtime_flags` map variable (1 per stack). They are stored in the cloud secret manager and are
fetched by the services on startup. Because each service consumes many unique flags, there are two
sources to check in order to gain a full understanding of each flag:

1. In the codebase, please familiarize yourself with `services/<service_name>/runtime_flags.h` (such
   as `services/auction_service/runtime_flags.h`). These header files serve as the ultimate source
   of truth for the flags unique to the service. For descriptions of each flag, you can search their
   corresponding `ABSL_FLAG` definition (typically the name of the flag but all snakecase). For
   usage of each flag, consider searching the codebase for the flag name in all uppercase.
1. For flags common to all services, please inspect
   `services/common/constants/common_service_flags.h`. For learning more about these flags and how
   these integrate with the codebase, you can use the same principles as from step 1. For examples,
   please refer to `./buyer/buyer.tf` and `./seller/seller.tf`.

### GCP Architecture Flags

Running a stack in GCP requires a large number of parameters to be specified by the operator. These
parameters are all of the variables specified outside of the `runtime_flags` fields. For
descriptions, please refer to `../modules/buyer/service_vars.tf` and
`../modules/seller/service_vars.tf`. For examples, please refer to `./buyer/buyer.tf` and
`./seller/seller.tf`.

## Using the Demo Configuration

1.  Create a sibling directory to `demo` (the directory hosting this file). It can be called
    anything, although naming it after your environment may be convenient. Example: `my_env`
1.  Copy either ./buyer or ./seller to your directory. Example:

```bash
         |-- environment
         |   |-- demo
         |   |   |-- buyer
         |   |   `-- seller
         |   `-- my_env
         |       `-- seller
```

1.  Set the copied buyer or seller directory as your new working directory.
1.  Modify all of the variables in buyer.tf or seller.tf.
1.  `terraform init && terraform apply` from within the buyer or seller directory.
1.  If everything was configured properly, the stack should be created on GCP.
