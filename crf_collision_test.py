#!/usr/bin/env python3
"""
Test: Build 100,000 CRF output sequences, remove pairing at position 2,
check for collisions (identical output sequences).
"""
from crf_v1 import v2
from collections import Counter

NUM_ORBITS = 10_000_000
MAX_STEPS = 1000
MAX_N = 10**12
REMOVE_POSITION = 2


def get_crf_sequence(n0: int):
    """
    Build the full sequence of k values (pairings) for orbit starting at odd n0.
    Returns list of k_i where k_i = v2(3*n_i + 1).
    """
    km2, km1 = 0, 0
    sequence = []
    n = n0

    for _ in range(MAX_STEPS):
        t = 3 * n + 1
        k = v2(t)
        sequence.append(k)

        if n == 1 or n > MAX_N:
            break
        n = t >> k
        km2, km1 = km1, k

    return sequence


def main():
    # Collect output sequences (with position 2 removed) from 100k orbits
    output_sequences = []
    orbits_processed = 0
    orbits_skipped = 0  # too short to have position 2

    for n0 in range(1, 2 * NUM_ORBITS + 100, 2):  # odd integers
        if orbits_processed >= NUM_ORBITS:
            break

        seq = get_crf_sequence(n0)
        if len(seq) <= REMOVE_POSITION:
            orbits_skipped += 1
            continue

        # Remove pairing at position 2 (0-indexed)
        seq_without_pos2 = tuple(
            seq[i] for i in range(len(seq)) if i != REMOVE_POSITION
        )
        output_sequences.append((n0, seq_without_pos2))
        orbits_processed += 1

    # Count collisions
    seq_counts = Counter(s for _, s in output_sequences)
    collisions = [(seq, count) for seq, count in seq_counts.items() if count > 1]

    # Report to file
    output_path = "crf_collision_test_results.txt"
    with open(output_path, "w") as f:
        f.write(f"Orbits processed: {orbits_processed}\n")
        f.write(f"Orbits skipped (too short): {orbits_skipped}\n")
        f.write(f"Unique output sequences: {len(seq_counts)}\n")
        f.write(f"Collisions found: {len(collisions)}\n")
        f.write("\n")

        if collisions:
            f.write("Sample collisions (output sequence -> count, example inputs):\n")
            for i, (seq, count) in enumerate(collisions[:5]):
                example_inputs = [n0 for n0, s in output_sequences if s == seq][:3]
                f.write(f"  [{i+1}] count={count}, example n0: {example_inputs}\n")
                f.write(f"       seq (first 20): {seq[:20]}...\n")
        else:
            f.write("No collisions: all 100,000 output sequences are unique.\n")

    print(f"Results written to {output_path}")


if __name__ == "__main__":
    main()
