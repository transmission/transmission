FROM alpine:3.11 AS builder
RUN apk --no-cache add cmake make g++ automake git autoconf libtool curl-dev openssl-dev libevent-dev
WORKDIR /src
ADD . .
RUN rm -rf build/ && \
  git submodule update --init && \
  git submodule update && \
  mkdir build && \
  cd build && \
  cmake .. && \
  make

FROM alpine:3.11
RUN apk --no-cache add libevent libcurl
WORKDIR /app
COPY --from=builder /src/build/daemon/transmission-daemon .
CMD exec ./transmission-daemon -f
