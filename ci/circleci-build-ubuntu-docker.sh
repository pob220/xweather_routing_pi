#!/usr/bin/env bash
set -euo pipefail
set -x

cd "${HOME}/project"
git submodule update --init --recursive

test -n "${DOCKER_IMAGE:-}"
test -n "${OCPN_TARGET:-}"
test -n "${BUILD_ENV:-}"

mkdir -p build artifacts
docker build --build-arg "BASE_IMAGE=${DOCKER_IMAGE}" \
  -f "${DOCKERFILE:-ci/Dockerfile.linux}" \
  -t weather-routing-linux-build ci
docker run --rm \
  -e "BUILD_ENV=${BUILD_ENV}" \
  -e "OCPN_TARGET=${OCPN_TARGET}" \
  -e "WX_VER=${WX_VER:-32}" \
  -e "BUILD_GTK3=${BUILD_GTK3:-true}" \
  -e "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-3}" \
  -v "${PWD}:/src:rw" \
  -v "${PWD}/build:/work" \
  weather-routing-linux-build \
  /src/ci/build-linux-catalogue.sh

sudo chmod -R a+rw build
cp -a build/artifacts/. artifacts/
