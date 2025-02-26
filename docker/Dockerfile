# syntax=docker/dockerfile:1.7

FROM ubuntu:24.04

ARG HIBP_VERSION=0.6.1

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt update && \
    apt install --no-install-recommends -y \
    curl ca-certificates 'libevent-2.1*' && \
    useradd -ms /bin/bash hibp && \
    mkdir -p /data && \
    chown hibp:hibp /data

RUN curl -sSL -o hibp.deb \
    https://github.com/oschonrock/hibp/releases/download/v${HIBP_VERSION}/hibp_${HIBP_VERSION}-1_amd64.deb && \
    dpkg -i hibp.deb && \
    rm -f hibp.deb

COPY docker/entrypoint.sh /entrypoint.sh

WORKDIR /data
COPY data /data
VOLUME /data
EXPOSE 8082
CMD ["/entrypoint.sh"]
