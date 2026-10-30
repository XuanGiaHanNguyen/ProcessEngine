# syntax=docker/dockerfile:1
# ProcessEngine server: build the C++ engine + Crow server, ship a slim runtime image.

FROM ubuntu:24.04 AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++-14 cmake ninja-build git ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake cmake
COPY engine engine
COPY server server
COPY tools tools
ENV CC=gcc-14 CXX=g++-14
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPF_BUILD_TESTS=OFF \
 && cmake --build build --target pf_server pf_compare

FROM ubuntu:24.04
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 processengine
COPY --from=build /src/build/server/pf_server /src/build/tools/pf_compare /usr/local/bin/
COPY instances /app/instances
USER processengine
EXPOSE 8080
HEALTHCHECK --interval=10s --timeout=3s --retries=5 CMD curl -fsS http://localhost:8080/api/health || exit 1
ENTRYPOINT ["pf_server", "--port", "8080", "--instances", "/app/instances"]
