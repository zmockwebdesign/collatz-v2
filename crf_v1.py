"""
CRF (Collatz Residue Fingerprint) V1 potential and exception detection.

V1 potential: V1(n_i) = log2(n_i) - (2/3) * (k_i + k_{i-1} + k_{i-2})
where k_i = v2(3*n_i + 1) (trailing zeros in binary).

An orbit is exceptional if the fraction of negative V1 increments (down steps)
is < exc_threshold (default 0.4).
"""

import math


def v2(n: int) -> int:
    """Trailing zeros in binary (2-adic valuation). n must be even."""
    if n == 0:
        raise ValueError("v2(0) undefined")
    return (n & -n).bit_length() - 1


def is_exceptional_v1(
    n0: int,
    max_steps: int = 1000,
    max_n: int = 10**12,
    exc_threshold: float = 0.4,
) -> bool:
    """
    Check if orbit starting at odd n0 is exceptional under V1.

    Returns True if frac_down < exc_threshold.
    """
    km2, km1 = 0, 0
    prev_V = None
    down = up = flat = 0

    n = n0
    for _ in range(max_steps):
        t = 3 * n + 1
        k = v2(t)
        V = math.log2(n) - (2.0 / 3.0) * (k + km1 + km2)

        if prev_V is not None:
            dV = V - prev_V
            if dV < 0:
                down += 1
            elif dV > 0:
                up += 1
            else:
                flat += 1
        prev_V = V

        if n == 1 or n > max_n:
            break
        n = t >> k
        km2, km1 = km1, k

    denom = down + up + flat
    frac_down = down / denom if denom > 0 else 1.0
    return frac_down < exc_threshold
