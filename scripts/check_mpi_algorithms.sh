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

echo "Allreduce algorithm enumerators (the exact query this project's spec recommends):"
echo "\$ ompi_info --parseable --all | grep _algorithm: | grep enumerator | grep allreduce"
ompi_info --parseable --all 2>/dev/null | grep "_algorithm:" | grep enumerator | grep allreduce || \
  echo "(no rows matched -- see the broader fallback query below)"

echo
echo "Broader fallback (full parameter description, useful if the line above is empty" \
     "or your Open MPI build's --parseable output is formatted differently):"
ompi_info --param coll tuned --level 9 2>/dev/null | grep -A 40 "coll_tuned_allreduce_algorithm" || \
  ompi_info --all 2>/dev/null | grep -i -B2 -A2 "allreduce_algorithm" || \
  echo "still nothing -- try 'ompi_info --all | less' and search for 'allreduce' by hand"

echo
echo "Find the line naming the 'ring' algorithm (e.g. 'value 4 ring, ...' or similar) and use that number:"
echo "  RING_ALGO_ID=<id> ./scripts/run_local_sweep.sh"
echo
echo "Record the Open MPI version and the ring algorithm ID you found in docs/BENCHMARKING.md."
