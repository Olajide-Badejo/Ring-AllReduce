#!/bin/bash
#SBATCH --job-name=ring-allreduce-sweep
#SBATCH --output=ring-allreduce-sweep-%j.out
#SBATCH --error=ring-allreduce-sweep-%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=16
#SBATCH --time=00:30:00
#SBATCH --partition=<your-partition>

# Cluster equivalent of scripts/run_local_sweep.sh, using srun instead of a
# bare mpirun. Edit --partition (and --account, if your site requires one)
# before submitting: sbatch scripts/run_slurm_sweep.sbatch
#
# Site-specific note: some Slurm + Open MPI integrations expect a bare
# mpirun *inside* the job step instead of srun as the launcher, and MCA
# forcing for the ring algorithm is done here via OMPI_MCA_* environment
# variables (the mechanism Open MPI itself documents for setting MCA
# parameters without editing mpirun's command line) rather than an srun
# flag, since srun has no direct --mca equivalent. If your cluster's
# OpenMPI/PMIx setup handles this differently, adjust the two srun lines
# below accordingly -- this script is a starting template, not a
# guaranteed-portable one across every Slurm site.
#
# Run scripts/check_mpi_algorithms.sh on a login or compute node first and
# override RING_ALGO_ID below if the ring algorithm's ID differs from 4 on
# this cluster's Open MPI build.

set -euo pipefail

module load openmpi 2>/dev/null || echo "note: no 'openmpi' module found -- ensure mpirun/srun resolve to Open MPI"

RING_ALGO_ID="${RING_ALGO_ID:-4}"
BUILD_DIR="${BUILD_DIR:-build}"
OUTPUT_DIR="${OUTPUT_DIR:-results/slurm_run_${SLURM_JOB_ID:-manual}}"
DTYPE="${DTYPE:-float}"

mkdir -p "$OUTPUT_DIR"
RESULTS_CSV="$OUTPUT_DIR/results.csv"

for N in 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
  echo "--- N=$N: ring, mpi-default ---"
  srun --ntasks="$N" "$BUILD_DIR/apps/benchmark" \
    --algorithm ring,mpi-default --dtype "$DTYPE" --output "$RESULTS_CSV"

  echo "--- N=$N: mpi-ring (forced algorithm id=$RING_ALGO_ID) ---"
  OMPI_MCA_coll_tuned_use_dynamic_rules=1 \
  OMPI_MCA_coll_tuned_allreduce_algorithm="$RING_ALGO_ID" \
    srun --ntasks="$N" "$BUILD_DIR/apps/benchmark" \
      --algorithm mpi-ring --dtype "$DTYPE" --output "$RESULTS_CSV"
done

echo "--- pingpong (N=2) ---"
srun --ntasks=2 "$BUILD_DIR/apps/pingpong" --output "$OUTPUT_DIR/pingpong.csv"

echo "Done: $RESULTS_CSV"
