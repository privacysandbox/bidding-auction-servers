# GCP confidential space

## Build docker image

```shell
./production/packaging/build_and_test_all_in_docker --service-path bidding_service --service-path seller_frontend_service --service-path buyer_frontend_service --service-path auction_service --instance local --platform gcp --no-precommit --no-tests --gcp-image-tag <ENV_NAME> --gcp-image-repo ${DOCKER_REPO}  --build-flavor prod
```

-   switch `prod` to `non_prod` for debugging build that turn on all vlog.

-   `${DOCKER_REPO}` is where to store the images such as `us-docker.pkg.dev/project-id/services`

-   `${ENV_NAME}` should match `environment` in terraform deploy.

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
        "8a313cb454295d6cffc14f9dac48e14021660bcd7fd1df27d3d2b451f4577ca55ed4f3b0c91e3d4daa08d838ec861b17"

-   seller_frontend_service

    -   "PCR0":
        "6387a23a788127a9fb13c1027af3024af424c67913422a83c19338d2a9d193a7f712b57080b244d96d8fd7db5b411ed5"

-   bidding_service

    -   "PCR0":
        "9b186b693c01156c8beb7cc504ce429d2ca49afb890c44e06eff2fc14fec4aeffa02a66a0c9e385a466c60fb639cdff0"

-   buyer_frontend_service
    -   "PCR0":
        "a4256f7bf8d1dad8ab91889c83bb183bdbb50ddb8690c75d065437cfb57115ab99968a51f9a987f8e051927deecedb4f"

The PCR0 of `--build-flavor non_prod` should match below.

-   auction_service

    -   "PCR0":
        "b7f80a37082945e8c32a60c37a09e5ac7b86e2d8dd202d4008bcb33cdc968ee37f66f92b26ffb01c1a53064b362907dd"

-   seller_frontend_service

    -   "PCR0":
        "944b8b49d87c79710ab352ee0f0fde64886a1967ffe754494b7104907e93f372400122d2a8ca39cbe36a070f19fa4592"

-   bidding_service

    -   "PCR0":
        "816b20b1d7558fa6d1c820814844a24e3b3d26557353f97ee9f021643beb8c6e30fa4c28e9d6165a344b32cc6e8f0835"

-   buyer_frontend_service
    -   "PCR0":
        "b98b170ccd616a01e360355d33358da03705fdabb848e843cfb2b8914ff56b6fc0ad46a1080f0c3ed70ba1863bca94be"

> Note: The PCR0 hash would be validated by the Key Management Systems operated by Coordinators to
> provision keys as part of server attestation.
