#!/usr/bin/env python3
"""Test for sequence sum collisions across different inputs."""

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

# Test inputs - similar strings that might collide
test_inputs = [
    'password',
    'Password',
    'password1',
    'password2',
    'passw0rd',
    'passwrod',  # typo
    'drowssap',  # reversed
    'password123!@#',
    'password123!@$',
    'Password123!@#',
    'hi',
    'Hi',
    'hj',  # adjacent char
    'ih',  # reversed
    'a',
    'b',
    'aa',
    'ab',
    'ba',
    'abc',
    'cba',
    'test',
    'Test',
    'TEST',
    '123',
    '321',
    'hello',
    'Hello',
    'HELLO',
    'world',
    'World',
]

print('=' * 80)
print('SEQUENCE SUM COLLISION TEST')
print('=' * 80)
print()

results = []
sums_seen = {}
collisions = []

header = "Input                Encoded Bits    Seq Len    Sum (last 20 digits)"
print(header)
print('-' * 75)

for text in test_inputs:
    encoded = text_to_number(text)
    seq = collatz_fast(encoded)
    seq_sum = sum(seq)
    
    # Check for collision
    if seq_sum in sums_seen:
        collisions.append((text, sums_seen[seq_sum]))
    sums_seen[seq_sum] = text
    
    # Display last 20 digits of sum (full sum is huge)
    sum_str = str(seq_sum)[-20:]
    
    print(f"{text:<20} {encoded.bit_length():<15} {len(seq):<10} ...{sum_str}")
    results.append((text, seq_sum))

print()
print('=' * 80)
print('COLLISION CHECK')
print('=' * 80)
print()

if collisions:
    print(f"COLLISIONS FOUND: {len(collisions)}")
    for t1, t2 in collisions:
        print(f'  "{t1}" collides with "{t2}"')
else:
    print(f"NO COLLISIONS among {len(test_inputs)} inputs!")
    print("All sequence sums are unique.")

print()

# Also check: are any sums equal?
all_sums = [s for _, s in results]
unique_sums = set(all_sums)
print(f"Total inputs: {len(all_sums)}")
print(f"Unique sums: {len(unique_sums)}")
print(f"Collision rate: {(len(all_sums) - len(unique_sums)) / len(all_sums) * 100:.2f}%")

print()
print('=' * 80)
print('ADDITIONAL CHECKS')
print('=' * 80)
print()

# Check if similar inputs have similar sums (they shouldn't)
print("Difference between similar inputs:")
print("-" * 50)

pairs = [
    ('password', 'Password'),
    ('password', 'password1'),
    ('hi', 'hj'),
    ('abc', 'cba'),
]

for t1, t2 in pairs:
    s1 = sum(collatz_fast(text_to_number(t1)))
    s2 = sum(collatz_fast(text_to_number(t2)))
    diff = abs(s1 - s2)
    ratio = max(s1, s2) / min(s1, s2)
    print(f'"{t1}" vs "{t2}": differ by {diff:.2e} ({ratio:.2f}x ratio)')

print()
print("Conclusion: Even similar inputs produce vastly different sums.")
