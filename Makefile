.PHONY: build test bench analyze report clean help

BUILD_DIR := build

help:
	@echo "Targets:"
	@echo "  build    configure + build (CMake, warnings as errors)"
	@echo "  test     build, then run the full ctest suite"
	@echo "  bench    build, then run a real local benchmark sweep (opt-in, needs MPI)"
	@echo "  analyze  regenerate report/figures and report/tables from results data"
	@echo "  report   analyze, then compile report/main.pdf"
	@echo "  clean    remove build/ and report LaTeX build artifacts"

build:
	cmake -S . -B $(BUILD_DIR) -DRING_ALLREDUCE_WARNINGS_AS_ERRORS=ON
	cmake --build $(BUILD_DIR) -j

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

# Opt-in: actually launches mpirun repeatedly and can take a while for the
# full N=2..16 sweep. Requires a working MPI installation, which this
# repository's own build sandbox did not have (see docs/DESIGN_DECISIONS.md).
bench: build
	scripts/run_local_sweep.sh

# Prefers a real sweep's output (results/local_run/, produced by `make
# bench`) if one exists; otherwise falls back to the committed
# results/sample_run/ dataset, which is a real single-node Microsoft MPI
# measurement (see its own README.md for provenance and caveats). Either way
# this involves no MPI at all, only Python, so it works on a fresh checkout
# with no benchmarking hardware.
analyze:
	@if [ -f results/local_run/results.csv ] && [ -f results/local_run/pingpong.csv ]; then \
		echo "== using results/local_run/ (a real sweep) =="; \
		python3 analysis/run_full_analysis.py \
			--results results/local_run/results.csv \
			--pingpong results/local_run/pingpong.csv; \
	else \
		echo "== no real sweep found in results/local_run/; using results/sample_run/ (SYNTHETIC, see results/sample_run/README.md) =="; \
		python3 analysis/run_full_analysis.py; \
	fi

report: analyze
	$(MAKE) -C report

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C report clean
