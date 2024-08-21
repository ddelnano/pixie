# Triggering Short Lived Processes

To view this error I added a server process written in go that serves a single request after waiting for a particular timeout specificed by the payload.
Thus the length of the process can be controlled from the client.
Then simple requests with `0ns` timeouts are sufficient to trigger the bug, see `bash/short-request.bash` for more details.
This deploys the server along with the client `1000` times.
If you wish to look at a longer running process we also have `bash/long-request.bash` which ensures a timeout of `10s` for `15` requests.

## System Prerequisites
These have only been tested on a `macOS` in `zsh`.
Actual System Requirements:
- Minikube installation
- Pixie installation
- Pixie account

## How To Run
If you wish to deploy the bug triggering event on `macOS` you can simply run:
`make short-example`.
The default driver is `hyperkit`, however you can set this using `DRIVER=docker make-short-example` for example.
Then you can go to the [pixie website](https://work.withpixie.ai:443) to see how kubernetes is behaving.
Please note that the environment assumes you have no other pixie deployments.
If you do have a pixie deployment then feel free to touch the relevant files, so that you can simply upload the container to the right docker instance.

## What to Expect
NOTHING!

Well you expect to see very few of the short running processes captured in the pixie ui.
In order to see this navigate to the pixie script `http_data` it should look something like the below image.
I used a destination filter for `short-request`.

![Pixie Failure](/trigger_slp_error/images/fail_short.png)

In the image you can see that pixie only captured `1` out of `1000` requests where it could figure out the destination pod. This is clearly the race condition
showing itself.


