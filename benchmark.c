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
#include <math.h>

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

typedef struct {
    double sum;
    double sum_squared;
    int count;
    double min;
    double max;
    double variance;
} stats_t;

double get_mean(stats_t* stats) {
    return stats->sum / stats->count;
}

double get_stddev(stats_t* stats) {
    double mean = get_mean(stats);
    return sqrt((stats->sum_squared / stats->count) - (mean * mean));
}

double get_confidence_interval_95(stats_t* stats) {
    return 1.96 * get_stddev(stats) / sqrt(stats->count);
}

void update_stats(stats_t* stats, double value) {
    stats->sum += value;
    stats->sum_squared += value * value;
    stats->count++;
    if (stats->count == 1) {
        stats->min = stats->max = value;
    } else {
        stats->min = fmin(stats->min, value);
        stats->max = fmax(stats->max, value);
    }

    if (stats->count > 1) {
        double mean = get_mean(stats);
        stats->variance = ((stats->count - 2) * stats->variance + (value - mean) * (value - mean)) / (stats->count - 1); // Welford's algorithm
    } else if (stats->count == 1) {
        stats->variance = 0.0; // Initialize variance
    }
}

double get_time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / BILLION;
}

void print_usage() {
    printf("Usage: benchmark [options]\n");
    printf("Options:\n");
    printf("  -d <device>      Device to test (e.g., /dev/sda2)\n");
    printf("  -s <size>        I/O size in bytes (4KB-100MB)\n");
    printf("  -t <stride>      Stride size in bytes (0-100MB)\n");
    printf("  -r <range>       Range for random I/Os in bytes (up to 1GB)\n");
    printf("  -w               Perform write test (default is read)\n");
    printf("  -R               Perform random I/Os (default is sequential)\n");
    printf("  -n <iterations>  Number of iterations (default: 5)\n");
    printf("  -o <file>        Output CSV file\n");
    exit(1);
}

void write_csv_header(FILE* fp) {
    fprintf(fp, "operation,io_size,stride_size,is_random,iteration,throughput,mean,stddev,ci95,variance\n");
}

void write_csv_result(FILE* fp, benchmark_config* config, int iteration,
                      double throughput, stats_t* stats) {
    fprintf(fp, "%s,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f\n",
            config->is_write ? "write" : "read",
            config->io_size,
            config->stride_size,
            config->is_random,
            iteration,
            throughput,
            get_mean(stats),
            get_stddev(stats),
            get_confidence_interval_95(stats),
            stats->variance);
}

long get_random_offset(long range, int io_size) {
    if (range % 4096 != 0) {
        range = (range / 4096) * 4096; // Align range
    }
    long offset = (random() % (range / io_size)) * io_size;
    return offset & ~(4096 - 1); // Align to 4KB boundary
}

void validate_config(benchmark_config* config) {
    if (config->io_size < 4*KB || config->io_size > 100*MB) {
        fprintf(stderr, "Error: I/O size must be between 4KB and 100MB\n");
        exit(1);
    }

    if (config->stride_size < 0 || config->stride_size > 100*MB) {
        fprintf(stderr, "Error: Stride size must be between 0 and 100MB\n");
        exit(1);
    }

    if (config->range < config->io_size || config->range > GB) {
        fprintf(stderr, "Error: Range must be between I/O size and 1GB\n");
        exit(1);
    }

    if (config->num_iterations < 1) {
        fprintf(stderr, "Error: Number of iterations must be positive\n");
        exit(1);
    }
}

#include <sys/stat.h> // For stat()

void prepare_test_file(const char* filename, size_t size) {
    struct stat st;

    // Check if the file already exists
    if (stat(filename, &st) == 0) {
        // If the file exists and is large enough, reuse it
        if (st.st_size >= size) {
            printf("Reusing existing test file: %s\n", filename);
            return;
        }
    }

    printf("Creating test file: %s\n", filename);

    // Create or truncate the file to the specified size
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        perror("Failed to create test file");
        exit(1);
    }

    // Set the file size
    if (ftruncate(fileno(fp), size) != 0) {
        perror("Failed to set file size");
        fclose(fp);
        exit(1);
    }

    fclose(fp);
    printf("Test file created successfully: %s\n", filename);
}

void cleanup_test_file(const char* filename) {
    if (remove(filename) != 0) {
        perror("Failed to remove test file");
    } else {
        printf("Test file cleaned up: %s\n", filename);
    }
}

void cleanup(int fd, void* buffer, FILE* csv_fp) {
    if (fd >= 0) close(fd);
    if (buffer) {
        free(buffer);
        buffer = NULL;
    }
    if (csv_fp) {
        fclose(csv_fp);
        csv_fp = NULL;
    }
}

double run_benchmark(benchmark_config* config) {
    char* buffer;
    int fd;
    struct timespec start, end;
    long total_bytes = 0;
    long current_pos = 0;

    if (posix_memalign((void**)&buffer, 4096, config->io_size) != 0) {
        perror("posix_memalign failed");
        exit(1);
    }

    int flags = O_DIRECT | O_DSYNC | O_RDONLY;
    fd = open(config->device, flags);
    if (fd < 0) {
        if (errno == EINVAL) {
            fprintf(stderr, "O_DIRECT is not supported on this file system\n");
        }
        perror("Failed to open device");
        free(buffer);
        exit(1);
    }

    int file_flags = fcntl(fd, F_GETFL);
    if (!(file_flags & O_DIRECT)) {
        fprintf(stderr, "O_DIRECT is not set!\n");
        exit(1);
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    while (total_bytes < GB) {
        ssize_t bytes = read(fd, buffer, config->io_size);
        if (bytes != config->io_size) {
            fprintf(stderr, "Read failed: expected %d bytes, got %zd bytes\n", config->io_size, bytes);
            exit(1);
        }
        total_bytes += bytes;
        current_pos += bytes;
        if (current_pos >= GB) {
            if (lseek(fd, 0, SEEK_SET) < 0) {
                perror("lseek failed");
                exit(1);
            }
            current_pos = 0;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    close(fd);
    free(buffer);

    double seconds = get_time_diff(start, end);
    return (double)total_bytes / (seconds * MB); // Return throughput in MB/s
}

void init_config(benchmark_config* config) {
    config->device = NULL;
    config->io_size = 4 * KB;
    config->stride_size = 0;
    config->range = GB;
    config->is_write = 0;
    config->is_random = 0;
    config->num_iterations = 5;
    config->output_file = NULL;
}

int main(int argc, char* argv[]) {
    benchmark_config config;
    init_config(&config);

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

    if (!config.device) {
        fprintf(stderr, "Error: Device parameter (-d) is required\n");
        print_usage();
    }

    validate_config(&config);

    // Initialize random number generator
    srandom(time(NULL));

    // Open CSV file if specified
    FILE* csv_fp = NULL;
    if (config.output_file) {
        // Check if the file exists first
        if (access(config.output_file, F_OK) == -1) {
            // File doesn't exist, create it and write the header
            csv_fp = fopen(config.output_file, "w"); // Use "w" to create/truncate
            if (!csv_fp) {
                perror("Failed to create output file");
                exit(1);
            }
            write_csv_header(csv_fp);
        } else {
            // File exists, open it in append mode
            csv_fp = fopen(config.output_file, "a");
            if (!csv_fp) {
                perror("Failed to open output file");
                exit(1);
            }
        }
    }

    // Step 1: Prepare the test file (create it if necessary)
    prepare_test_file(config.device, 1 * GB); // 1 GB file

    printf("Starting benchmark...\n");

    // Initialize statistics
    stats_t stats = {0};

    printf("\nRunning benchmark with following configuration:\n");
    printf("Device: %s\n", config.device);
    printf("I/O Size: %d bytes\n", config.io_size);
    printf("Stride Size: %d bytes\n", config.stride_size);
    printf("Operation: %s\n", config.is_write ? "Write" : "Read");
    printf("Pattern: %s\n", config.is_random ? "Random" : "Sequential");
    printf("Iterations: %d\n\n", config.num_iterations);

    for (int i = 0; i < config.num_iterations; i++) {
        double throughput = run_benchmark(&config);
        update_stats(&stats, throughput);

        printf("Iteration %d: %.2f MB/s\n", i + 1, throughput);
        if (csv_fp) {
            write_csv_result(csv_fp, &config, i + 1, throughput, &stats);
        }

        // Add a small delay between iterations
        usleep(100000);  // 100ms delay
    }

    double mean = get_mean(&stats);
    double stddev = get_stddev(&stats);
    double ci95 = get_confidence_interval_95(&stats);

    printf("\nResults Summary:\n");
    printf("Average throughput: %.2f MB/s\n", mean);
    printf("Standard deviation: %.2f MB/s\n", stddev);
    printf("95%% Confidence Interval: %.2f ± %.2f MB/s\n", mean, ci95);
    printf("Min throughput: %.2f MB/s\n", stats.min);
    printf("Max throughput: %.2f MB/s\n", stats.max);

    if (csv_fp) {
        fclose(csv_fp);
    }

    // Step 3: Clean up the test file after the benchmark
    cleanup_test_file(config.device);

    return 0;
}