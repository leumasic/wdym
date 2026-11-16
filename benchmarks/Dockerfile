FROM docker.io/library/fedora:44

ARG JOBS=12

RUN dnf install -y \
    cargo \
    gcc \
    gcc-c++ \
    git \
    ninja-build \
    python3 \
    tar \
    gzip \
    time \
    util-linux \
    && dnf clean all

ARG CMAKE_VERSION=4.5.0
RUN curl -fL https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz \
    | tar -C /opt -xz
ENV PATH="/opt/cmake-${CMAKE_VERSION}-linux-x86_64/bin:${PATH}"

ARG SENTENCEPIECE_COMMIT=a28cbf07e16d6437e63ad0cf1d3c30ada69a42f8
COPY cpp/encode_lite.cpp /opt/encode_lite.cpp
RUN git clone https://github.com/google/sentencepiece.git /opt/sentencepiece \
    && git -C /opt/sentencepiece checkout ${SENTENCEPIECE_COMMIT} \
    && cp /opt/encode_lite.cpp /opt/sentencepiece/lite/encode_lite.cpp \
    && printf '\nadd_executable(encode_lite encode_lite.cpp)\ntarget_link_libraries(encode_lite cached_sentencepiece_lite sentencepiece_lite)\n' \
       >> /opt/sentencepiece/lite/CMakeLists.txt \
    && cmake -S /opt/sentencepiece -B /opt/sentencepiece/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /opt/sentencepiece/build -j ${JOBS} --target spm_train spm_to_fb encode_lite

COPY rust /opt/huggingface_benchmarks
RUN cargo build --release --jobs ${JOBS} --manifest-path /opt/huggingface_benchmarks/Cargo.toml

RUN git config --global --add safe.directory '*'

WORKDIR /workspace
