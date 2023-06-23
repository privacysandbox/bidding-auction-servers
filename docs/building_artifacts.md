# Building artifacts

## Prerequisites

Before starting the build process, install [Docker](https://docs.docker.com/engine/install/) and
[BuildKit](https://docs.docker.com/build/buildkit/). If you run into any Docker access errors,
follow the instructions for
[setting up sudoless Docker](https://docs.docker.com/engine/install/linux-postinstall/#manage-docker-as-a-non-root-user).

## Build local artifacts

From the repo root directory, run the following commands to build artifacts for each service:

```sh
builders/tools/bazel-debian run //production/packaging/aws/auction_service:copy_to_dist
builders/tools/bazel-debian run //production/packaging/aws/bidding_service:copy_to_dist
builders/tools/bazel-debian run //production/packaging/aws/buyer_frontend_service:copy_to_dist
builders/tools/bazel-debian run //production/packaging/aws/seller_frontend_service:copy_to_dist
```

Artifacts should now be located in the `dist` directory of the repo root directory. Service specific
artifacts are located in `dist/debian`.
