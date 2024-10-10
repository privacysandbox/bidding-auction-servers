# Internal TLS (Load Balancer to VM Instance) Setup

## Overview

GCP Application Load Balancers
[require their backends to terminate TLS connections](https://cloud.google.com/load-balancing/docs/ssl-certificates/encryption-to-the-backends).
The terraform in this directory installs certificates that the Seller Frontend VM envoy and Buyer
Frontend VM grpc server processes will use to terminate the load balancer TLS connection. The
certificates may be self-signed and may be used in production. These are separate from the TLS
certificates required for client to load balancer communication -- see the
[domain](../domain/README.md) setup for more details.

## Usage

Fill out the `backend` block in [terraform.tf](./terraform.tf) and all fields in
[internal_tls_setup.auto.tfvars.json](./internal_tls_setup.auto.tfvars.json).

Then, in the same directory as this README, run:

```bash
terraform init
terraform apply # You will have to type 'yes' if you approve of the plan.
```
