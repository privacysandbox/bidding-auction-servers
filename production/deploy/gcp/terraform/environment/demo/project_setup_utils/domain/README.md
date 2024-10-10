# Domain Setup

## Overview

This directory provides terraform functionality to create domain records and TLS for buyers and
sellers. You only need to ever run this once for every unique domain you wish to use.

When testing, it is useful to bring up both a buyer and seller, so by default we provide DNS and TLS
support for all services. You may modify [domain_setup.tf](./domain_setup.tf) to suit your needs,
although the defaults are strongly recommended for initial project setup.

Specifically, you will provide a domain -- for example, `example.com`. Then, the terraform in this
directory will create DNS records for `sfe.example.com` and `bfe.example.com`, with wildcard TLS
certificates for `*sfe.example.com` and `*bfe.example.com`.

The TLS certificates renew via DNS challenge (for which we also automatically create records).

## Usage

Fill out the `backend` block in [terraform.tf](./terraform.tf) and all fields in
[domain_setup.auto.tfvars.json](./domain_setup.auto.tfvars.json). Note: Instead of using a top level
domain, it is required to add at least one sub-domain to avoid DNS zone and record conflicts in GCP.
For example, if you own `example.com`, we recommend you use `ps-test.example.com`. This offers the
flexibility to re-use a single top level domain for many purposes or environments. For clarity, the
documentation will continue to use `ps-test.example.com`.

You are free to use a top level domain with no subdomain but must resolve all record conflicts
manually, making sure to to include 2 additional `NS` records:

1. One for `bfe.ps-test.example.com`. This must point at the nameservers for `bfe` found via the
   `zone_url` output (see below).
1. One for `sfe.ps-test.example.com`. This must point at the nameservers for `sfe` found via the
   `zone_url` output (see below).

The following script makes a Primary DNS zone for your domain as-specified (say,
`ps-test.example.com`), and a DNS zone for each of `bfe.ps-test.example.com` and
`sfe.ps-test.example.com`; the 'Primary' DNS zone is then populated with NS records to
`bfe.ps-test.example.com` and `sfe.ps-test.example.com`'s nameservers. It is up to you to ensure
that your top level domain records (for `example.com`) have NS records for the Primary DNS zone
(`ps-test.example.com`).

To create the DNS records and TLS certificates, in the same directory as this README, run:

```bash
terraform init
terraform apply # You will have to type 'yes' if you approve of the plan.
```

The terraform for the server setup ([buyer](../../buyer/buyer.tf) and
[seller](../../seller/seller.tf)) may then reference the outputs of this terraform for setup of the
individual services, which will register service-specific subdomains. You will need to save the
outputs (but you can just run `terraform apply` again if you lose them).

Visit the link output by `zone_url` and verify that your Primary zone NS and SOA records conform
with your domain registrar.
