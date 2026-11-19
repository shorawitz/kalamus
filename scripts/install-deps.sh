#!/usr/bin/env bash
# Installs everything needed to build Kalamus on Ubuntu 26.04 (KDE Plasma 6 / KF6).
set -euo pipefail
sudo -v || exit 1
sudo apt-get update
sudo apt-get install -y \
  cmake ninja-build g++ gettext extra-cmake-modules \
  qt6-base-dev \
  libkf6coreaddons-dev libkf6i18n-dev libkf6xmlgui-dev libkf6configwidgets-dev \
  libkf6widgetsaddons-dev libkf6config-dev libkf6syntaxhighlighting-dev libkf6sonnet-dev libkf6colorscheme-dev \
  libcmark-gfm-dev libcmark-gfm-extensions-dev \
  pandoc
echo "Done. Kalamus build dependencies are installed."
