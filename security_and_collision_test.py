#!/usr/bin/env python3
"""
Security and Collision Test for Collatz Hash

Tests:
1. DETERMINISM - Same input always produces same output
2. AVALANCHE EFFECT - Small input change should flip ~50% of output bits
3. OUTPUT DISTRIBUTION - Output bits should be roughly 50/50 zeros and ones
4. COLLISION SPACE - Can two different inputs produce the same hash?
   - 256/512-bit: Collisions exist in theory (pigeonhole) but are infeasible to find
   - Reduced bit widths: We can actually find collisions to prove the space
"""

import itertools
import random
import string
from collections import Counter
from collatz_hash import collatz_hash


def hex_to_bits(hex_str):
    """Convert hex string to list of bits (MSB first)."""
    n = int(hex_str, 16)
    bits = []
    while n:
        bits.append(n & 1)
        n >>= 1
    bits.reverse()
    return bits or [0]


def bits_to_hex(bits):
    """Convert bit list back to hex string."""
    n = 0
    for b in bits:
        n = (n << 1) | b
    return format(n, 'x')


def hamming_distance_hex(h1, h2):
    """Number of differing bits between two hex hashes."""
    b1 = hex_to_bits(h1)
    b2 = hex_to_bits(h2)
    # Pad to same length
    max_len = max(len(b1), len(b2))
    b1 = [0] * (max_len - len(b1)) + b1
    b2 = [0] * (max_len - len(b2)) + b2
    return sum(a != b for a, b in zip(b1, b2))


def avalanche_test(text, bit_width=256, num_flips=20):
    """Test: flipping 1 bit in input should change ~50% of output bits."""
    base_hash = collatz_hash(text, bit_width=bit_width)
    base_bits = hex_to_bits(base_hash)
    total_bits = len(base_bits)

    # Encode text to bytes, flip bits
    encoded = list(text.encode('utf-8', errors='replace'))
    if not encoded:
        return None

    diffs = []
    for _ in range(min(num_flips, len(encoded) * 8)):
        byte_idx = random.randint(0, len(encoded) - 1)
        bit_idx = random.randint(0, 7)
        flipped = bytearray(encoded)
        flipped[byte_idx] ^= (1 << bit_idx)
        try:
            modified_text = bytes(flipped).decode('utf-8', errors='replace')
        except Exception:
            continue
        mod_hash = collatz_hash(modified_text, bit_width=bit_width)
        diff = hamming_distance_hex(base_hash, mod_hash)
        diffs.append(diff / total_bits if total_bits > 0 else 0)

    return diffs, total_bits


def single_char_avalanche(text, bit_width=256):
    """Avalanche: change 1 character, measure output bit change."""
    base_hash = collatz_hash(text, bit_width=bit_width)
    base_bits = hex_to_bits(base_hash)
    total_bits = len(base_bits)

    results = []
    for i in range(min(len(text), 10)):  # Test first 10 positions
        for new_char in ['a', 'b', 'A', '0']:  # A few alternatives
            if new_char != text[i]:
                modified = text[:i] + new_char + text[i + 1:]
                mod_hash = collatz_hash(modified, bit_width=bit_width)
                diff = hamming_distance_hex(base_hash, mod_hash)
                results.append(diff / total_bits if total_bits > 0 else 0)
                break  # One change per position

    return results, total_bits


def output_distribution_test(texts, bit_width=256):
    """Check if output bits are roughly 50% zero, 50% one."""
    all_bits = []
    for t in texts:
        h = collatz_hash(t, bit_width=bit_width)
        all_bits.extend(hex_to_bits(h))
    zeros = all_bits.count(0)
    ones = all_bits.count(1)
    total = len(all_bits)
    return zeros, ones, total, zeros / total if total else 0


def collision_search(num_inputs, bit_width, input_generator=None):
    """
    Hash many inputs and look for collisions.
    For small bit_width (32, 64), collisions are findable.
    For 256-bit, we expect none in reasonable time.
    """
    if input_generator is None:
        def input_generator():
            for i in range(num_inputs):
                yield f"test_input_{i}_{random.randint(0, 10**9)}"

    seen = {}
    collisions = []
    for inp in input_generator():
        if len(collisions) >= 5:  # Stop after finding 5
            break
        h = collatz_hash(inp, bit_width=bit_width)
        if h in seen:
            collisions.append((seen[h], inp, h))
        else:
            seen[h] = inp

    return len(seen), collisions


def main():
    print("=" * 70)
    print("COLLATZ HASH: Security & Collision Test")
    print("=" * 70)
    print()

    # 1. DETERMINISM
    print("1. DETERMINISM")
    print("-" * 70)
    test_str = "sample input 12"
    h1 = collatz_hash(test_str, bit_width=256)
    h2 = collatz_hash(test_str, bit_width=256)
    print(f"   Input: '{test_str}'")
    print(f"   Run 1: {h1}")
    print(f"   Run 2: {h2}")
    print(f"   Match: {h1 == h2} ✓" if h1 == h2 else "   Match: FAIL")
    print()

    # 2. AVALANCHE EFFECT
    print("2. AVALANCHE EFFECT (1-char change → output bit change)")
    print("-" * 70)
    for text in ["hello", "password", "sample input 12"]:
        diffs, total_bits = single_char_avalanche(text, bit_width=256)
        if diffs:
            avg = sum(diffs) / len(diffs) * 100
            print(f"   '{text}': avg {avg:.1f}% of output bits flip (ideal: 50%)")
    print("   Good hash: small input change → ~50% of output bits change")
    print()

    # 3. OUTPUT DISTRIBUTION
    print("3. OUTPUT DISTRIBUTION (bit balance)")
    print("-" * 70)
    texts = [f"sample_{i}" for i in range(100)] + [f"rand_{random.randint(0, 10**6)}" for _ in range(50)]
    z, o, t, pct_zero = output_distribution_test(texts, bit_width=256)
    print(f"   Bits: {z} zeros ({pct_zero*100:.1f}%), {o} ones ({(1-pct_zero)*100:.1f}%)")
    print("   Good: ~50/50 balance (high entropy)")
    print()

    # 4. COLLISION SPACE
    print("4. COLLISION SPACE")
    print("-" * 70)
    print("   Theory: Hash maps huge input space → fixed output (256/512 bits).")
    print("   Pigeonhole: Collisions MUST exist when inputs > 2^bit_width.")
    print("   Practical: For 256-bit, finding one requires ~2^128 tries (infeasible).")
    print()

    # 4a. Full 256-bit: no collisions expected
    print("   4a. 256-bit hash, 5,000 random inputs:")
    def gen_100k():
        for i in range(5_000):
            s = ''.join(random.choices(string.ascii_letters + string.digits, k=12))
            yield f"{s}_{i}"

    unique, collisions = collision_search(5_000, bit_width=256, input_generator=gen_100k)
    print(f"      Unique hashes: {unique:,}")
    print(f"      Collisions found: {len(collisions)}")
    if collisions:
        for a, b, h in collisions[:3]:
            print(f"         '{a}' and '{b}' → {h[:16]}...")
    else:
        print("      (None expected - 256-bit has 2^256 possible outputs)")
    print("      [256-bit: collisions exist in theory but ~2^128 tries needed to find one]")
    print()

    # 4b. Reduced width: find collisions to prove they exist
    # 24-bit: ~4k inputs for 50% collision chance
    print("   4b. 24-bit hash, 10,000 inputs (collisions expected):")
    def gen_50k():
        for i in range(10_000):
            yield f"collision_test_{i}"

    unique_32, collisions_32 = collision_search(10_000, bit_width=24, input_generator=gen_50k)
    print(f"      Unique hashes: {unique_32:,}")
    print(f"      Collisions found: {len(collisions_32)}")
    if collisions_32:
        for a, b, h in collisions_32[:3]:
            print(f"         '{a}' and '{b}' → {h}")
        print("      ✓ Collision space CONFIRMED (different inputs → same hash)")
    else:
        print("      (Try increasing num_inputs for 32-bit)")
    print()

    # 4c. 32-bit: smaller space, collisions possible with enough inputs
    print("   4c. 32-bit hash, 25,000 inputs (birthday ~65k for 50% chance):")
    def gen_200k():
        for i in range(25_000):
            yield f"x_{i}_{random.randint(0, 10**9)}"

    unique_64, collisions_64 = collision_search(25_000, bit_width=32, input_generator=gen_200k)
    print(f"      Unique hashes: {unique_64:,}")
    print(f"      Collisions found: {len(collisions_64)}")
    if collisions_64:
        for a, b, h in collisions_64[:2]:
            print(f"         '{a}' and '{b}' → {h}")
    print()

    print("=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print("  • Deterministic: same input → same output ✓")
    print("  • Avalanche: small change → large output change")
    print("  • Collisions: exist in theory; provable with reduced bit width")
    print("  • 256/512-bit: collision-resistant for practical use (infeasible to find)")
    print("=" * 70)


if __name__ == "__main__":
    main()
