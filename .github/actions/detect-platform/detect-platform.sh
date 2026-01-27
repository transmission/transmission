#!/usr/bin/env bash
set -ex

OS_FAMILY="unknown"
DISTRO="unknown"
DISTRO_VERSION=""
PKG_FAMILY="unknown"

if [[ "$(uname -s)" == "Darwin" ]]; then
  OS_FAMILY="macos"
  PKG_FAMILY="brew"
  echo "Detected macOS"
elif [[ -f /etc/os-release ]]; then
  source /etc/os-release
  echo "Detected Linux: ID=$ID, VERSION_ID=$VERSION_ID"

  OS_FAMILY="linux"
  DISTRO="$ID"
  DISTRO_VERSION="$VERSION_ID"

  case "$ID" in
    ubuntu)
      PKG_FAMILY="apt"
      ;;
    debian)
      PKG_FAMILY="apt"
      ;;
    fedora)
      PKG_FAMILY="dnf"
      ;;
    *)
      echo "Warning: Unsupported Linux distribution: $ID"
      ;;
  esac
else
  echo "Error: Unable to detect platform"
fi

echo "OS_FAMILY=$OS_FAMILY" >> "$GITHUB_ENV"
echo "DISTRO=$DISTRO" >> "$GITHUB_ENV"
echo "DISTRO_VERSION=$DISTRO_VERSION" >> "$GITHUB_ENV"
echo "PKG_FAMILY=$PKG_FAMILY" >> "$GITHUB_ENV"

echo "os-family=$OS_FAMILY" >> "$GITHUB_OUTPUT"
echo "distro=$DISTRO" >> "$GITHUB_OUTPUT"
echo "distro-version=$DISTRO_VERSION" >> "$GITHUB_OUTPUT"
echo "pkg-family=$PKG_FAMILY" >> "$GITHUB_OUTPUT"

echo "Final detected platform: OS_FAMILY=$OS_FAMILY DISTRO=$DISTRO DISTRO_VERSION=$DISTRO_VERSION PKG_FAMILY=$PKG_FAMILY"
