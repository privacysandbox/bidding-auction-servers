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
        "e1bb35fab8e5c699793ba514b275632ff7deee53b33f1972a5271b40a9cb7dd9c546603b371065066688bfa2a44a7e10"

-   seller_frontend_service

    -   "PCR0":
        "9fec8c6cdca16a01bce7d042035a53c25fce0168253baa989fa0039be17a229b84258eea8aaad2a0aca2694664c0f1f4"

-   bidding_service

    -   "PCR0":
        "97b878136116766c1c45b160c99c847466509a7e8bf81f5cfa45ac9acfc327531df3b4ee056fd5017c6356a2ad110542"

-   buyer_frontend_service
    -   "PCR0":
        "40bb23cacdc09b0baa25c30111a536790e8f5a7284dbfa852e4843c49f4bdd0576f6c1f572b7523b35e934220a22199c"

The PCR0 of `--build-flavor non_prod` should match below.

-   auction_service

    -   "PCR0":
        "413d8cf9863943b42d354bf510857d65b43f10e693ffe4aee25fe666fcd0b759cae9ad08c7182a2ed9183e5d985c0a3a"

-   seller_frontend_service

    -   "PCR0":
        "3bf2849af6e52a451129f76fb68b5bcc5d2b653ac306f4839ae0fc97f11ee10db495587efaa1584fffe27ed4e98e38b5"

-   bidding_service

    -   "PCR0":
        "36cfa852bdd8c93c2b0a69e50db7b746e4bb13bfebd1947c74432d621f2f6b5ce7b249f1104c61efc69e7590c30d63f2"

-   buyer_frontend_service
    -   "PCR0":
        "e0a59f724be3253280b3d179786f5e52ba9fc6c85d65f4ea48beadfb9a33bca1fe887294bb1287bb75b3cdadcee0c45d"

> Note: The PCR0 hash would be validated by the Key Management Systems operated by Coordinators to
> provision keys as part of server attestation.
