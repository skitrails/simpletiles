FROM ubuntu:16.04 as buildstage
RUN apt-get update && apt-get -y install g++-5 libboost-all-dev libmapnik-dev libprotobuf-dev protobuf-compiler python cmake make

RUN apt-get -y install git libprotozero-dev
RUN mkdir -p /opt
WORKDIR /opt
COPY ./ /opt

RUN git submodule init && git submodule update
RUN cd deps/mapnik-vector-tile && git checkout v1.0.4 && make libvtile
RUN mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make

FROM ubuntu:16.04 as runstage
RUN apt-get update && \
    apt-get -y install \
       libboost-filesystem1.58.0 \
       libboost-regex1.58.0 \
       libboost-system1.58.0 \
       libboost-program-options1.58.0 \
       libmapnik3.0 \
       libprotobuf9v5 && \
    apt-get -y autoremove && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists*
COPY --from=buildstage /opt/build/tileserver /usr/local/bin
WORKDIR /
