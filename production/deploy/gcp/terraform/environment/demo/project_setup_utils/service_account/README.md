# Service Account Setup

## Overview

A service account is attached to each running Bidding and Auction service's Confidential Compute
instance and require permissions for all of the actions the service will perform.

This directory provides terraform functionality to create a service account and configure all
project permissions the service account requires to operate the Bidding and Auction servers.

Additionally, we create a
[GCS HMAC Key](https://github.com/privacysandbox/bidding-auction-servers/blob/722e1542c262dddc3aaf41be7b6c159a38cefd0a/production/deploy/gcp/terraform/modules/secrets/secrets.tf#L49)
for usage with consented debugging. This key is tied to the service account.

## Usage

Fill out the `backend` block in [terraform.tf](./terraform.tf) and all fields in
[service_account_setup.auto.tfvars.json](./service_account_setup.auto.tfvars.json).

Then, in the same directory as this README, run:

```bash
terraform init
terraform apply # You will have to type 'yes' if you approve of the plan.
```

The terraform for the server setup ([buyer](../../buyer/buyer.tf) and
[seller](../../seller/seller.tf)) may then reference the outputs of this terraform for setup of the
individual services. You will need to save the outputs (but you can just run `terraform apply` again
if you lose them).
