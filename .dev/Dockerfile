FROM nvcr.io/nvidia/pytorch:26.01-py3

RUN apt-get update && apt-get install -y software-properties-common \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update && apt-get install -y \
    curl \
    git \
    build-essential \
    g++-15 \
    ripgrep \
    fd-find \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc-15
ENV CXX=g++-15

RUN curl -LO https://github.com/neovim/neovim/releases/latest/download/nvim-linux-x86_64.tar.gz \
    && tar -C /opt -xzf nvim-linux-x86_64.tar.gz \
    && rm nvim-linux-x86_64.tar.gz

ARG LAZYGIT_VERSION=0.66.2
RUN curl -fLo lazygit.tar.gz "https://github.com/jesseduffield/lazygit/releases/download/v${LAZYGIT_VERSION}/lazygit_${LAZYGIT_VERSION}_linux_x86_64.tar.gz" \
    && tar -xzf lazygit.tar.gz lazygit \
    && install lazygit /usr/local/bin/ \
    && rm lazygit lazygit.tar.gz

RUN git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf
RUN ~/.fzf/install --all

ENV PATH="/opt/nvim-linux-x86_64/bin:${PATH}"

COPY --from=ghcr.io/astral-sh/uv:latest /uv /uvx /usr/local/bin/

# TODO: Move installation of cmake 4.5.0 to pyproject.toml once it gets published on pypi
RUN curl -fL https://github.com/Kitware/CMake/releases/download/v4.5.0-rc2/cmake-4.5.0-rc2-linux-x86_64.tar.gz \
    | tar -C /opt -xz \
    && ln -s /opt/cmake-4.5.0-rc2-linux-x86_64/bin/* /usr/local/bin/

ENV VIRTUAL_ENV=/workspace/.venv
ENV PATH="/workspace/.venv/bin:${PATH}"
