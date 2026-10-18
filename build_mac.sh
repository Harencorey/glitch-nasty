#!/bin/bash
# Build Glitch Nasty (AU + VST3 + Standalone) on macOS, install the plug-ins and run the engine tests.
#   bash build_mac.sh                 -> downloads JUCE 8.0.15 automatically
#   bash build_mac.sh /path/to/JUCE   -> uses your own JUCE checkout instead
set -euo pipefail
cd "$(dirname "$0")"

command -v cmake >/dev/null || { echo "CMake not found. Install it with:  brew install cmake"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Xcode command line tools missing. Run:  xcode-select --install"; exit 1; }

JUCE_ARG=()
if [ $# -ge 1 ]; then JUCE_ARG=(-DJUCE_DIR="$1"); fi

# Plain Makefiles: works with just the command line tools (no full Xcode needed)
cmake -B build-mac -DCMAKE_BUILD_TYPE=Release "${JUCE_ARG[@]+"${JUCE_ARG[@]}"}"
cmake --build build-mac --config Release --target GlitchNasty_AU GlitchNasty_VST3 GlitchNasty_Standalone GlitchNastyTests -j "$(sysctl -n hw.ncpu)"

echo "Running engine tests..."
./build-mac/GlitchNastyTests

# COPY_PLUGIN_AFTER_BUILD already installed them; make sure Logic/GarageBand see the new AU right away
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Installed:"
ls -d ~/Library/Audio/Plug-Ins/Components/"Glitch Nasty.component" ~/Library/Audio/Plug-Ins/VST3/"Glitch Nasty.vst3" 2>/dev/null || true
echo "Standalone app: $(pwd)/build-mac/GlitchNasty_artefacts/Release/Standalone/Glitch Nasty.app"
echo "Rescan plug-ins in your DAW and look for Norka DSP > Glitch Nasty.  (Logic: auval -v aumf Gnst Nrka)"
