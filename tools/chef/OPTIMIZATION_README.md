# Chef Solo Optimization Guide

## Overview

The Chef Solo setup has been optimized to significantly reduce runtime by making optional components conditional and adding guards to prevent redundant installations.

## Quick Start - Optimized Configuration

To use the optimized configuration that skips slow operations:

```bash
sudo CHEF_LICENSE="accept" chef-solo -c tools/chef/solo.rb -j tools/chef/node_workstation_optimized.json
```

This configuration:
- **Skips Google Cloud SDK installation** (primary timeout fix)
- **Skips Kubernetes tools** (kubectl, helm, minikube, skaffold, kustomize, opm)
- **Skips PHP and Arcanist**
- **Skips optional tools** (packer, trivy, lego)
- **Includes**: Go, Python, Node.js, docker-buildx, gperftools, and essential dev tools

**Expected runtime reduction**: 50-70% faster, especially on first run

## What Changed

### 1. Google Cloud SDK (MAJOR OPTIMIZATION)
The gcloud installation was the primary cause of timeouts. Changes:
- Only installs if `optional_components.gcloud: true`
- Skips component updates by default (very slow operation)
- Additional components (beta, gke-gcloud-auth-plugin) are opt-in only
- Added existence checks to prevent redundant installations

### 2. Kubernetes Tools Removed
Unless explicitly enabled, these tools are skipped:
- kubectl
- helm
- kustomize
- minikube
- skaffold
- opm

### 3. PHP/Arcanist Made Optional
- PHP packages and Arcanist are only installed if `optional_components.php: true` or `optional_components.arcanist: true`

### 4. Caching Improvements
Added `not_if` guards to prevent reinstalling:
- Go binaries (9 packages)
- Go linters (golint, goimports)
- Node packages (yarn, protobufjs)
- JS linters (jshint)
- Python linters (flake8, mypy, yamllint)

### 5. Optional Tools
Made conditional:
- packer
- trivy
- lego
- gperftools (enabled by default in optimized config)
- docker-buildx (enabled by default in optimized config)

## Customizing Your Configuration

Create a custom node JSON file based on your needs:

```json
{
   "run_list": [ "role[px_workstation]" ],
   "env": "base",
   "optional_components": {
      "gcloud": false,              // Install Google Cloud SDK
      "gcloud_update": false,       // Run gcloud components update (slow!)
      "gcloud_components": false,   // Install beta, gke-gcloud-auth-plugin, docker-credential-gcr
      "php": false,                 // Install PHP
      "arcanist": false,            // Install Arcanist
      "k8s_tools": false,           // Install kubectl, helm, kustomize, minikube, skaffold, opm
      "packer": false,              // Install Packer
      "trivy": false,               // Install Trivy security scanner
      "lego": false,                // Install Lego ACME client
      "gperftools": true,           // Install gperftools
      "docker_buildx": true         // Install docker-buildx plugin
   }
}
```

## Usage Examples

### Minimal Installation (Fastest)
```json
{
   "run_list": [ "role[px_workstation]" ],
   "env": "base",
   "optional_components": {
      "gcloud": false,
      "k8s_tools": false,
      "php": false,
      "arcanist": false,
      "packer": false,
      "trivy": false,
      "lego": false,
      "gperftools": false,
      "docker_buildx": false
   }
}
```

### With Google Cloud (If You Need It)
```json
{
   "run_list": [ "role[px_workstation]" ],
   "env": "base",
   "optional_components": {
      "gcloud": true,               // Install gcloud
      "gcloud_update": false,       // Skip update (faster)
      "gcloud_components": false,   // Skip additional components
      "k8s_tools": false,
      "php": false,
      "arcanist": false,
      "gperftools": true,
      "docker_buildx": true
   }
}
```

### Full Installation (Everything)
```json
{
   "run_list": [ "role[px_workstation]" ],
   "env": "base",
   "optional_components": {
      "gcloud": true,
      "gcloud_update": true,
      "gcloud_components": true,
      "php": true,
      "arcanist": true,
      "k8s_tools": true,
      "packer": true,
      "trivy": true,
      "lego": true,
      "gperftools": true,
      "docker_buildx": true
   }
}
```

## What's Always Installed

These components are always installed (core development dependencies):
- Go 1.24.6 + essential Go tools
- Node.js 18.16.0 + yarn + protobufjs
- Python 3.12 + pip
- Clang 15.0
- Docker
- Build essentials (gcc, make, etc.)
- Development tools: git, curl, vim, jq, etc.
- Linters: golangci-lint, shellcheck, clang-linters, bazel
- Essential CLI tools: gh, sops, yq, faq

## Troubleshooting

### Still Getting Timeouts?

1. **Check if gcloud is already installed**: If `/opt/google-cloud-sdk` exists, the script will skip installation
2. **Network issues**: Slow downloads can cause timeouts. Try running again - the guards will skip already-installed components
3. **Verify your config**: Ensure `optional_components.gcloud: false` in your node JSON

### Need to Force Reinstall?

Remove the existing installation directory:
```bash
sudo rm -rf /opt/google-cloud-sdk  # For gcloud
sudo rm -rf /opt/px_dev/gopath/bin # For Go binaries
```

## Performance Comparison

| Configuration | Estimated Runtime | Use Case |
|--------------|------------------|----------|
| Original (node_workstation.json) | 15-20 min | Full installation with all tools |
| Optimized (node_workstation_optimized.json) | 5-8 min | Balanced, skips slow components |
| Minimal | 3-5 min | Only core dev tools |

## Migration Guide

### Updating Existing Installations

If you've already run the original configuration:
1. Use the optimized config - guards will skip already-installed components
2. Runtime will be much faster on subsequent runs
3. No need to uninstall anything unless you want to free disk space

### Switching Configurations

You can switch between different configurations at any time:
```bash
# Use optimized
sudo CHEF_LICENSE="accept" chef-solo -c tools/chef/solo.rb -j tools/chef/node_workstation_optimized.json

# Use full installation
sudo CHEF_LICENSE="accept" chef-solo -c tools/chef/solo.rb -j tools/chef/node_workstation_full.json
```

The guards ensure only missing components are installed.
