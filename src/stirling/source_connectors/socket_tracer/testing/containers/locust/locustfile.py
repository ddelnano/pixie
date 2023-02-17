import logging
import time
from locust import between, events, HttpUser, task, runners

# This is needed for locust to support the -i (iterations) flag to generate
# a stable number of requests. See https://github.com/locustio/locust/issues/1085
import locust_plugins

# Load test that sends a request with a particular User-Agent to /{user_agent}.
# It expects that the server will 
class UserAgentResponseMultiplier(HttpUser):
    wait_time = between(1, 5)

    @task()
    def fetch(self):
        user_agent = self.environment.parsed_options.user_agent
        resp = self.client.get(f"/{user_agent}", headers={"User-Agent": user_agent}, verify=False)
        logging.debug(f"Received resp with length: {len(resp.text)} and contains chars: {set(resp.text)} ")

@events.init_command_line_parser.add_listener
def _(parser):
    # TODO(ddelnano): Make this required
    parser.add_argument("--user-agent", type=str, default="")
