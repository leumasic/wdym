FROM nvcr.io/nvidia/pytorch:26.01-py3

# Install prerequisites for Neovim and LazyVim (git, curl, build essentials, ripgrep, fd-find)
RUN apt-get update && apt-get install -y \
    curl \
    git \
    build-essential \
    ripgrep \
    fd-find \
    && rm -rf /var/lib/apt/lists/*

# Download and install the latest stable Neovim binary tarball
RUN curl -LO https://github.com/neovim/neovim/releases/latest/download/nvim-linux-x86_64.tar.gz \
    && tar -C /opt -xzf nvim-linux-x86_64.tar.gz \
    && rm nvim-linux-x86_64.tar.gz

RUN git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf
RUN ~/.fzf/install --all

# Add Neovim to PATH
ENV PATH="/opt/nvim-linux-x86_64/bin:${PATH}"

# Install uv, which builds the project's virtual environment from pyproject.toml and uv.lock
COPY --from=ghcr.io/astral-sh/uv:latest /uv /uvx /usr/local/bin/

# Activate the project's venv for every process in the container. `uv sync` creates it in
# the bind-mounted repo, so it doesn't exist until that has been run once.
ENV VIRTUAL_ENV=/workspace/.venv
ENV PATH="/workspace/.venv/bin:${PATH}"
