#!/usr/bin/env bash
# Glitch Nasty - build & install on macOS (universal: Apple Silicon + Intel).
#
#   bash build_mac.sh                  # downloads JUCE 8.0.15 into ./JUCE the first time
#   bash build_mac.sh ~/path/to/JUCE   # or use your own JUCE 8.x checkout
#
# Needs: Xcode command line tools (xcode-select --install) and CMake (brew install cmake).
set -euo pipefail

cd "$(dirname "$0")"
JUCE_VERSION="8.0.15"

if [[ $# -ge 1 ]]; then
    JUCE_DIR="$(cd "$1" && pwd)"
else
    JUCE_DIR="$(pwd)/JUCE"
    if [[ ! -d "$JUCE_DIR" ]]; then
        echo "==> Downloading JUCE $JUCE_VERSION"
        git clone --depth 1 --branch "$JUCE_VERSION" https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
    fi
fi

echo "==> Configuring"
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
      -DJUCE_DIR="$JUCE_DIR"

echo "==> Building"
cmake --build build --config Release --parallel "$(sysctl -n hw.ncpu)"

echo "==> Running engine tests"
./build/GlitchNastyTests

cat <<EOF

Done. Installed to ~/Library/Audio/Plug-Ins/ (AU in Components, VST3 in VST3).
Standalone app: build/GlitchNasty_artefacts/Release/Standalone/Glitch Nasty.app
Rescan in your DAW and look for Norka DSP > Glitch Nasty.
Logic AU check:  auval -v aumf Gnst Nrka
EOF
