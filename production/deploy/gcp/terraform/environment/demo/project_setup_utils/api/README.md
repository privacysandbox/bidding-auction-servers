# Project API Setup

## Overview

This directory provides terraform functionality to enable the APIs used by the Bidding and Auction
Services.

You only need to run the terraform in this directory once per GCP project.

## Usage

Fill out the `backend` block in [terraform.tf](./terraform.tf) and all fields in
[enable_apis.auto.tfvars.json](./api_setup.auto.tfvars.json).

Then, in the same directory as this README, run:

```bash
terraform init
terraform apply # You will have to type 'yes' if you approve of the plan.
```
