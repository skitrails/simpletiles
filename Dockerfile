FROM ubuntu:16.04
RUN mkdir -p /opt
WORKDIR /opt
RUN apt-get update
RUN apt-get -y install libmapnik-dev g++-5 cmake ccache

EXPOSE 8080
