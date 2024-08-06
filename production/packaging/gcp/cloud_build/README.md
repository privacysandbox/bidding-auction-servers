# GCP Code Build for Bidding and Auction Services

## Overview

This README contains instructions on how to setup [GCP Cloud Build](https://cloud.google.com/build)
to build the Bidding and Auction service Docker Images for use in
[Confidential Spaces](https://cloud.google.com/docs/security/confidential-space). These images can
be directly used for the
[deployment process](https://github.com/privacysandbox/protected-auction-services-docs/blob/main/bidding_auction_services_gcp_guide.md#guide-package-deploy-and-run-a-service).

### Why do this?

The Bidding and Auction services can take around 2 hours (with 32 cores) to build. If you create an
automated build pipeline that builds new Bidding and Auction service releases, you can avoid manual
labor and increase operational efficiency. Binaries and docker images will be provided directly in
the future.

## Cloud Build Configuration

### Prerequisites

#### Connecting to Github

First, follow the steps to
[connect a Github repository](https://cloud.google.com/build/docs/automating-builds/github/connect-repo-github?generation=2nd-gen)
and create a host connection. You will need to clone the
[Bidding and Auction repo](https://github.com/privacysandbox/bidding-auction-servers) to your own
Github account before you can connect it to your GCP project's Cloud Build. Make sure that your
fork, if updated automatically, also fetches the tags from the upstream repo -- that way, you can
build directly from the semantically versioned tags. See [here](sync_bidding_auction_repo.yaml) for
an example Github Action that handles syncing.

#### Configuring an Image Repo

Please create an [Artifact Registry](https://cloud.google.com/artifact-registry) repo to hold all of
the Bidding and Auction service images that will be created. Four directories will be created in the
repo (`auction_serivce`, `bidding_service`, `seller_frontend_service`, and
`buyer_frontend_service`). We suggest a default repo name of
`us-docker.pkg.dev/${PROJECT_ID}/services`.

#### Service Account Permissions

Navigate to the Cloud Build page in the GCP GUI and click on Settings. Make sure the service account
permissions have 'Service Account User' enabled. Then, in IAM, additionally make sure that the
service account has Artifact Registry Writer permissions. The build script will attempt to push
images to the image repo specified using the service account for permissions.

### Create a Trigger

#### Source

You must create a build trigger. Starting with a
[manual](https://cloud.google.com/build/docs/triggers#manual) or
[Github](https://cloud.google.com/build/docs/triggers#github) trigger is recommended. Please make
sure to use a '2nd gen' repository source type.

Recommendation 1: Use a `Push a new tag` Event to build `.*` tags.

Recommendation 2: Create a separate trigger for each `_BUILD_FLAVOR` (see below).

#### Configuration

1. Type: Cloud Build configuration file (yaml or json)
1. Location: Repository

    ```plaintext
    production/packaging/gcp/cloud_build/cloudbuild.yaml
    ```

1. Substitution Variables

    Note: these will override variables in the cloudbuild.yaml.

    ```plaintext
     key: _BUILD_FLAVOR
     value: prod or non_prod. While 'prod' allows for attestation against production private keys, non_prod has enhanced logging.

     key: _GCP_IMAGE_REPO
     value: service images repo URI from prerequisites (default: us-docker.pkg.dev/${PROJECT_ID}/services)

     key: _GCP_IMAGE_TAG
     value: any tag (default: ${GIT_TAG}, only useful if building from a tag directly)
    ```

1. Service account: Use the account created [previously](#service-account-permissions).

After configuring your Trigger, click Save. You may manually run it from the Triggers page.
