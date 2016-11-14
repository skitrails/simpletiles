FROM ubuntu:16.04
RUN mkdir -p /opt
WORKDIR /opt
COPY ./ /opt
RUN apt-get update && apt-get -y install g++-5 libboost-all-dev libmapnik-dev cmake make && \
    mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make && \
    apt-get -y remove libmapnik-dev g++ libboost-all-dev cmake make && \
    apt-get -y install libmapnik3.0 \
        libboost-regex1.58.0 \
        libboost-system1.58.0 \
        libboost-filesystem1.58.0 && \
    apt-get -y autoremove && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*
