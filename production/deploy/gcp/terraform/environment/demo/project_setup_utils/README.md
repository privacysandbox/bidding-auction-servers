# Overview

This directory contains various subdirectories that provide project setup functionality. See the
README.md in each subdirectory for more details.

If you are setting up Bidding and Auction Services for the first time, it is strongly recommended
that you perform the actions described by the README.md in each subdirectory.

## Pre-Requisites

You will first need to create a GCP project and set up billing. You should also purchase a domain
for testing.

Then, you will need to install `glcoud` on the machine from which you plan to run terraform setup
and you will need to authenticate with `gcloud auth login`.

Finally, set your default project with:
`gcloud config set project <Your project id, found in cloud console>`

## Terraform Deploy Permissions

For initial setup, it is recommended that your account (or any account attempting to deploy the
resources specified by the terraform) has
[`roles/owner`](https://cloud.google.com/iam/docs/roles-overview) in your GCP project.

<!-- markdownlint-disable -->
<details>
<summary>Fine-grain permissions list (if not using roles/owner).</summary>

```bash
appengine.applications.get
appengine.instances.enableDebug
artifactregistry.files.download
artifactregistry.files.list
artifactregistry.locations.list
artifactregistry.packages.delete
artifactregistry.packages.list
artifactregistry.projectsettings.get
artifactregistry.projectsettings.update
artifactregistry.repositories.create
artifactregistry.repositories.delete
artifactregistry.repositories.get
artifactregistry.repositories.list
artifactregistry.repositories.listEffectiveTags
artifactregistry.repositories.update
artifactregistry.repositories.uploadArtifacts
artifactregistry.tags.create
artifactregistry.tags.delete
artifactregistry.tags.list
artifactregistry.tags.update
artifactregistry.versions.delete
artifactregistry.versions.get
artifactregistry.versions.list
backupdr.backupPlanAssociations.list
backupdr.operations.list
billing.resourceCosts.get
cloudasset.assets.searchAllResources
cloudbuild.builds.editor
cloudnotifications.activities.list
cloudprivatecatalogproducer.products.create
commerceorggovernance.services.get
commerceorggovernance.services.list
commerceorggovernance.services.request
compute.acceleratorTypes.list
compute.addresses.list
compute.autoscalers.create
compute.autoscalers.delete
compute.autoscalers.get
compute.autoscalers.list
compute.backendServices.create
compute.backendServices.delete
compute.backendServices.get
compute.backendServices.list
compute.backendServices.use
compute.disks.create
compute.disks.get
compute.disks.list
compute.diskTypes.list
compute.firewalls.create
compute.firewalls.delete
compute.firewalls.get
compute.firewalls.list
compute.globalAddresses.create
compute.globalAddresses.delete
compute.globalAddresses.get
compute.globalAddresses.use
compute.globalForwardingRules.create
compute.globalForwardingRules.delete
compute.globalForwardingRules.get
compute.globalForwardingRules.setLabels
compute.globalOperations.get
compute.healthChecks.create
compute.healthChecks.delete
compute.healthChecks.get
compute.healthChecks.use
compute.healthChecks.useReadOnly
compute.instanceGroupManagers.create
compute.instanceGroupManagers.delete
compute.instanceGroupManagers.get
compute.instanceGroupManagers.list
compute.instanceGroupManagers.use
compute.instanceGroups.create
compute.instanceGroups.delete
compute.instanceGroups.list
compute.instanceGroups.use
compute.instances.addAccessConfig
compute.instances.attachDisk
compute.instances.create
compute.instances.delete
compute.instances.deleteAccessConfig
compute.instances.detachDisk
compute.instances.get
compute.instances.getSerialPortOutput
compute.instances.list
compute.instances.listEffectiveTags
compute.instances.listReferrers
compute.instances.osLogin
compute.instances.reset
compute.instances.resume
compute.instances.setDeletionProtection
compute.instances.setDiskAutoDelete
compute.instances.setLabels
compute.instances.setMachineResources
compute.instances.setMachineType
compute.instances.setMetadata
compute.instances.setMinCpuPlatform
compute.instances.setScheduling
compute.instances.setServiceAccount
compute.instances.setTags
compute.instances.start
compute.instances.stop
compute.instances.suspend
compute.instances.updateAccessConfig
compute.instances.updateDisplayDevice
compute.instances.updateNetworkInterface
compute.instances.updateShieldedInstanceConfig
compute.instanceTemplates.create
compute.instanceTemplates.delete
compute.instanceTemplates.get
compute.instanceTemplates.useReadOnly
compute.machineImages.create
compute.machineTypes.get
compute.machineTypes.list
compute.networks.create
compute.networks.delete
compute.networks.get
compute.networks.list
compute.networks.updatePolicy
compute.projects.get
compute.projects.setCommonInstanceMetadata
compute.regionOperations.get
compute.regions.list
compute.resourcePolicies.create
compute.resourcePolicies.list
compute.routers.create
compute.routers.delete
compute.routers.get
compute.routers.update
compute.sslCertificates.create
compute.sslCertificates.delete
compute.sslCertificates.get
compute.sslCertificates.list
compute.subnetworks.create
compute.subnetworks.delete
compute.subnetworks.get
compute.subnetworks.list
compute.subnetworks.use
compute.targetHttpsProxies.create
compute.targetHttpsProxies.delete
compute.targetHttpsProxies.get
compute.targetHttpsProxies.list
compute.targetHttpsProxies.use
compute.targetPools.list
compute.targetSslProxies.list
compute.targetTcpProxies.create
compute.targetTcpProxies.delete
compute.targetTcpProxies.get
compute.targetTcpProxies.use
compute.urlMaps.create
compute.urlMaps.delete
compute.urlMaps.get
compute.urlMaps.use
compute.zones.list
consumerprocurement.entitlements.list
container.clusters.list
container.deployments.create
containeranalysis.occurrences.list
dns.changes.create
dns.changes.get
dns.resourceRecordSets.create
dns.resourceRecordSets.delete
dns.resourceRecordSets.list
errorreporting.groups.list
iam.serviceAccounts.actAs
iam.serviceAccounts.list
iap.tunnelInstances.accessViaIAP
logging.buckets.list
logging.buckets.update
logging.logEntries.download
logging.logEntries.list
logging.logServiceIndexes.list
logging.logServices.list
logging.privateLogEntries.list
logging.queries.deleteShared
logging.queries.listShared
logging.queries.share
logging.queries.updateShared
logging.queries.usePrivate
logging.settings.get
logging.views.list
monitoring.alertPolicies.create
monitoring.alertPolicies.list
monitoring.alertPolicies.update
monitoring.dashboards.create
monitoring.dashboards.delete
monitoring.dashboards.get
monitoring.dashboards.list
monitoring.dashboards.update
monitoring.groups.list
monitoring.metricDescriptors.get
monitoring.metricDescriptors.list
monitoring.monitoredResourceDescriptors.list
monitoring.timeSeries.list
monitoring.uptimeCheckConfigs.create
monitoring.uptimeCheckConfigs.list
networkservices.grpcRoutes.create
networkservices.grpcRoutes.delete
networkservices.grpcRoutes.get
networkservices.meshes.create
networkservices.meshes.delete
networkservices.meshes.get
networkservices.meshes.use
networkservices.operations.get
observability.scopes.get
opsconfigmonitoring.resourceMetadata.list
orgpolicy.policy.get
osconfig.inventories.get
osconfig.osPolicyAssignments.create
osconfig.osPolicyAssignments.get
osconfig.vulnerabilityReports.get
pubsub.subscriptions.consume
pubsub.subscriptions.delete
pubsub.subscriptions.get
pubsub.subscriptions.getIamPolicy
pubsub.subscriptions.list
pubsub.subscriptions.setIamPolicy
pubsub.subscriptions.update
recommender.computeInstanceGroupManagerMachineTypeRecommendations.list
recommender.computeInstanceIdleResourceRecommendations.list
recommender.computeInstanceMachineTypeRecommendations.list
recommender.iamPolicyInsights.list
recommender.iamPolicyRecommendations.list
resourcemanager.projects.get
resourcemanager.projects.getIamPolicy
resourcemanager.projects.setIamPolicy
resourcemanager.projects.update
run.services.create
secretmanager.secrets.create
secretmanager.secrets.delete
secretmanager.secrets.get
secretmanager.versions.access
secretmanager.versions.add
secretmanager.versions.destroy
secretmanager.versions.enable
secretmanager.versions.get
securitycenter.userinterfacemetadata.get
serviceusage.services.disable
serviceusage.services.enable
serviceusage.services.get
serviceusage.services.list
stackdriver.projects.get
stackdriver.resourceMetadata.list
storage.buckets.create
storage.buckets.getObjectInsights
storage.buckets.list
storage.buckets.listEffectiveTags
storageinsights.reportConfigs.create
storagetransfer.jobs.create
```

</details>
<!-- markdownlint-enable -->

## Project Setup

1. See the [Project APIs README](./api/README.md). **You must enable the APIs before attempting to
   run any other terraform.**

1. Next, configure your [domain](./domain/README.md).

1. Configure [internal TLS](./internal_tls/README.md).

1. Configure [service accounts](./service_account/README.md).

1. **OPTIONAL but recommended:** Configure
   [Cloud Build](../../../../../../packaging/gcp/cloud_build/README.md) to automatically build the
   latest releases.
