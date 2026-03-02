#!/usr/bin/env python3
"""
Tune Collatz security to target specific bit levels.

Target: SHA-256 (256 bits) or SHA-512 (512 bits)
"""

import math
from collatz_fast import collatz_fast

def text_to_number(text, multiplier):
    """Base-128 encoding with multiplier."""
    result = 0
    for char in text:
        result = (result << 7) + (ord(char) & 0x7F)
    result *= multiplier
    if not (result & 1):
        result = (result << 1) | 1
    return result


def calculate_security(num_downsteps, num_levels):
    """Calculate security bits: log2(num_levels) * num_downsteps"""
    return int(math.log2(num_levels) * num_downsteps)


def analyze(text, multiplier, num_levels):
    """Analyze security for given parameters."""
    encoded = text_to_number(text, multiplier)
    seq, downsteps = collatz_fast(encoded)
    security_bits = calculate_security(len(downsteps), num_levels)
    
    return {
        'multiplier': multiplier,
        'num_levels': num_levels,
        'encoded_bits': encoded.bit_length(),
        'seq_length': len(seq),
        'num_downsteps': len(downsteps),
        'security_bits': security_bits,
    }


def main():
    text = 'sample input 12'
    
    print("=" * 80)
    print(f"TUNING COLLATZ SECURITY FOR: '{text}'")
    print("=" * 80)
    print()
    print("Target levels:")
    print("  - SHA-256: 256 bits")
    print("  - SHA-512: 512 bits")
    print()
    
    # Current settings
    print("-" * 80)
    print("CURRENT SETTINGS (100T multiplier, 20 levels)")
    print("-" * 80)
    r = analyze(text, 100_000_000_000_000, 20)
    print(f"  Security: 2^{r['security_bits']} bits")
    print(f"  Downsteps: {r['num_downsteps']}")
    print()
    
    # Test different multipliers with 20 levels
    print("-" * 80)
    print("OPTION 1: Adjust MULTIPLIER (keep 20 levels)")
    print("-" * 80)
    print()
    print(f"{'Multiplier':<20} {'Downsteps':<12} {'Security Bits':<15} {'Target':<15}")
    print("-" * 62)
    
    multipliers = [
        1,
        1_000,
        1_000_000,
        10_000_000,
        100_000_000,
        1_000_000_000,
        10_000_000_000,
        100_000_000_000,
        1_000_000_000_000,
        10_000_000_000_000,
        100_000_000_000_000,
    ]
    
    for mult in multipliers:
        r = analyze(text, mult, 20)
        
        if r['security_bits'] <= 256:
            target = "<= SHA-256"
        elif r['security_bits'] <= 512:
            target = "<= SHA-512"
        else:
            target = "> SHA-512"
        
        mult_str = f"{mult:,}"
        print(f"{mult_str:<20} {r['num_downsteps']:<12} 2^{r['security_bits']:<12} {target:<15}")
    
    print()
    
    # Test different levels with fixed multiplier
    print("-" * 80)
    print("OPTION 2: Adjust NUM LEVELS (keep 1B multiplier)")
    print("-" * 80)
    print()
    
    # First find a good multiplier that gives reasonable downsteps
    r = analyze(text, 1_000_000_000, 20)
    print(f"With 1 billion multiplier: {r['num_downsteps']} downsteps")
    print()
    
    print(f"{'Levels':<10} {'Branching Factor':<20} {'Security Bits':<15} {'Target':<15}")
    print("-" * 60)
    
    for levels in [2, 3, 4, 5, 6, 8, 10, 12, 15, 20]:
        r = analyze(text, 1_000_000_000, levels)
        branching = f"log2({levels}) = {math.log2(levels):.2f}"
        
        if r['security_bits'] <= 256:
            target = "<= SHA-256"
        elif r['security_bits'] <= 512:
            target = "<= SHA-512"
        else:
            target = "> SHA-512"
        
        print(f"{levels:<10} {branching:<20} 2^{r['security_bits']:<12} {target:<15}")
    
    print()
    
    # Find optimal settings for each target
    print("=" * 80)
    print("RECOMMENDED SETTINGS")
    print("=" * 80)
    print()
    
    # Target: 256 bits
    print("FOR SHA-256 LEVEL (256 bits):")
    print("-" * 40)
    
    # Try combinations
    best_256 = None
    for mult in [1, 1000, 1_000_000, 10_000_000, 100_000_000, 1_000_000_000]:
        for levels in [2, 3, 4, 5, 6, 8, 10, 12, 15, 20]:
            r = analyze(text, mult, levels)
            if 240 <= r['security_bits'] <= 280:
                if best_256 is None or abs(r['security_bits'] - 256) < abs(best_256['security_bits'] - 256):
                    best_256 = r
    
    if best_256:
        print(f"  Multiplier: {best_256['multiplier']:,}")
        print(f"  Levels: {best_256['num_levels']}")
        print(f"  Security: 2^{best_256['security_bits']} bits")
    else:
        print("  No exact match found - try different combinations")
    
    print()
    
    # Target: 512 bits
    print("FOR SHA-512 LEVEL (512 bits):")
    print("-" * 40)
    
    best_512 = None
    for mult in [1_000_000, 10_000_000, 100_000_000, 1_000_000_000, 10_000_000_000]:
        for levels in [4, 5, 6, 8, 10, 12, 15, 20]:
            r = analyze(text, mult, levels)
            if 480 <= r['security_bits'] <= 540:
                if best_512 is None or abs(r['security_bits'] - 512) < abs(best_512['security_bits'] - 512):
                    best_512 = r
    
    if best_512:
        print(f"  Multiplier: {best_512['multiplier']:,}")
        print(f"  Levels: {best_512['num_levels']}")
        print(f"  Security: 2^{best_512['security_bits']} bits")
    else:
        print("  No exact match found - try different combinations")
    
    print()
    print("=" * 80)
    print("FORMULA")
    print("=" * 80)
    print()
    print("Security bits = log2(num_levels) * num_downsteps")
    print()
    print("To get 256 bits with N downsteps:")
    print("  levels = 2^(256/N)")
    print()
    print("Example: 100 downsteps -> need 2^2.56 = ~6 levels")
    print("Example: 50 downsteps  -> need 2^5.12 = ~35 levels")


if __name__ == "__main__":
    main()
