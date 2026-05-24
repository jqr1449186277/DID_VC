FROM node:20-bookworm

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    bash \
    build-essential \
    ca-certificates \
    curl \
    git \
    jq \
    libgmp-dev \
    libsodium-dev \
    nlohmann-json3-dev \
    procps \
    python3 \
    python3-pip \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["bash", "-lc", "scripts/build.sh all && scripts/dev.sh up && tail -f /dev/null"]
