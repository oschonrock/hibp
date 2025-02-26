# syntax=docker/dockerfile:1.7

ARG BASE_IMAGE=ubuntu:24.04

FROM ${BASE_IMAGE} AS build

ARG TEST=ON
ARG BUILD_TYPE=release

RUN rm -f /etc/apt/apt.conf.d/docker-clean && \
    echo 'Binary::apt::APT::Keep-Downloaded-Packages "true";' > /etc/apt/apt.conf.d/keep-cache

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt update && \
    apt install -y \
      sudo curl wget unzip git ruby \
      build-essential cmake ninja-build \
      libcurl4-openssl-dev libevent-dev libtbb-dev

WORKDIR /usr/src

COPY ext ./ext

RUN cd ext/restinio && \
    gem install Mxx_ru && \
    mxxruexternals

COPY app ./app
COPY include ./include
COPY src ./src
COPY test ./test
COPY CMakeLists.txt .

RUN \
    cmake -G Ninja -S . -B build \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DNOPCH=ON -DBINFUSE_TEST=OFF -DHIBP_TEST=${TEST} && \
    cmake --build build && \
    \
    strip build/hibp-* && \
    cmake --install build --prefix /usr/local

# ---

FROM ${BASE_IMAGE}

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt update && \
    apt install --no-install-recommends -y \
    curl ca-certificates 'libevent-2.1*' && \
    useradd -ms /bin/bash hibp && \
    mkdir -p /data && \
    chown hibp:hibp /data

COPY --from=build /usr/local/bin/hibp-* /usr/local/bin/

COPY docker/entrypoint.sh /entrypoint.sh

WORKDIR /data
COPY data /data
VOLUME /data
EXPOSE 8082
CMD ["/entrypoint.sh"]
