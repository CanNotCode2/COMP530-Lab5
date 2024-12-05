#!/bin/bash

# Configuration
DEVICE="/run/media/user/b1ef707f-6071-4a99-b608-2c84c642e5fb/tmp_file"
OUTPUT_DIR="hdd_benchmark_results"
FILE_SIZE=$((1024*1024*1024))  # 1GB in bytes

# Create output directory
mkdir -p $OUTPUT_DIR

# IO sizes to test (in bytes)
IO_SIZES=(4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152 4194304 8388608 16777216 33554432 67108864 104857600)

# Stride sizes to test (in bytes)
STRIDE_SIZES=(4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152 4194304 8388608 16777216 33554432 67108864 104857600)

# IO sizes for stride test
STRIDE_IO_SIZES=(4096 65536 524288 1048576 10485760)

# Number of iterations
ITERATIONS=5

# Create initial test file with random data
create_test_file() {
    echo "Creating 1GB test file with random data..."
    dd if=/dev/urandom of=$DEVICE bs=1M count=1024 status=progress
    if [ $? -ne 0 ]; then
        echo "Error: Failed to create test file"
        exit 1
    fi
    sync  # Ensure the file is written to disk
    echo "Test file created successfully"
}

# Function to check if a test has been completed
is_test_completed() {
    local output_file=$1
    local operation=$2
    local io_size=$3
    local stride_size=$4
    local is_random=$5

    # If file doesn't exist, test hasn't been run
    [ ! -f "$output_file" ] && return 1

    # Check if we have all iterations for this configuration
    completed_iterations=$(grep "^$operation,$io_size,$stride_size,$is_random" "$output_file" | wc -l)
    [ "$completed_iterations" -eq "$ITERATIONS" ]
    return $?
}

# Function to run benchmarks
run_benchmark_set() {
    local name=$1
    local output_file="$OUTPUT_DIR/${name}.csv"

    echo "Running $name benchmarks..."

    # Create header if file doesn't exist
    if [ ! -f "$output_file" ]; then
        echo "operation,io_size,stride_size,is_random,iteration,throughput,mean,stddev,ci95" > "$output_file"
    fi

    case $name in
        "sequential_size_read")
            for size in "${IO_SIZES[@]}"; do
                if ! is_test_completed "$output_file" "read" "$size" "0" "false"; then
                    ./benchmark -d $DEVICE -s $size -n $ITERATIONS -m 1000 -o "$output_file"
                fi
            done
            ;;

        "sequential_size_write")
            for size in "${IO_SIZES[@]}"; do
                if ! is_test_completed "$output_file" "write" "$size" "0" "false"; then
                    ./benchmark -d $DEVICE -s $size -n $ITERATIONS -m 1000 -w -o "$output_file"
                fi
            done
            ;;

        "stride_read")
            for io_size in "${STRIDE_IO_SIZES[@]}"; do
                for stride in "${STRIDE_SIZES[@]}"; do
                    if ! is_test_completed "$output_file" "read" "$io_size" "$stride" "false"; then
                        ./benchmark -d $DEVICE -s $io_size -t $stride -n $ITERATIONS -m 5 -o "$output_file"
                    fi
                done
            done
            ;;

        "stride_write")
            for io_size in "${STRIDE_IO_SIZES[@]}"; do
                for stride in "${STRIDE_SIZES[@]}"; do
                    if ! is_test_completed "$output_file" "write" "$io_size" "$stride" "false"; then
                        ./benchmark -d $DEVICE -s $io_size -t $stride -n $ITERATIONS -m 5 -w -o "$output_file"
                    fi
                done
            done
            ;;

"random_read")
            for size in "${IO_SIZES[@]}"; do
                if ! is_test_completed "$output_file" "read" "$size" "0" "true"; then
                    # Use smaller multiplier for larger IO sizes
                    if [ "$size" -lt 65536 ]; then
                        multiplier=500
                    elif [ "$size" -lt 1048576 ]; then
                        multiplier=100
                    else
                        multiplier=50
                    fi
                    ./benchmark -d $DEVICE -s $size -R -n $ITERATIONS -m $multiplier -o "$output_file"
                fi
            done
            ;;

        "random_write")
            for size in "${IO_SIZES[@]}"; do
                if ! is_test_completed "$output_file" "write" "$size" "0" "true"; then
                    # Use smaller multiplier for larger IO sizes
                    if [ "$size" -lt 65536 ]; then
                        multiplier=500
                    elif [ "$size" -lt 1048576 ]; then
                        multiplier=100
                    else
                        multiplier=50
                    fi
                    ./benchmark -d $DEVICE -s $size -R -w -n $ITERATIONS -m $multiplier -o "$output_file"
                fi
            done
            ;;
    esac
}

# Check if test file exists
if [ ! -f "$DEVICE" ]; then
    create_test_file
else
    echo "Test file already exists. Using existing file."
    # Verify file size using stat -c %s for Linux
    actual_size=$(stat -c %s "$DEVICE")
    if [ "$actual_size" -ne "$FILE_SIZE" ]; then
        echo "Warning: Existing file size ($actual_size bytes) differs from expected size ($FILE_SIZE bytes)"
        echo "Recreating test file..."
        create_test_file
    fi
fi

# Compile the benchmark program
gcc -O2 benchmark.c -lm -o benchmark

run_benchmark_set "sequential_size_read"
run_benchmark_set "sequential_size_write"
run_benchmark_set "stride_read"
run_benchmark_set "stride_write"
run_benchmark_set "random_read"
run_benchmark_set "random_write"

echo "All benchmarks completed. Results are in $OUTPUT_DIR/"