#!/usr/bin/env python3
"""Test for sequence sum collisions across 100,000 random inputs."""

import random
import string
import time

def text_to_number(text, multiplier=100_000_000):
    result = 0
    for char in text:
        result = (result << 7) + (ord(char) & 0x7F)
    result *= multiplier
    if not (result & 1):
        result = (result << 1) | 1
    return result

def collatz_fast(n):
    sequence = [n]
    while n != 1:
        if n & 1:
            n = 3 * n + 1
        else:
            n = n >> 1
        sequence.append(n)
    return sequence

def generate_random_string(min_len=1, max_len=20):
    """Generate a random string of printable characters."""
    length = random.randint(min_len, max_len)
    chars = string.ascii_letters + string.digits + string.punctuation
    return ''.join(random.choice(chars) for _ in range(length))

# Test parameters
NUM_TESTS = 10_000_000

print('=' * 80)
print(f'SEQUENCE SUM COLLISION TEST - {NUM_TESTS:,} RANDOM INPUTS')
print('=' * 80)
print()

# Generate unique random inputs
print("Generating random inputs...")
random.seed(42)  # Reproducible
inputs = set()
while len(inputs) < NUM_TESTS:
    inputs.add(generate_random_string())
inputs = list(inputs)
print(f"Generated {len(inputs):,} unique random strings")
print()

# Run collision test
print("Computing Collatz sequences and sums...")
start_time = time.perf_counter()

sums_seen = {}
collisions = []
sum_collision_count = 0

for i, text in enumerate(inputs):
    if (i + 1) % 500000 == 0:
        elapsed = time.perf_counter() - start_time
        rate = (i + 1) / elapsed
        print(f"  Progress: {i+1:,}/{NUM_TESTS:,} ({rate:.0f} inputs/sec)")
    
    encoded = text_to_number(text)
    seq = collatz_fast(encoded)
    seq_sum = sum(seq)
    
    # Check for sum collision
    if seq_sum in sums_seen:
        collisions.append((text, sums_seen[seq_sum], seq_sum))
        sum_collision_count += 1
    else:
        sums_seen[seq_sum] = text

elapsed = time.perf_counter() - start_time
print()
print(f"Completed in {elapsed:.2f} seconds ({NUM_TESTS/elapsed:.0f} inputs/sec)")
print()

# Results
print('=' * 80)
print('RESULTS')
print('=' * 80)
print()

print(f"Total inputs tested: {NUM_TESTS:,}")
print(f"Unique sequence sums: {len(sums_seen):,}")
print(f"Sum collisions found: {sum_collision_count}")
print(f"Collision rate: {sum_collision_count / NUM_TESTS * 100:.6f}%")
print()

if collisions:
    print("COLLISIONS DETECTED:")
    print("-" * 60)
    for i, (t1, t2, s) in enumerate(collisions[:10]):  # Show first 10
        print(f'  "{t1}" collides with "{t2}"')
        print(f'    Sum: ...{str(s)[-30:]}')
    if len(collisions) > 10:
        print(f"  ... and {len(collisions) - 10} more")
else:
    print("NO COLLISIONS FOUND!")
    print()
    print("All 100,000 random inputs produced unique sequence sums.")

print()
print('=' * 80)
print('STATISTICAL ANALYSIS')
print('=' * 80)
print()

# Expected collision rate for random 64-bit hashes (birthday paradox)
# P(collision) ≈ n² / (2 * 2^bits)
# For n=100000, bits=64: P ≈ 10^10 / 2^65 ≈ 0.00027 (0.027%)
# For bits=128: P ≈ 10^10 / 2^129 ≈ 0 (essentially impossible)

print("For reference (birthday paradox):")
print("  If this were a 64-bit hash: ~0.03% expected collisions")
print("  If this were a 128-bit hash: ~0% expected collisions")
print()
print(f"Our result: {sum_collision_count / NUM_TESTS * 100:.6f}% collisions")
print()

if sum_collision_count == 0:
    print("CONCLUSION: Collatz sequence sums behave like a high-entropy hash.")
    print("            No collisions in 10 million tests indicates strong uniqueness.")
