FROM alpine
MAINTAINER "Simon Gomizelj <simon@vodik.xyz>"

RUN apk add --no-cache \
    python \
    python-dev \
    py-pip \
    build-base \
    ragel \
    pacman-dev \
    libffi-dev \
 && rm -rf /var/cache/apk/*

RUN pip install \
    cffi \
    pytest \
    pytest-xdist

ADD . /usr/src
WORKDIR /usr/src
CMD ["make", "tests"]
