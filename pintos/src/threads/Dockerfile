# Use a standard Ubuntu image as our base
FROM ubuntu:22.04

# Avoid interactive prompts during installation
ENV DEBIAN_FRONTEND=noninteractive

# Install all the necessary tools
RUN apt-get update && \
    apt-get install -y \
    nasm \
    qemu-system-x86 \
    make \
    coreutils

# Set a working directory inside the container
WORKDIR /os-dev

# When the container starts, just run a bash shell
CMD ["bash"]