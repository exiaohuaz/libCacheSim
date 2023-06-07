import subprocess
import re

trace = "vscsi"
eviction_algs = ["fifo", "lru", "clock", "lfu", "arc", "slru", "tinylfu"]
cache_sizes = ["1mb", "16mb", "256mb", "8gb"]

for alg in eviction_algs:
    miss_ratios = []
    throughputs = []
    for size in cache_sizes:
        out = subprocess.check_output(['./cachesim', '../data/trace.' + trace, trace, alg, size])
        match = re.search('miss ratio (\d*[.,]?\d*)', str(out))
        if match:
            miss_ratios.append(float(match.group(1)))

        match = re.search('throughput (\d*[.,]?\d*)', str(out))
        if match:
            throughputs.append(float(match.group(1)))

    print(alg, "------------------------")
    for i in range(len(cache_sizes)):
        print(cache_sizes[i], ":", "miss ratio", miss_ratios[i], "throughput", throughputs[i])