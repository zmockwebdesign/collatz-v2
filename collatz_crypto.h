#pragma once
/**
 * Collatz Cryptographic Suite - Public API
 *
 * CollatzHash V5:  Sponge hash, 256/512/1024-bit output
 * CollatzAEAD:     Duplex sponge authenticated encryption
 * CollatzMAC:      Keyed sponge message authentication
 *
 * All built on CollatzPerm-512 (collatz_perm.h).
 */

#include "collatz_perm.h"
#include <string>
#include <vector>

// ============================================================
// CollatzHash V5 - Sponge hash
// ============================================================

struct CollatzDigest {
    uint8_t bytes[128];  // up to 1024 bits
    int len;             // actual byte length

    std::string hex() const;
    bool operator==(const CollatzDigest& o) const;
    bool operator!=(const CollatzDigest& o) const;
};

// Hash raw bytes. output_bits: 256, 512, or 1024.
CollatzDigest collatz_hash(const void* data, size_t len, int output_bits = 256);

// Convenience: hash a string
CollatzDigest collatz_hash_str(const std::string& text, int output_bits = 256);

// ============================================================
// CollatzAEAD - Authenticated Encryption with Associated Data
// ============================================================

struct CollatzAEADResult {
    std::vector<uint8_t> ciphertext;
    uint8_t tag[16];  // 128-bit authentication tag

    bool verify_tag(const uint8_t expected[16]) const;
};

// Encrypt and authenticate.
// key: 32 bytes (256-bit), nonce: 16 bytes (128-bit)
CollatzAEADResult collatz_aead_encrypt(
    const uint8_t key[32],
    const uint8_t nonce[16],
    const void* plaintext, size_t pt_len,
    const void* aad, size_t aad_len);

// Decrypt and verify. Returns empty vector if tag verification fails.
std::vector<uint8_t> collatz_aead_decrypt(
    const uint8_t key[32],
    const uint8_t nonce[16],
    const void* ciphertext, size_t ct_len,
    const void* aad, size_t aad_len,
    const uint8_t tag[16]);

// ============================================================
// CollatzMAC - Keyed sponge MAC
// ============================================================

struct CollatzMACTag {
    uint8_t bytes[32];  // up to 256 bits
    int len;

    std::string hex() const;
    bool operator==(const CollatzMACTag& o) const;
};

// Compute MAC. key: 32 bytes (256-bit). tag_bits: 128 or 256.
CollatzMACTag collatz_mac(
    const uint8_t key[32],
    const void* data, size_t len,
    int tag_bits = 128);

// Verify MAC. Returns true if tag matches.
bool collatz_mac_verify(
    const uint8_t key[32],
    const void* data, size_t len,
    const CollatzMACTag& expected);
