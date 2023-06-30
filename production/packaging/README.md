# GCP confidential space

## Build docker image

```shell
./production/packaging/build_and_test_all_in_docker --service-path bidding_service --service-path seller_frontend_service --service-path buyer_frontend_service --service-path auction_service --instance local --platform gcp --no-precommit --no-tests --gcp-image-tag xgprod --gcp-image-repo ${DOCKER_REPO}  --build-flavor prod
```

-   switch `prod` to `non_prod` for debugging build that turn on all vlog.

-   `${DOCKER_REPO}` is where to store the images such as `us-docker.pkg.dev/project-id/services`

After images being build, forward the docker URL(starting with ${DOCKER_REPO}) to Coordinator to
enable the attestation.

> Note: For this Alpha release, Google Privacy Sandbox (Bidding and Auction Server engineers) would
> act as the Coordinators to run the Key Management Systems that provision keys for encryption /
> decryption.

# AWS Nitro Enclaves

## build AMI

```shell
./production/packaging/build_and_test_all_in_docker --service-path auction_service  --service-path  bidding_service  --service-path buyer_frontend_service  --service-path seller_frontend_service --platform aws --instance aws --with-ami us-west-1 --no-precommit --no-tests  --build-flavor prod
```

-   switch `prod` to `non_prod` for debugging build that turn on all vlog.

After AMI being built, the PCR0 will be saved at `dist/aws/${SERVICE_PATH}.json`

-   `${SERVICE_PATH}` is what specified in `--service-path` in build command.

-   Example

```json
{
    "Measurements": {
        "HashAlgorithm": "Sha384 { ... }",
        "PCR0": "20252a7f1852b80363e3bcc0f7a44b99ebcfb8baef99f1f9a872cc18ccdf47773bbd95998cbbc0d45a940d99385a0809",
        "PCR1": "bcdf05fefccaa8e55bf2c8d6dee9e79bbff31e34bf28a99aa19e6b29c37ee80b214a414b7607236edf26fcb78654e63f",
        "PCR2": "a37cb1afc87304d22ad01a2eb2acb8f9a4305c09fad3bd9acf69b2d1add2ac579031ae661e2af22b506a274dee9b2070"
    }
}
```

The PCR0 of `--build-flavor prod` should match below.

-   auction_service

    -   "PCR0":
        "20252a7f1852b80363e3bcc0f7a44b99ebcfb8baef99f1f9a872cc18ccdf47773bbd95998cbbc0d45a940d99385a0809"

-   seller_frontend_service

    -   "PCR0":
        "8eacc7fcb5ebb3b8d15b9cb7b4e67dd6c0b8191f607ae9638ef8d2b95952631d26232f14ad17271c047d5e1e9dd116e6"

-   bidding_service

    -   "PCR0":
        "c21fd30ddef4b1c647daf7e65052d7a01c66e069a86b536e42242b02fb4289f05942bae9f7e77bfb38e74bc44d141b81"

-   buyer_frontend_service
    -   "PCR0":
        "04cd715bf20aab5fe6c77ee22fdb6e1c7a12550b98af7ca836ca9275bdbebb1bff165ab68d7f4903242cbeb3b6f50c64"

The PCR0 of `--build-flavor non_prod` should match below.

-   auction_service

    -   "PCR0":
        "3e99fc3aa82eaeb01ab6b5db4d91880fa733e94637131476d016c5d71c1a1289fe050db71b854c0984ae64aa97ef500e"

-   seller_frontend_service

    -   "PCR0":
        "07671695543cacd86022c004e8e75736eb551290eabf22c087414aef3825955c6782a6d1e7d4bfa73e27fd2c2f163558"

-   bidding_service

    -   "PCR0":
        "ac2aa6058fbf71cdcc3334e168fe9ced1d9f0f3ba8614cef626359efc7e902cc2da17d26f7fe3979dd884b4c936b4900"

-   buyer_frontend_service
    -   "PCR0":
        "aa829850088a376c4ccc4dd02e41399df61d55a8e54eb32ff9342f7f70a1a7f632b1ad7366dceebd14a6dc7c697b0aa5"

> Note: The PCR0 hash would be validated by the Key Management Systems operated by Coordinators to
> provision keys as part of server attestation.
