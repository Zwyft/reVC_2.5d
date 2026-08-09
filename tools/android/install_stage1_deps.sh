#!/usr/bin/env bash
set -euo pipefail

PACKAGES=(
  openjdk-17-jdk
  build-essential
  cmake
  ninja-build
  libglfw3-dev
  libx11-dev
  libgl1-mesa-dev
  libopenal-dev
  libmpg123-dev
  libsndfile1-dev
  pkg-config
  unzip
  wget
)

echo "Installing host packages with apt. This requires sudo/root."
sudo apt-get update
sudo apt-get install -y "${PACKAGES[@]}"

if ! command -v sdkmanager >/dev/null 2>&1; then
  cat >&2 <<'MSG'

sdkmanager was not found after host package installation. Install Android Studio or Android command-line tools, then ensure sdkmanager is on PATH.
Required Android packages:
  platform-tools
  platforms;android-35
  build-tools;35.0.0
  ndk;27.2.12479018
  cmake;3.22.1
MSG
  exit 2
fi

yes | sdkmanager --licenses
sdkmanager \
  "platform-tools" \
  "platforms;android-35" \
  "build-tools;35.0.0" \
  "ndk;27.2.12479018" \
  "cmake;3.22.1"

echo "Stage 1 dependencies installed."
