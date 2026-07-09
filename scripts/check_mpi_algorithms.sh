#!/usr/bin/env bash
set -euo pipefail

# Prints this machine's Open MPI tuned-collective algorithm choices for
# MPI_Allreduce. The ring algorithm's numeric ID has moved across Open MPI
# releases (it is commonly, but not reliably, 4) -- run this BEFORE
# scripts/run_local_sweep.sh and pass the right ID via RING_ALGO_ID=<id> if
# yours differs. See docs/BENCHMARKING.md.

if ! command -v ompi_info >/dev/null 2>&1; then
  echo "error: ompi_info not found on PATH -- is Open MPI installed?" >&2
  exit 1
fi

echo "Open MPI version:"
ompi_info --version | head -1
echo

echo "coll_tuned_allreduce_algorithm choices on this installation:"
if ompi_info --param coll tuned --level 9 2>/dev/null | grep -A 40 "coll_tuned_allreduce_algorithm"; then
  :
else
  echo "(the --param query above found nothing; falling back to a broader search)"
  ompi_info --all 2>/dev/null | grep -i -B2 -A2 "allreduce_algorithm" || \
    echo "still nothing -- try 'ompi_info --all | less' and search for 'allreduce' by hand"
fi

echo
echo "Find the line naming the 'ring' algorithm (e.g. 'value 4 ring, ...') and use that number:"
echo "  RING_ALGO_ID=<id> ./scripts/run_local_sweep.sh"
