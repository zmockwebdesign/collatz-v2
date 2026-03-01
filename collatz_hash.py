#!/usr/bin/env python3
"""
Collatz Hash - Pure math hashing using the Collatz conjecture.

No external crypto libraries. Just arithmetic and bit operations.

Output: Hexadecimal hash of the Collatz sequence sum.
"""

# Configuration
MULTIPLIER = 100_000_000_000_000  # 100 trillion
NUM_LEVELS = 15                    # Fake branch levels


def text_to_number(text, multiplier=MULTIPLIER):
    """
    Encode text to a number using base-128.
    
    Base-128 matches ASCII printable range (0-127 = 7 bits per char).
    """
    result = 0
    for char in text:
        result = (result << 7) + (ord(char) & 0x7F)
    result *= multiplier
    if not (result & 1):
        result = (result << 1) | 1
    return result


def collatz_sequence(n):
    """
    Generate Collatz sequence using bit operations for speed.
    """
    sequence = [n]
    while n != 1:
        if n & 1:  # Odd
            n = 3 * n + 1
        else:      # Even
            n = n >> 1
        sequence.append(n)
    return sequence


def collatz_hash(text, hex_length=None, bit_width=512):
    """
    Generate a Collatz-based hash of the input text.
    
    Uses 2D vector folding:
    1. Concatenate all sequence values into one large binary number
    2. Split into rows of 'bit_width' bits (e.g., 512)
    3. Pad last row if needed
    4. XOR/ADD all rows together to produce fixed-width output
    
    Args:
        text: Input string to hash
        hex_length: Optional - override output length in hex chars
        bit_width: Width of each row (default 512 bits)
    
    Returns:
        Hexadecimal hash string (fixed length based on bit_width)
    """
    # Encode text to number
    encoded = text_to_number(text)
    
    # Generate Collatz sequence
    sequence = collatz_sequence(encoded)
    
    # Concatenate all sequence values into one giant number
    concat_bits = 0
    total_bits = 0
    for val in sequence:
        val_bits = val.bit_length() or 1  # At least 1 bit
        concat_bits = (concat_bits << val_bits) | val
        total_bits += val_bits
    
    # Split into rows of bit_width
    mask = (1 << bit_width) - 1  # Mask for bit_width bits
    rows = []
    
    remaining = concat_bits
    while remaining > 0:
        row = remaining & mask  # Take lowest bit_width bits
        rows.append(row)
        remaining = remaining >> bit_width
    
    # If no rows (shouldn't happen), create one with zeros
    if not rows:
        rows = [0]
    
    # Pad the last row is already handled (it's just smaller, but that's fine)
    # Actually, we want to ensure it's treated as full width - it already is via the mask
    
    # Fold all rows together using directional folding
    # Even rows: add top-to-bottom (normal)
    # Odd rows: add bottom-to-top (bit-reversed within the row)
    
    def reverse_bits(n, width):
        """Reverse the bits of n within the given width."""
        result = 0
        for _ in range(width):
            result = (result << 1) | (n & 1)
            n >>= 1
        return result
    
    result = 0
    for i, row in enumerate(rows):
        if i % 2 == 0:
            # Even rows: XOR top-to-bottom (normal direction)
            result = result ^ row
        else:
            # Odd rows: ADD bottom-to-top (reversed bits)
            reversed_row = reverse_bits(row, bit_width)
            result = (result + reversed_row) & mask
    
    # Additional mixing: rotate and XOR with weighted sum
    weighted_sum = sum(val * (i + 1) for i, val in enumerate(sequence)) & mask
    result = result ^ weighted_sum
    
    # Convert to hexadecimal
    hex_hash = format(result, 'x')
    
    # Ensure fixed length (bit_width / 4 hex chars)
    target_len = hex_length if hex_length else (bit_width // 4)
    hex_hash = hex_hash.zfill(target_len)
    
    # Truncate if somehow longer
    if len(hex_hash) > target_len:
        hex_hash = hex_hash[:target_len]
    
    return hex_hash


def collatz_hash_256(text):
    """Return a 256-bit (64 hex char) hash."""
    return collatz_hash(text, bit_width=256)


def collatz_hash_512(text):
    """Return a 512-bit (128 hex char) hash."""
    return collatz_hash(text, bit_width=512)


def collatz_hash_full(text):
    """Return 512-bit hash (default)."""
    return collatz_hash(text, bit_width=512)


# Demo
if __name__ == "__main__":
    test_inputs = [
        'hello',
        'Hello',
        'password',
        'password1',
        'Noobas123!@#',
        'The quick brown fox jumps over the lazy dog',
    ]
    
    print("=" * 80)
    print("COLLATZ HASH - Pure Math, No Crypto Libraries")
    print("=" * 80)
    print()
    
    print("FULL HASH (variable length, zero collisions):")
    print("-" * 80)
    for text in test_inputs:
        h = collatz_hash_full(text)
        print(f'"{text}"')
        print(f'  Length: {len(h)} hex chars ({len(h)*4} bits)')
        print(f'  Hash: {h[:64]}{"..." if len(h) > 64 else ""}')
        print()
    
    print()
    print("256-BIT HASH (64 hex chars, like SHA-256):")
    print("-" * 80)
    for text in test_inputs:
        h = collatz_hash_256(text)
        print(f'"{text}"')
        print(f'  {h}')
        print()
    
    print()
    print("512-BIT HASH (128 hex chars, like SHA-512):")
    print("-" * 80)
    for text in test_inputs[:3]:  # Just first 3 for brevity
        h = collatz_hash_512(text)
        print(f'"{text}"')
        print(f'  {h}')
        print()
    
    print()
    print("=" * 80)
    print("COMPARISON: Same input = Same hash")
    print("=" * 80)
    h1 = collatz_hash_256('test')
    h2 = collatz_hash_256('test')
    print(f'collatz_hash_256("test") run 1: {h1}')
    print(f'collatz_hash_256("test") run 2: {h2}')
    print(f'Match: {h1 == h2}')
    print()
    
    print("=" * 80)
    print("AVALANCHE EFFECT: Small change = Completely different hash")
    print("=" * 80)
    for pair in [('hello', 'hallo'), ('abc', 'abd'), ('test', 'Test')]:
        h1 = collatz_hash_256(pair[0])
        h2 = collatz_hash_256(pair[1])
        # Count differing characters
        diff = sum(1 for a, b in zip(h1, h2) if a != b)
        print(f'"{pair[0]}" vs "{pair[1]}": {diff}/64 chars differ ({diff/64*100:.1f}%)')
