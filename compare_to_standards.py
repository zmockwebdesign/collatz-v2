#!/usr/bin/env python3
"""
Compare Collatz hash to standard algorithms - runnable benchmark.

Usage: python3 compare_to_standards.py
"""

import hashlib
import time
from collatz_hash import collatz_hash_256, collatz_hash_512

def benchmark(name, func, text, iters):
    start = time.perf_counter()
    for _ in range(iters):
        func(text)
    elapsed = time.perf_counter() - start
    ops = iters / elapsed
    return ops, elapsed / iters * 1000  # ops/sec, ms/op

def main():
    text = "sample input 12"
    print("=" * 70)
    print("COLLATZ vs STANDARD HASHING ALGORITHMS")
    print("=" * 70)
    print(f"Input: '{text}'")
    print()

    results = []

    # Collatz (fewer iters - slower)
    try:
        ops, ms = benchmark("Collatz-256", collatz_hash_256, text, 200)
        results.append(("Collatz-256", ops, ms, "Pure math, no libs"))
    except Exception as e:
        print(f"Collatz-256: Error - {e}")

    try:
        ops, ms = benchmark("Collatz-512", collatz_hash_512, text, 200)
        results.append(("Collatz-512", ops, ms, "Pure math, no libs"))
    except Exception as e:
        print(f"Collatz-512: Error - {e}")

    # Standard hashes (many iters - fast)
    for name, func in [
        ("MD5", lambda t: hashlib.md5(t.encode()).hexdigest()),
        ("SHA-1", lambda t: hashlib.sha1(t.encode()).hexdigest()),
        ("SHA-256", lambda t: hashlib.sha256(t.encode()).hexdigest()),
        ("SHA-512", lambda t: hashlib.sha512(t.encode()).hexdigest()),
        ("SHA3-256", lambda t: hashlib.sha3_256(t.encode()).hexdigest()),
    ]:
        ops, ms = benchmark(name, func, text, 50000)
        results.append((name, ops, ms, "Standard crypto"))

    # Sort by speed (slowest first)
    results.sort(key=lambda x: x[1])

    print(f"{'Algorithm':<15} {'Ops/Sec':>12} {'Ms/Op':>10} {'Notes':<25}")
    print("-" * 65)
    for name, ops, ms, notes in results:
        if ops >= 1000:
            speed_str = f"{ops:,.0f}"
        else:
            speed_str = f"{ops:.1f}"
        print(f"{name:<15} {speed_str:>12} {ms:>8.2f}   {notes:<25}")

    print()
    print("SECURITY LEVEL (output size):")
    print("  MD5:     128 bits (deprecated)")
    print("  SHA-1:   160 bits (deprecated)")
    print("  SHA-256: 256 bits (NIST standard)")
    print("  SHA-512: 512 bits (NIST standard)")
    print("  Collatz: 256/512 bits (matches SHA, novel approach)")
    print()
    print("See HASH_ALGORITHM_COMPARISON.md for full analysis.")
    print("=" * 70)

if __name__ == "__main__":
    main()
