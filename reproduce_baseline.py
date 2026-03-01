#!/usr/bin/env python3
"""
Reproduce v1_baseline_test.csv using the CRF V1 logic.
"""
from crf_v1 import is_exceptional_v1

START = 1
END = 10_000_000
INTERVAL = 1_000_000
MAX_STEPS = 1000
MAX_N = 10**12
EXC_THRESHOLD = 0.4


def main():
    num_blocks = (END - START) // INTERVAL + 1
    block_orbits = [0] * num_blocks
    block_exc = [0] * num_blocks

    for n0 in range(1, END + 1, 2):  # odd integers only
        b = min((n0 - START) // INTERVAL, num_blocks - 1)
        exc = is_exceptional_v1(n0, MAX_STEPS, MAX_N, EXC_THRESHOLD)
        block_orbits[b] += 1
        if exc:
            block_exc[b] += 1

    print("end,orbits,exceptions,exception_fraction")
    cum_orbits = cum_exc = 0
    for i in range(num_blocks):
        cum_orbits += block_orbits[i]
        cum_exc += block_exc[i]
        bound = START + (i + 1) * INTERVAL
        if bound > END:
            bound = END
        frac = cum_exc / cum_orbits if cum_orbits else 0
        print(f"{bound},{cum_orbits},{cum_exc},{frac:.12g}")


if __name__ == "__main__":
    main()
