FROM mikedld/clang-format:12-debian AS clang_format

FROM debian:bullseye-slim AS final

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        gnupg2 \
    && curl -sS https://dl.yarnpkg.com/debian/pubkey.gpg | apt-key add - \
    && echo "deb https://dl.yarnpkg.com/debian/ stable main" >> /etc/apt/sources.list.d/yarn.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        bash \
        nodejs \
        perl \
        yarn \
    && rm -rf /var/lib/apt/lists/*

COPY --from=clang_format /usr/bin/clang-format /usr/bin/
