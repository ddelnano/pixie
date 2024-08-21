FROM ubuntu:22.04
ENV DEBIAN_FRONTEND noninteractive

#Install required python things
ENV APT_BUILD ""
ENV APT_REQUI curl golang-go
RUN apt update -y \
  && apt install -y $APT_BUILD $APT_REQUI\
  && apt remove -y $APT_BUILD\
  && apt autoremove -y\
  && apt clean -y

#Build go src files
COPY src/ /src
RUN cd src/ && go build

COPY bash /bash
