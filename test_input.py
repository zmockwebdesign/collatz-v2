#!/usr/bin/env python3
"""Test a specific input."""

from compare_hashes import analyze_collatz_security
#
text = 'Noobas123!@#'
r = analyze_collatz_security(text)

print(f"Input: '{r['text']}'")
print(f"Encoded number: {r['encoded_number']:,}")
print(f"Encoded bits: {r['encoded_bits']}")
print(f"Collatz sequence length: {r['sequence_length']}")
print(f"Number of downsteps: {r['num_downsteps']}")
print(f"Estimated obfuscated length: {r['estimated_obfuscated_length']}")
print(f"Branching output space: 2^{r['branching_bits']} = {r['branching_output_space']:,}")
print()

# Brute force estimate
ops_per_sec = 1000
seconds = r['branching_output_space'] / ops_per_sec
years = seconds / (60 * 60 * 24 * 365)
print(f"Brute force time: ~{years:.2e} years")
print()

# Compare to standard algorithms
print("COMPARISON:")
print("-" * 50)
print(f"  Collatz '{text}': 2^{r['branching_bits']} bits")
print(f"  MD5:              2^128 bits")
print(f"  SHA-1:            2^160 bits")
print(f"  bcrypt:           2^184 bits")
print(f"  SHA-256:          2^256 bits")
print()

if r['branching_bits'] >= 256:
    print("STATUS: Matches or exceeds SHA-256!")
elif r['branching_bits'] >= 184:
    print("STATUS: Exceeds bcrypt!")
elif r['branching_bits'] >= 160:
    print("STATUS: Exceeds SHA-1!")
elif r['branching_bits'] >= 128:
    print("STATUS: Exceeds MD5!")
else:
    print(f"STATUS: Below MD5 (need {128 - r['branching_bits']} more bits)")
