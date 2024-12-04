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
    char* device;
    int io_size;
    int stride_size;
    long range;
    int is_write;
    int is_random;
    int num_iterations;
    char* output_file;
} benchmark_config;

double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void validate_config(benchmark_config* config) {
    if (config->io_size % 4096 != 0) {
        fprintf(stderr, "Error: I/O size must be 4K aligned\n");
        exit(1);
    }
    if (config->stride_size % 4096 != 0) {
        fprintf(stderr, "Error: Stride size must be 4K aligned\n");
        exit(1);
    }
    if (config->range < config->io_size) {
        fprintf(stderr, "Error: Range must be larger than I/O size\n");
        exit(1);
    }
}

void write_csv_header(FILE* fp) {
    fprintf(fp, "operation,io_size,stride_size,is_random,iteration,throughput,mean,stddev,ci95\n");
}

void write_csv_result(FILE* fp, benchmark_config* config, int iteration,
                      double throughput, double mean, double stddev, double ci95) {
    fprintf(fp, "%s,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f\n",
            config->is_write ? "write" : "read",
            config->io_size,
            config->stride_size,
            config->is_random,
            iteration,
            throughput,
            mean,
            stddev,
            ci95);
}

double run_benchmark(benchmark_config* config) {
    char* buffer;
    int fd;
    long total_bytes = 0;
    long current_pos = 0;

    validate_config(config);

    if (posix_memalign((void**)&buffer, 4096, config->io_size) != 0) {
        perror("posix_memalign failed");
        exit(1);
    }

    int flags = O_DIRECT | (config->is_write ? O_RDWR : O_RDONLY);
    fd = open(config->device, flags);
    if (fd < 0) {
        perror("Failed to open device");
        free(buffer);
        exit(1);
    }

    double start = get_time();

    while (total_bytes < GB) {
        if (config->is_random) {
            long max_pos = config->range - config->io_size;
            current_pos = (random() % (max_pos / 4096)) * 4096; // Ensure 4K alignment
        }

        if (lseek(fd, current_pos, SEEK_SET) < 0) {
            perror("lseek failed");
            close(fd);
            free(buffer);
            exit(1);
        }

        ssize_t bytes;
        if (config->is_write) {
            bytes = write(fd, buffer, config->io_size);
        } else {
            bytes = read(fd, buffer, config->io_size);
        }

        if (bytes != config->io_size) {
            fprintf(stderr, "I/O operation failed: expected %d bytes, got %zd bytes\n", config->io_size, bytes);
            close(fd);
            free(buffer);
            exit(1);
        }

        total_bytes += config->io_size;
        if (!config->is_random) {
            current_pos += config->io_size + config->stride_size;
            if (current_pos + config->io_size > config->range) {
                current_pos = 0;
            }
        }
    }

    if (config->is_write) {
        fsync(fd);
    }

    double end = get_time();

    close(fd);
    free(buffer);

    return (double)total_bytes / (end - start) / MB;
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
}

int main(int argc, char* argv[]) {
    benchmark_config config = {
            .device = NULL,
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
            case 'd': config.device = optarg; break;
            case 's': config.io_size = atoi(optarg); break;
            case 't': config.stride_size = atoi(optarg); break;
            case 'r': config.range = atol(optarg); break;
            case 'w': config.is_write = 1; break;
            case 'R': config.is_random = 1; break;
            case 'n': config.num_iterations = atoi(optarg); break;
            case 'o': config.output_file = optarg; break;
            case 'h':
            default: print_usage(); exit(1);
        }
    }

    if (!config.device) {
        fprintf(stderr, "Error: Device parameter (-d) is required\n");
        print_usage();
        exit(1);
    }

    srandom(time(NULL));

    printf("Running benchmark with following configuration:\n");
    printf("Device: %s\n", config.device);
    printf("I/O Size: %d bytes\n", config.io_size);
    printf("Stride Size: %d bytes\n", config.stride_size);
    printf("Range: %ld bytes\n", config.range);
    printf("Operation: %s\n", config.is_write ? "Write" : "Read");
    printf("Pattern: %s\n", config.is_random ? "Random" : "Sequential");
    printf("Iterations: %d\n\n", config.num_iterations);

    FILE* csv_fp = NULL;
    if (config.output_file) {
        csv_fp = fopen(config.output_file, "w");
        if (!csv_fp) {
            perror("Failed to open output file");
            exit(1);
        }
        write_csv_header(csv_fp);
    }

    double results[config.num_iterations];
    double sum = 0, sum_squared = 0;

    for (int i = 0; i < config.num_iterations; i++) {
        results[i] = run_benchmark(&config);
        sum += results[i];
        sum_squared += results[i] * results[i];
        printf("Iteration %d: %.2f MB/s\n", i + 1, results[i]);

        if (csv_fp) {
            double mean = sum / (i + 1);
            double variance = (sum_squared / (i + 1)) - (mean * mean);
            double stddev = sqrt(variance);
            double ci_95 = 1.96 * stddev / sqrt(i + 1);
            write_csv_result(csv_fp, &config, i + 1, results[i], mean, stddev, ci_95);
        }
    }

    double mean = sum / config.num_iterations;
    double variance = (sum_squared / config.num_iterations) - (mean * mean);
    double stddev = sqrt(variance);
    double ci_95 = 1.96 * stddev / sqrt(config.num_iterations);

    printf("\nResults Summary:\n");
    printf("Average throughput: %.2f MB/s\n", mean);
    printf("Standard deviation: %.2f MB/s\n", stddev);
    printf("95%% Confidence Interval: %.2f ± %.2f MB/s\n", mean, ci_95);

    if (csv_fp) {
        fclose(csv_fp);
        printf("CSV output written to %s\n", config.output_file);
    }

    return 0;
}