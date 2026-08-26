FROM debian:bullseye

# Install the cross-compiler and basic build tools
RUN apt-get update && apt-get install -y \
    gcc-arm-linux-gnueabihf \
    binutils-arm-linux-gnueabihf \
    libc6-dev-armhf-cross \
    make \
    git