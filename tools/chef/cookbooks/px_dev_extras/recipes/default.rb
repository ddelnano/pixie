# Copyright 2018- The Pixie Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

ENV['CLOUDSDK_CORE_DISABLE_PROMPTS'] = '1'
ENV['CLOUDSDK_INSTALL_DIR'] = '/opt'
ENV['PATH'] = "/opt/google-cloud-sdk/bin:#{ENV['PATH']}"

include_recipe 'px_dev_extras::mac_os_x'

if node['optional_components'] && node['optional_components']['gperftools']
  include_recipe 'px_dev_extras::gperftools'
end

include_recipe 'px_dev_extras::packaging'

pkg_list = [
  'cmake',
  'emacs',
  'graphviz',
  'jq',
  'vim',
]

if platform_family?('debian')
  apt_package pkg_list + ['zsh'] do
    action :upgrade
  end
end

if platform_family?('mac_os_x')
  homebrew_package pkg_list
end

remote_file '/usr/local/share/zsh/site-functions/_bazel' do
  source node['bazel']['zsh_completions']
  mode 0644
  checksum node['bazel']['zcomp_sha256']
end

if node['optional_components'] && node['optional_components']['k8s_tools']
  common_remote_bin 'faq'
  common_remote_bin 'sops'
  common_remote_bin 'yq'
end

common_remote_tar_bin 'gh' do
  tool_loc 'bin/gh'
  strip_components 1
end

# Kubernetes tools - optional, disabled by default
if node['optional_components'] && node['optional_components']['k8s_tools']
  common_remote_bin 'kubectl'
  common_remote_tar_bin 'kustomize'
  common_remote_bin 'minikube'
  common_remote_bin 'opm'
  common_remote_bin 'skaffold'

  common_remote_tar_bin 'helm' do
    strip_components 1
  end
end

if node['optional_components'] && node['optional_components']['lego']
  common_remote_tar_bin 'lego'
end

if node['optional_components'] && node['optional_components']['trivy']
  common_remote_tar_bin 'trivy'
end

# Google Cloud SDK installation - can be slow, make it optional
if node['optional_components'] && node['optional_components']['gcloud']
  execute 'install gcloud' do
    command 'curl https://sdk.cloud.google.com | bash'
    creates '/opt/google-cloud-sdk'
    action :run
    not_if { ::File.exist?('/opt/google-cloud-sdk/bin/gcloud') }
  end

  # Only update if explicitly requested (very slow operation)
  if node['optional_components']['gcloud_update']
    execute 'update gcloud' do
      command 'gcloud components update'
      action :run
      only_if { ::File.exist?('/opt/google-cloud-sdk/bin/gcloud') }
    end
  end

  # Only install additional components if explicitly requested
  if node['optional_components']['gcloud_components']
    execute 'install components' do
      command 'gcloud components install beta gke-gcloud-auth-plugin docker-credential-gcr'
      action :run
      only_if { ::File.exist?('/opt/google-cloud-sdk/bin/gcloud') }
    end

    execute 'configure docker-credential-gcr' do
      command 'docker-credential-gcr configure-docker'
      action :run
      only_if { ::File.exist?('/opt/google-cloud-sdk/bin/docker-credential-gcr') }
    end
  end

  directory '/opt/google-cloud-sdk/.install/.backup' do
    action :delete
    recursive true
    only_if { ::File.exist?('/opt/google-cloud-sdk/.install/.backup') }
  end

  execute 'remove gcloud pycache' do
    action :run
    cwd '/opt/google-cloud-sdk'
    command "find . -regex '.*/__pycache__' -exec rm -r {} +"
    only_if { ::File.exist?('/opt/google-cloud-sdk') }
  end
end

if node['optional_components'] && node['optional_components']['packer']
  remote_file '/tmp/packer.zip' do
    source node['packer']['download_path']
    mode '0644'
    checksum node['packer']['sha256']
  end

  execute 'install packer' do
    command 'unzip -d /opt/px_dev/tools/packer -o /tmp/packer.zip'
  end

  link '/opt/px_dev/bin/packer' do
    to '/opt/px_dev/tools/packer/packer'
    link_type :symbolic
    owner node['owner']
    group node['group']
    action :create
  end

  file '/tmp/packer.zip' do
    action :delete
  end
end

if node['optional_components'] && node['optional_components']['docker_buildx']
  directory '/usr/local/lib/docker/cli-plugins' do
    action :create
    recursive true
    owner node['owner']
    group node['group']
    mode '0755'
  end

  remote_file '/usr/local/lib/docker/cli-plugins/docker-buildx' do
    source node['docker-buildx']['download_path']
    mode '0755'
    checksum node['docker-buildx']['sha256']
  end
end
