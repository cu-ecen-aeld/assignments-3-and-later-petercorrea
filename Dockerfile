# base image
FROM --platform=linux/amd64 ubuntu:20.04
WORKDIR  /usr/src/app

ENV DEBIAN_FRONTEND=noninteractive

COPY . .

# install the packages and dependencies along with jq so we can parse JSON (add additional packages as necessary)
RUN apt-get update -y && \
    apt-get upgrade -y && \
    apt-get install -y --no-install-recommends sudo ruby cmake curl nodejs wget unzip vim git jq build-essential libssl-dev libffi-dev python3 python3-venv python3-dev python3-pip tar

# update the base packages, add a non-sudo user, download runner
RUN useradd -m -s /bin/bash docker && \
    mkdir -p /home/docker/actions-runner && \
    chown docker:docker /home/docker/actions-runner -R
# chmod 755 ./actions-runner -R

USER docker
# WORKDIR /home/docker/actions-runner
# RUN curl -o actions-runner-linux-x64-2.307.1.tar.gz -L https://github.com/actions/runner/releases/download/v2.307.1/actions-runner-linux-x64-2.307.1.tar.gz && \
#     tar xzf ./actions-runner-linux-x64-2.307.1.tar.gz && \
#     ls -l /home/docker/actions-runner