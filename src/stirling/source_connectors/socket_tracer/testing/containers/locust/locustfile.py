import time
from locust import between, events, HttpUser, task, runners

class QuickstartUser(HttpUser):
    wait_time = between(1, 5)

    @task()
    def fetch_nginx_page(self):
        user_agent = self.environment.parsed_options.user_agent
        for item_id in range(10):
            self.client.get(f"/{user_agent}", headers={"User-Agent": user_agent}, verify=False)

@events.init.add_listener
def on_locust_init(environment, **_kwargs):
    if not isinstance(environment.runner, runners.MasterRunner):
        QuickstartUser.index = environment.runner.worker_index

@events.init_command_line_parser.add_listener
def _(parser):
    # TODO(ddelnano): Make this required
    parser.add_argument("--user-agent", type=str, default="")
