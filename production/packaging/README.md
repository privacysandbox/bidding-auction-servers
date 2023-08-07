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

The PCR0 of `--build-flavor prod` and `--build-flavor non_prod` should match PCR0 in
[Release notes](https://github.com/privacysandbox/bidding-auction-servers/releases)

> Note: The PCR0 hash would be validated by the Key Management Systems operated by Coordinators to
> provision keys as part of server attestation.
