# GCP Cloud Build for Bidding and Auction Services

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
the future. The images built by Cloud Build replace the
[build and packaging](https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_gcp_guide.md#step-1-packaging)
steps, but also require you to update the `bidding_image`, `auction_image`, `buyer_frontend_image`,
and `seller_frontend_image` fields in your terraform to point to an image built by this process.

## Cloud Build Configuration

### Connecting to Github

First, follow the steps to
[connect a Github repository](https://cloud.google.com/build/docs/automating-builds/github/connect-repo-github?generation=2nd-gen)
and create a host connection. You will need to clone the
[Bidding and Auction repo](https://github.com/privacysandbox/bidding-auction-servers) to your own
Github account before you can connect it to your GCP project's Cloud Build. Make sure that your
fork, if updated automatically, also fetches the tags from the upstream repo -- that way, you can
build directly from the semantically versioned tags. See [here](sync_bidding_auction_repo.yaml) for
an example
[Github Action](https://docs.github.com/en/actions/writing-workflows/quickstart#creating-your-first-workflow)
that handles syncing.

Note: [Optional] you can express this step in
[terraform](https://registry.terraform.io/providers/hashicorp/google/latest/docs/resources/cloudbuild_trigger#example-usage---cloudbuild-trigger-repo)
by modifing [cloud_build.tf](./cloud_build.tf) to take as input the necessary information to create
a parent connection and your github repo.

### Terraform

Fill out the `backend` block in [terraform.tf](./terraform.tf) and all fields in
[cloud_build.auto.tfvars.json](./cloud_build.auto.tfvars.json).

Then, in the same directory as this README, run:

```bash
terraform init
terraform apply # You will have to type 'yes' if you approve of the plan.
```

This will create triggers that automatically build `prod` and `non_prod` images of the Bidding and
Auction Services every time a new release (tag) is pushed to the
[connected repo](#connecting-to-github). The images will be pushed to an Artifact Registry of your
choice, using the default compute service account.
