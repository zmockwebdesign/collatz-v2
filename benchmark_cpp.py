#!/usr/bin/env python3
"""
Benchmark: Python vs C++ Collatz Hash implementations.

Run: python3 benchmark_cpp.py
Requires: collatz_hash binary (make collatz_hash)
"""
import subprocess
import sys
import time

def benchmark_python(text, iters=1000):
    from collatz_hash import collatz_hash_256
    start = time.perf_counter()
    for _ in range(iters):
        collatz_hash_256(text)
    return time.perf_counter() - start, iters

def benchmark_cpp(text, iters=10000):
    try:
        result = subprocess.run(
            ["./collatz_hash", text, str(iters)],
            capture_output=True, text=True, cwd=".",
            timeout=60
        )
        if result.returncode != 0:
            return None
        # Parse "X.XX ms/op (Y ops/sec)"
        out = result.stdout
        for line in out.split('\n'):
            if 'ms/op' in line:
                return None  # C++ prints its own timing
        return None  # We need to run without benchmark mode to get timing
    except FileNotFoundError:
        return None
    except Exception as e:
        print(f"C++ benchmark failed: {e}")
        return None

def main():
    text = "sample input 12"
    py_iters = 500
    cpp_iters = 5000

    print("=" * 60)
    print("COLLATZ HASH: Python vs C++ Speed Comparison")
    print("=" * 60)
    print(f"Input: '{text}'")
    print()

    # Python
    py_time, py_n = benchmark_python(text, py_iters)
    py_ms = py_time / py_n * 1000
    py_ops = py_n / py_time
    print(f"Python: {py_ms:.3f} ms/op  ({py_ops:,.0f} ops/sec)  [{py_n} iters]")

    # C++
    try:
        start = time.perf_counter()
        subprocess.run(["./collatz_hash", text, str(cpp_iters)],
                       capture_output=True, timeout=60, cwd=".")
        cpp_time = time.perf_counter() - start
        cpp_ms = cpp_time / cpp_iters * 1000
        cpp_ops = cpp_iters / cpp_time
        print(f"C++:    {cpp_ms:.3f} ms/op  ({cpp_ops:,.0f} ops/sec)  [{cpp_iters} iters]")
        print()
        speedup = cpp_ops / py_ops
        print(f"Speedup: C++ is {speedup:.1f}x faster than Python")
    except FileNotFoundError:
        print("C++:    (run 'make collatz_hash' first)")
    except Exception as e:
        print(f"C++:    Error - {e}")

    print()
    print("=" * 60)

if __name__ == "__main__":
    main()
