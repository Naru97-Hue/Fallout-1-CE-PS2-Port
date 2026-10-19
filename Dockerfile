# Reproducible PS2 build. From the repository root:
#   docker build -t fallout1-ce-ps2 .
#   docker run --rm -v "$PWD:/src" fallout1-ce-ps2 tools/make_dist.sh v0.7
FROM ps2dev/ps2dev:latest

RUN apk add --no-cache cmake make g++ python3 zip git

ENV PS2DEV=/usr/local/ps2dev
ENV PS2SDK=$PS2DEV/ps2sdk
ENV GSKIT=$PS2DEV/gsKit
ENV PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin

WORKDIR /src
# Configure on first run if needed, then whatever command was given.
ENTRYPOINT ["/bin/sh", "-c", "[ -d build-ps2 ] || cmake -S . -B build-ps2 -DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake -DCMAKE_BUILD_TYPE=Release; exec \"$@\"", "--"]
CMD ["cmake", "--build", "build-ps2", "-j8"]
