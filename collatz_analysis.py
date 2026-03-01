#!/usr/bin/env python3
"""
Collatz sequence analysis with branch point detection.

This script:
1. Takes an input number and generates the full Collatz sequence
2. Identifies "downstep sequences" (consecutive divisions by 2)
3. For positions where there are at least 2 consecutive downsteps,
   takes the first even number, multiplies by 2 and by 4,
   and finds odd numbers that could reach that value via 3x+1
4. Adds those potential odd predecessors and their even outputs to the sequence
5. Obfuscates by trimming from start until first consecutive even pair
"""


def collatz_sequence(n):
    """Generate the full Collatz sequence from n to 1."""
    sequence = [n]
    while n != 1:
        if n % 2 == 0:
            n = n // 2
        else:
            n = 3 * n + 1
        sequence.append(n)
    return sequence


def find_downstep_sequences(sequence):
    """
    Find positions where there are at least 2 consecutive downsteps (divisions by 2).
    
    A downstep occurs when we go from an even number to its half.
    Returns list of (start_index, length) for each downstep sequence of length >= 2.
    """
    downstep_sequences = []
    i = 0
    
    while i < len(sequence) - 1:
        # Check if this is the start of a downstep sequence
        if sequence[i] % 2 == 0:
            start = i
            length = 0
            
            # Count consecutive even numbers (each represents a /2 step)
            j = i
            while j < len(sequence) - 1 and sequence[j] % 2 == 0:
                length += 1
                j += 1
            
            if length >= 2:
                downstep_sequences.append((start, length))
            
            i = j
        else:
            i += 1
    
    return downstep_sequences


def find_odd_predecessors(even_value):
    """
    Find odd numbers x such that 3x + 1 = even_value.
    
    For 3x + 1 = even_value:
        x = (even_value - 1) / 3
    
    x must be a positive odd integer.
    """
    predecessors = []
    
    if (even_value - 1) % 3 == 0:
        x = (even_value - 1) // 3
        if x > 0 and x % 2 == 1:  # x must be positive and odd
            predecessors.append(x)
    
    return predecessors


def obfuscate_list(sequence):
    """
    Remove elements from the beginning of the list until we reach a point
    where the current element and the next element are both even (consecutive /2 steps).
    
    This finds the first position where we have at least 2 consecutive even numbers.
    """
    for i in range(len(sequence) - 1):
        current = sequence[i]
        next_val = sequence[i + 1]
        
        # Check if both current and next are even (indicating consecutive /2 steps)
        if current % 2 == 0 and next_val % 2 == 0:
            # Found the start of a downstep sequence - trim everything before
            return sequence[i:]
    
    # If no such point found, return the original list
    return sequence


def analyze_collatz(n):
    """
    Main analysis function.
    
    1. Generate Collatz sequence
    2. Find downstep sequences of length >= 2
    3. For each, take first even, multiply by 2 and by 4
    4. Find odd predecessors that would produce those values via 3x+1
    5. Return enriched and obfuscated data structures
    """
    # Step 1: Generate the sequence
    sequence = collatz_sequence(n)
    
    print(f"Input: {n}")
    print(f"Collatz sequence length: {len(sequence)}")
    print(f"Sequence: {' -> '.join(str(x) for x in sequence)}")
    print()
    
    # Step 2: Find downstep sequences
    downstep_seqs = find_downstep_sequences(sequence)
    
    print(f"Found {len(downstep_seqs)} downstep sequences of length >= 2:")
    for start, length in downstep_seqs:
        segment = sequence[start:start + length + 1]
        print(f"  Position {start}: {' -> '.join(str(x) for x in segment)} ({length} divisions)")
    print()
    
    # Step 3 & 4: For each downstep sequence, find branch points
    # Now we go up multiple levels: x2, x4, x8, x16, x32, x64
    branch_analysis = []
    
    # Define multiplier levels (how many times to double)
    # x2=1, x4=2, x8=3, x16=4, x32=5, x64=6, ... x2^20=1048576
    NUM_LEVELS = 20  # Go up 20 levels (x2 through x2^20)
    
    for start, length in downstep_seqs:
        first_even = sequence[start]
        
        branch_info = {
            'position': start,
            'first_even': first_even,
            'multiplied_values': {},  # Store all multiplied values
            'predecessor_evens': []   # The 3x+1 results for each odd predecessor
        }
        
        # Calculate all multiplied values and find odd predecessors
        for level in range(1, NUM_LEVELS + 1):
            multiplier = 2 ** level  # 2, 4, 8, 16, 32, 64
            multiplied = first_even * multiplier
            mult_name = f'x{multiplier}'
            
            branch_info['multiplied_values'][mult_name] = multiplied
            
            # Find odd predecessors for this multiplied value
            odd_preds = find_odd_predecessors(multiplied)
            
            for odd in odd_preds:
                even_result = 3 * odd + 1
                branch_info['predecessor_evens'].append({
                    'odd': odd,
                    'even_3x1': even_result,
                    'multiplier': mult_name
                })
        
        branch_analysis.append(branch_info)
    
    # Print branch analysis
    print("Branch Point Analysis:")
    print("=" * 60)
    
    for branch in branch_analysis:
        print(f"\nPosition {branch['position']}:")
        print(f"  First even in downstep: {branch['first_even']}")
        print(f"  Multiplied values: ", end="")
        mult_strs = [f"{k}={v}" for k, v in branch['multiplied_values'].items()]
        print(", ".join(mult_strs))
        
        if branch['predecessor_evens']:
            print(f"  Odd predecessors found:")
            for pred_info in branch['predecessor_evens']:
                odd = pred_info['odd']
                even = pred_info['even_3x1']
                mult = pred_info['multiplier']
                print(f"    ({mult}) x = {odd}  ->  3*{odd}+1 = {even}")
        else:
            print(f"  No valid odd predecessors exist")
    
    # Build enriched sequence list with values inserted at their proper positions
    # We need to insert in reverse order of position to maintain correct indices
    enriched_list = list(sequence)  # Start with original sequence
    
    # Collect all insertions with their target positions
    # Include all multiplied evens (x2, x4, x8, x16, x32, x64) plus any odd predecessors
    insertions = []
    for branch in branch_analysis:
        position = branch['position']
        
        # Collect values to insert, starting from highest multiplier (furthest) down to x2 (closest)
        values_to_insert = []
        
        # Process multipliers in reverse order (x64 down to x2)
        multipliers = sorted(branch['multiplied_values'].keys(), 
                           key=lambda x: int(x[1:]), reverse=True)
        
        for mult_name in multipliers:
            mult_even = branch['multiplied_values'][mult_name]
            mult_preds = [p for p in branch['predecessor_evens'] if p['multiplier'] == mult_name]
            
            if mult_preds:
                # Add odd predecessor and its even result
                for pred in mult_preds:
                    values_to_insert.append(('odd', pred['odd']))
                    values_to_insert.append(('even', pred['even_3x1']))
            else:
                # No valid odd predecessor, just add the even
                values_to_insert.append(('even_no_pred', mult_even))
        
        insertions.append({
            'position': position,
            'values': values_to_insert
        })
    
    # Sort by position in REVERSE order so we can insert without messing up indices
    insertions.sort(key=lambda x: x['position'], reverse=True)
    
    # Insert values at each position
    for ins in insertions:
        pos = ins['position']
        # Insert in reverse order so they end up in correct order
        for val_type, val in reversed(ins['values']):
            enriched_list.insert(pos, val)
    
    print("\n" + "=" * 60)
    print("Summary: Values inserted into the sequence")
    print("=" * 60)
    
    # Re-sort for display purposes
    insertions.sort(key=lambda x: x['position'])
    for ins in insertions:
        values_str = ', '.join(str(v[1]) for v in ins['values'])
        print(f"  At position {ins['position']}: inserted [{values_str}]")
    
    print(f"\nOriginal sequence ({len(sequence)} elements):")
    print(f"  {sequence}")
    print(f"\nEnriched sequence ({len(enriched_list)} elements):")
    print(f"  {enriched_list}")
    
    # Obfuscate: Remove elements from beginning until we reach a point
    # where the next two elements both divide by 2 (consecutive even numbers)
    obfuscated_list = obfuscate_list(enriched_list)
    
    print(f"\n" + "=" * 60)
    print("Obfuscated sequence (trimmed from start until first downstep)")
    print("=" * 60)
    print(f"\nObfuscated sequence ({len(obfuscated_list)} elements):")
    print(f"  {obfuscated_list}")
    
    return {
        'input': n,
        'sequence': sequence,
        'downstep_sequences': downstep_seqs,
        'branch_analysis': branch_analysis,
        'enriched_list': enriched_list,
        'obfuscated_list': obfuscated_list
    }


def main():
    import sys
    
    if len(sys.argv) > 1:
        try:
            n = int(sys.argv[1])
        except ValueError:
            print(f"Error: '{sys.argv[1]}' is not a valid integer")
            sys.exit(1)
    else:
        # Interactive mode
        try:
            n = int(input("Enter a positive integer: "))
        except ValueError:
            print("Error: Please enter a valid integer")
            sys.exit(1)
    
    if n <= 0:
        print("Error: Please enter a positive integer")
        sys.exit(1)
    
    result = analyze_collatz(n)
    
    return result


if __name__ == "__main__":
    main()
