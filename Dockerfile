FROM alpine
MAINTAINER "Simon Gomizelj <simon@vodik.xyz>"

RUN apk add --no-cache \
    python3 \
    python3-dev \
    build-base \
    ragel \
    pacman-dev \
    libffi-dev \
 && rm -rf /var/cache/apk/*

RUN python3 -m ensurepip && pip3 install \
    cffi \
    pytest \
    pytest-xdist

ADD . /usr/src
WORKDIR /usr/src
CMD ["make", "tests"]
