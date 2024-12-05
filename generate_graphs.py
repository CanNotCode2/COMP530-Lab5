import pandas as pd
import matplotlib.pyplot as plt
# import seaborn as sns
import os

def load_data(filepath):
    return pd.read_csv(filepath)

def plot_io_size(seq_data, random_data, operation, device, output_dir):
    plt.figure(figsize=(12, 6))

    # Sequential I/O
    means = seq_data.groupby('io_size')['mean'].last()
    ci95 = seq_data.groupby('io_size')['ci95'].last()
    plt.errorbar(means.index, means.values, yerr=ci95.values,
                 fmt='o-', label='Sequential')

    # Random I/O
    means = random_data.groupby('io_size')['mean'].last()
    ci95 = random_data.groupby('io_size')['ci95'].last()
    plt.errorbar(means.index, means.values, yerr=ci95.values,
                 fmt='o-', label='Random')

    plt.xscale('log')
    plt.yscale('log')
    plt.xlabel('I/O Size (bytes)')
    plt.ylabel('Throughput (MB/s)')
    plt.title(f'I/O Size vs Throughput - {device} {operation}')
    plt.grid(True)
    plt.legend()

    plt.savefig(f'{output_dir}/io_size_{device}_{operation}.png')
    plt.close()

def plot_stride(stride_data, operation, device, output_dir):
    plt.figure(figsize=(12, 6))

    # Select representative I/O sizes
    io_sizes = [4096, 65536, 524288, 1048576, 10485760]

    for io_size in io_sizes:
        data = stride_data[stride_data['io_size'] == io_size]
        if not data.empty:
            means = data.groupby('stride_size')['mean'].last()
            ci95 = data.groupby('stride_size')['ci95'].last()
            plt.errorbar(means.index, means.values, yerr=ci95.values,
                         label=f'IO Size={io_size/1024:.0f}KB')

    plt.xscale('log')
    plt.yscale('log')
    plt.xlabel('Stride Size (bytes)')
    plt.ylabel('Throughput (MB/s)')
    plt.title(f'Stride Size vs Throughput - {device} {operation}')
    plt.grid(True)
    plt.legend()

    plt.savefig(f'{output_dir}/stride_{device}_{operation}.png')
    plt.close()

def process_device(device_dir, device_name, output_dir):
    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    # Load all data files
    seq_read = load_data(f'{device_dir}/sequential_size_read.csv')
    seq_write = load_data(f'{device_dir}/sequential_size_write.csv')
    random_read = load_data(f'{device_dir}/random_read.csv')
    random_write = load_data(f'{device_dir}/random_write.csv')
    stride_read = load_data(f'{device_dir}/stride_read.csv')
    stride_write = load_data(f'{device_dir}/stride_write.csv')

    # Generate all plots
    plot_io_size(seq_read, random_read, 'read', device_name, output_dir)
    plot_io_size(seq_write, random_write, 'write', device_name, output_dir)
    plot_stride(stride_read, 'read', device_name, output_dir)
    plot_stride(stride_write, 'write', device_name, output_dir)

def main():
    # Set style
    # plt.style.use('seaborn')

    # Create plots directory
    output_dir = 'benchmark_plots'
    os.makedirs(output_dir, exist_ok=True)

    # Process HDD data
    process_device('hdd_benchmark_results', 'HDD', output_dir)

    # Process SSD data
    process_device('ssd_benchmark_results', 'SSD', output_dir)

if __name__ == "__main__":
    main()