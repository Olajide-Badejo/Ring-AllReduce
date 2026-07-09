#!/usr/bin/env bash
set -euo pipefail

# Installs the toolchain this project needs, on a Debian/Ubuntu machine.
#
# NOTE ON PROVENANCE: this repository's own scaffolding was written in a
# sandbox with NO network access (see docs/DESIGN_DECISIONS.md), so this
# script documents the exact commands that WOULD set up a matching
# environment but has not itself been executed end to end there. Adjust
# package names for other distributions (dnf, pacman, etc.) if needed.

sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git \
  openmpi-bin libopenmpi-dev \
  python3 python3-pip python3-venv \
  texlive texlive-latex-extra texlive-fonts-recommended texlive-science latexmk

python3 -m pip install --user -r analysis/requirements.txt

echo
echo "Toolchain installed. Sanity-check versions:"
echo "  g++ --version; cmake --version; mpirun --version; latexmk --version"
echo
echo "Then build and test:"
echo "  cmake -S . -B build -DRING_ALLREDUCE_WARNINGS_AS_ERRORS=ON"
echo "  cmake --build build -j"
echo "  ctest --test-dir build --output-on-failure"
