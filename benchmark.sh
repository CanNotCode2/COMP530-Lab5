#!/bin/bash

# Configuration
DEVICE="./benchmark_temp_file"  # Change to /dev/sdb1 for SSD tests
OUTPUT_DIR="benchmark_results"
mkdir -p $OUTPUT_DIR

# IO sizes to test (in bytes)
IO_SIZES=(4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152 4194304 8388608 16777216 33554432 67108864 104857600)

# Stride sizes to test (in bytes)
STRIDE_SIZES=(4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152 4194304 8388608 16777216 33554432 67108864 104857600)

# IO sizes for stride test
STRIDE_IO_SIZES=(4096 65536 524288 1048576 10485760)

# Number of iterations
ITERATIONS=1

# Function to run benchmarks
run_benchmark_set() {
    local name=$1
    local output_file="$OUTPUT_DIR/${name}.csv"

    echo "Running $name benchmarks..."

    case $name in
        "sequential_size_read")
            for size in "${IO_SIZES[@]}"; do
                ./benchmark -d $DEVICE -s $size -n $ITERATIONS -o "$output_file"
            done
            ;;

        "sequential_size_write")
            for size in "${IO_SIZES[@]}"; do
                ./benchmark -d $DEVICE -s $size -n $ITERATIONS -w -o "$output_file"
            done
            ;;

        "stride_read")
            for io_size in "${STRIDE_IO_SIZES[@]}"; do
                for stride in "${STRIDE_SIZES[@]}"; do
                    ./benchmark -d $DEVICE -s $io_size -t $stride -n $ITERATIONS -o "$output_file"
                done
            done
            ;;

        "stride_write")
            for io_size in "${STRIDE_IO_SIZES[@]}"; do
                for stride in "${STRIDE_SIZES[@]}"; do
                    ./benchmark -d $DEVICE -s $io_size -t $stride -n $ITERATIONS -w -o "$output_file"
                done
            done
            ;;

        "random_read")
            for size in "${IO_SIZES[@]}"; do
                ./benchmark -d $DEVICE -s $size -R -n $ITERATIONS -o "$output_file"
            done
            ;;

        "random_write")
            for size in "${IO_SIZES[@]}"; do
                ./benchmark -d $DEVICE -s $size -R -w -n $ITERATIONS -o "$output_file"
            done
            ;;
    esac
}

# Compile the benchmark program
gcc -O2 benchmark.c -o benchmark

# Run all benchmark sets
run_benchmark_set "sequential_size_read"
run_benchmark_set "sequential_size_write"
run_benchmark_set "stride_read"
run_benchmark_set "stride_write"
run_benchmark_set "random_read"
run_benchmark_set "random_write"

echo "All benchmarks completed. Results are in $OUTPUT_DIR/"