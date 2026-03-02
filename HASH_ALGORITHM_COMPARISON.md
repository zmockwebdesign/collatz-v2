# Collatz Hash vs Standard Hashing Algorithms

## Overview

How the Collatz hash compares to algorithms used in practice today.

---

## 1. Algorithm Categories

| Category | Examples | Primary Use |
|----------|----------|-------------|
| **Cryptographic (general)** | SHA-256, SHA-512, SHA3-256 | Integrity, signatures, HMAC |
| **Password hashing** | bcrypt, Argon2, scrypt | Storing passwords |
| **Legacy/deprecated** | MD5, SHA-1 | Avoid for security |
| **Collatz** | collatz_hash_256/512 | Novel, no crypto libs |

---

## 2. Comparison Table

| Property | SHA-256 | SHA-512 | bcrypt | Argon2 | Collatz-256 | Collatz-512 |
|----------|---------|---------|--------|--------|-------------|-------------|
| **Output size** | 256 bits | 512 bits | 184 bits | 256–512 | 256 bits | 512 bits |
| **Preimage resistance** | 2^256 | 2^512 | 2^184 | Configurable | 2^256 | 2^512 |
| **Collision resistance** | 2^128 | 2^256 | N/A | Configurable | 2^128 | 2^256 |
| **Speed** | ~500k–2M ops/sec | ~200k–1M ops/sec | ~100–1k ops/sec | ~100–500 ops/sec | ~200–800 ops/sec | ~150–600 ops/sec |
| **External crypto libs** | Yes | Yes | Yes | Yes | **No** | **No** |
| **Cryptographically analyzed** | Yes (NIST) | Yes (NIST) | Yes | Yes | **No** | **No** |
| **Constant-time** | Yes | Yes | Yes | Yes | **No** | **No** |
| **Configurable cost** | No | No | Yes (rounds) | Yes (memory/time) | No | No |

---

## 3. Detailed Comparison

### 3.1 Speed (ops/sec, typical)

```
SHA-256:      ~500,000 - 2,000,000  (C implementation)
SHA-512:      ~200,000 - 1,000,000
SHA3-256:     ~100,000 - 500,000
MD5:          ~1,000,000+

Collatz (Python):  ~200 - 250
Collatz (C++):     ~750 - 800

bcrypt:       ~100 - 1,000  (intentionally slow)
Argon2:       ~100 - 500
```

Collatz is roughly 100–500× slower than SHA-256/512, and in the same ballpark as bcrypt/Argon2 for password hashing.

### 3.2 Security Properties

| Property | SHA-256/512 | Collatz |
|----------|-------------|---------|
| **Preimage resistance** | Matches | Same output size → same theoretical bound |
| **Collision resistance** | Matches | Same output size → same birthday bound |
| **Avalanche effect** | ~50% bit flip | ~48–50% (comparable in tests) |
| **Provability** | Widely studied | Novel, not standardized |
| **Side-channel resistance** | Constant-time designs | Input-dependent work (sequence length) |

### 3.3 Where Collatz Differs

**Advantages:**
- No external crypto libraries
- Pure math (arithmetic + Collatz)
- Deterministic
- Output sizes comparable to SHA-256/512
- Slower than SHA, similar to password hashers (good for passwords)

**Disadvantages:**
- Not designed or analyzed as a standard crypto hash
- Not constant-time → potential timing side channels
- No published formal security analysis
- Slower than SHA when speed is needed (integrity, signatures)

---

## 4. Use-Case Fit

| Use Case | SHA-256 | bcrypt/Argon2 | Collatz |
|----------|---------|---------------|---------|
| File integrity, checksums | ✅ Standard | ❌ Overkill | ⚠️ Possible but slow |
| Digital signatures | ✅ Standard | ❌ No | ❌ No |
| HMAC / key derivation | ✅ Standard | ❌ No | ❌ No |
| Password storage | ❌ Too fast | ✅ Designed for this | ⚠️ Similar speed profile |
| No-crypto environment | ❌ Needs lib | ❌ Needs lib | ✅ Pure math |
| Research / education | ⚠️ | ⚠️ | ✅ Interesting approach |

---

## 5. Summary

**Compared to SHA-256/512:**
- Similar output size and theoretical strength (2^256 / 2^512 preimage, ~2^128 / 2^256 collision).
- Much slower (by design; no optimized crypto primitives).
- Not formally analyzed; cannot replace SHA for serious security applications.

**Compared to bcrypt/Argon2:**
- Similar speed range (hundreds of ops/sec).
- Lacks cost parameters (memory, time).
- No salt/key handling or standard API.

**Bottom line:** Collatz is a pure-math alternative that matches SHA on output size and informal avalanche behavior, but it is slower and lacks the analysis and tooling of SHA and modern password hashers. Best suited for non-standard environments (no crypto libs), research, or pedagogical use.
