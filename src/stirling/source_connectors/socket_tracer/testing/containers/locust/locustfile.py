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

import logging
from locust import between, events, HttpUser, task

# This is needed for locust to support the -i (iterations) flag to generate
# a stable number of requests. See https://github.com/locustio/locust/issues/1085
import locust_plugins  # noqa: F401


# Load test that sends a request with a particular User-Agent to /{user_agent}.
class UserAgentResponseMultiplier(HttpUser):
    wait_time = between(1, 5)

    @task()
    def fetch(self):
        user_agent = self.environment.parsed_options.user_agent
        resp = self.client.get(f"/{user_agent}", headers={"User-Agent": user_agent}, verify=False)
        logging.debug(f"Received resp with length: {len(resp.text)} and contains chars: {set(resp.text)} ")


@events.init_command_line_parser.add_listener
def _(parser):
    # This should be a required parameter if this code is "productionized"
    parser.add_argument("--user-agent", type=str, default="")
