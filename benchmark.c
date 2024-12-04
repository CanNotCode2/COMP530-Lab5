#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <getopt.h>
#include <errno.h>

#ifndef O_DIRECT
#define O_DIRECT 0
#warning O_DIRECT is not available, direct I/O will not be used
#endif

#define BILLION 1000000000L
#define GB (1024*1024*1024L)
#define MB (1024*1024L)
#define KB 1024

typedef struct {
    char* device;           // Device or file to test
    int io_size;           // Size of each I/O operation
    int stride_size;       // Size of stride between operations
    long range;           // Range for random I/Os
    int is_write;         // 1 for write, 0 for read
    int is_random;        // 1 for random, 0 for sequential
    int num_iterations;   // Number of test iterations
    char* output_file;    // CSV output file
} benchmark_config;

double get_time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / BILLION;
}

void print_usage() {
    printf("Usage: benchmark [options]\n");
    printf("Options:\n");
    printf("  -d <device>      Device to test (e.g., /dev/sda2)\n");
    printf("  -s <size>        I/O size in bytes\n");
    printf("  -t <stride>      Stride size in bytes\n");
    printf("  -r <range>       Range for random I/Os in bytes\n");
    printf("  -w               Perform write test (default is read)\n");
    printf("  -R               Perform random I/Os (default is sequential)\n");
    printf("  -n <iterations>  Number of iterations (default: 5)\n");
    printf("  -o <file>        Output CSV file\n");
    exit(1);
}

void write_csv_header(FILE* fp) {
    fprintf(fp, "operation,io_size,stride_size,is_random,iteration,throughput\n");
}

void write_csv_result(FILE* fp, benchmark_config* config, int iteration, double throughput) {
    fprintf(fp, "%s,%d,%d,%d,%d,%.2f\n",
            config->is_write ? "write" : "read",
            config->io_size,
            config->stride_size,
            config->is_random,
            iteration,
            throughput);
}

double run_benchmark(benchmark_config* config) {
    char* buffer;
    int fd;
    struct timespec start, end;
    long total_bytes = GB; // 1GB total
    long current_pos = 0;
    int flags = O_DIRECT;

    // Allocate aligned buffer for O_DIRECT
    if (posix_memalign((void**)&buffer, 4096, config->io_size) != 0) {
        perror("posix_memalign failed");
        exit(1);
    }

    // Initialize buffer with some data
    memset(buffer, 'A', config->io_size);

    // For read tests, first create and initialize the file if it doesn't exist
    if (!config->is_write) {
        int init_fd = open(config->device, O_RDWR | O_CREAT, 0644);
        if (init_fd < 0) {
            perror("Failed to create initialization file");
            free(buffer);
            exit(1);
        }

        // Pre-allocate the file
        if (fallocate(init_fd, 0, 0, total_bytes) < 0) {
            perror("Failed to allocate file space");
            close(init_fd);
            free(buffer);
            exit(1);
        }

        // Write initial data
        for (long pos = 0; pos < total_bytes; pos += config->io_size) {
            if (write(init_fd, buffer, config->io_size) != config->io_size) {
                perror("Failed to initialize file");
                close(init_fd);
                free(buffer);
                exit(1);
            }
        }

        fsync(init_fd);
        close(init_fd);
    }

    // Open or create file
    // Try to open with O_DIRECT first
    flags |= config->is_write ? O_RDWR | O_CREAT : O_RDONLY;
    fd = open(config->device, flags, 0644);

    // If O_DIRECT fails, try without it
    if (fd < 0 && errno == EINVAL) {
        fprintf(stderr, "Warning: O_DIRECT not supported, falling back to buffered I/O\n");
        flags &= ~O_DIRECT;  // Remove O_DIRECT flag
        fd = open(config->device, flags, 0644);
    }

    if (fd < 0) {
        perror("Failed to open file");
        free(buffer);
        exit(1);
    }

    // Verify I/O size alignment
    if (config->io_size % 512 != 0) {
        fprintf(stderr, "Warning: I/O size %d is not aligned to 512 bytes\n", config->io_size);
    }

    // Print actual flags being used
    // printf("Using flags: 0x%x\n", flags);

    // If writing, pre-allocate file space
    if (config->is_write) {
        if (fallocate(fd, 0, 0, total_bytes) < 0) {
            perror("Failed to allocate file space");
            close(fd);
            free(buffer);
            exit(1);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    while (current_pos < total_bytes) {
        ssize_t bytes;
        off_t offset;

        if (config->is_random) {
            offset = (random() % (config->range / config->io_size)) * config->io_size;
            if (lseek(fd, offset, SEEK_SET) < 0) {
                perror("lseek failed");
                close(fd);
                free(buffer);
                exit(1);}
        } else {
            offset = current_pos;
            if (lseek(fd, offset, SEEK_SET) < 0) {
                perror("lseek failed");
                close(fd);
                free(buffer);
                exit(1);
            }
        }

        if (config->is_write) {
            bytes = write(fd, buffer, config->io_size);
        } else {
            bytes = read(fd, buffer, config->io_size);
        }

        if (bytes != config->io_size) {
            perror("I/O operation failed");
            close(fd);
            free(buffer);
            exit(1);
        }

        current_pos += config->io_size + config->stride_size;
    }

    // Ensure writes are committed to disk
    if (config->is_write) {
        fsync(fd);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    close(fd);
    free(buffer);

    double seconds = get_time_diff(start, end);
    return (double)total_bytes / (seconds * MB); // Return throughput in MB/s
}

int main(int argc, char* argv[]) {
    benchmark_config config = {
            .device = "benchmark_temp_file", // Default filename
            .io_size = 4 * KB,
            .stride_size = 0,
            .range = GB,
            .is_write = 0,
            .is_random = 0,
            .num_iterations = 5,
            .output_file = NULL
    };

    int opt;
    while ((opt = getopt(argc, argv, "d:s:t:r:wRn:o:h")) != -1) {
        switch (opt) {
            case 'd':
                config.device = optarg;
                break;
            case 's':
                config.io_size = atoi(optarg);
                break;
            case 't':
                config.stride_size = atoi(optarg);
                break;
            case 'r':
                config.range = atol(optarg);
                break;
            case 'w':
                config.is_write = 1;
                break;
            case 'R':
                config.is_random = 1;
                break;
            case 'n':
                config.num_iterations = atoi(optarg);
                break;
            case 'o':
                config.output_file = optarg;
                break;
            case 'h':
            default:
                print_usage();
        }
    }

    // Verify I/O size is reasonable
    if (config.io_size < 512 || config.io_size % 512 != 0) {
        fprintf(stderr, "Warning: I/O size should be at least 512 bytes and a multiple of 512\n");
        config.io_size = ((config.io_size + 511) / 512) * 512;
        printf("Adjusted I/O size to: %d bytes\n", config.io_size);
    }


    // Initialize random number generator
    srandom(time(NULL));

    // Open CSV file if specified
    FILE* csv_fp = NULL;
    if (config.output_file) {
        csv_fp = fopen(config.output_file, "a");
        if (!csv_fp) {
            perror("Failed to open output file");
            exit(1);
        }
        write_csv_header(csv_fp);
    }

    // Run benchmarks
    double total_throughput = 0;
    printf("\nRunning benchmark with following configuration:\n");
    printf("Device: %s\n", config.device);
    printf("I/O Size: %d bytes\n", config.io_size);
    printf("Stride Size: %d bytes\n", config.stride_size);
    printf("Operation: %s\n", config.is_write ? "Write" : "Read");
    printf("Pattern: %s\n", config.is_random ? "Random" : "Sequential");
    printf("Iterations: %d\n\n", config.num_iterations);

    for (int i = 0; i < config.num_iterations; i++) {
        double throughput = run_benchmark(&config);
        total_throughput += throughput;
        printf("Iteration %d: %.2f MB/s\n", i + 1, throughput);
        if (csv_fp) {
            write_csv_result(csv_fp, &config, i + 1, throughput);
        }
    }

    double avg_throughput = total_throughput / config.num_iterations;
    printf("\nAverage throughput: %.2f MB/s\n", avg_throughput);

    if (csv_fp) {
        fclose(csv_fp);
    }

    // Clean up the temporary file
    if (unlink(config.device) < 0) {
        perror("Warning: Failed to remove temporary file");
    }

    return 0;
}