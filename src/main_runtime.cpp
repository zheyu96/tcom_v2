// Strict runtime benchmark entry point.
//
// The implementation is shared with main_time.cpp so both drivers use the
// same workloads, timed region, aggregation, and output format.
// All approximation algorithms share EXPERIMENT_EPSILON from config.h;
// --epsilon overrides it consistently for this benchmark.
// Unlike the development driver, this executable refuses to run unless every requested
// algorithm (including both Gurobi-backed EFiRAP variants) is available. It
// therefore cannot silently write incomplete or column-shifted ANS files.

#define RUNTIME_BENCHMARK_REQUIRE_ALL_ALGORITHMS 1
#define RUNTIME_BENCHMARK_LOG_NAME "main_runtime"
#define RUNTIME_BENCHMARK_INPUT_STEM "main_runtime"
#define RUNTIME_BENCHMARK_RAW_FILENAME "main_runtime_raw.csv"
#define RUNTIME_BENCHMARK_SUMMARY_FILENAME "main_runtime_summary.csv"

#include "main_time.cpp"
