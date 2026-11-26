#include "KeyCrypto.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include "../plugin/third_party/monocypher/monocypher.h"
}

namespace keycrypto {

// ====================================================================== SHA-256

namespace {
inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64];
    size_t used = 0;
    uint64_t total = 0;
    void block(const uint8_t* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + (rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
            uint32_t t2 = (rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const uint8_t* p, size_t n) {
        total += n;
        while (n > 0) {
            size_t k = std::min(n, 64 - used);
            std::memcpy(buf + used, p, k);
            used += k; p += k; n -= k;
            if (used == 64) { block(buf); used = 0; }
        }
    }
    std::array<uint8_t, 32> final() {
        const uint64_t bits = total * 8;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (used != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(len, 8);
        std::array<uint8_t, 32> out;
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 4; ++j) out[(size_t)(i * 4 + j)] = (uint8_t)(h[i] >> (24 - 8 * j));
        return out;
    }
};

const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

struct Sha512 {
    uint64_t h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                     0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    uint8_t buf[128];
    size_t used = 0;
    uint64_t total = 0;
    void block(const uint8_t* p) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 8; ++j) w[i] = (w[i] << 8) | p[i * 8 + j];
        }
        for (int i = 16; i < 80; ++i) {
            uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            uint64_t t1 = hh + (rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41)) + ((e & f) ^ (~e & g)) + K512[i] + w[i];
            uint64_t t2 = (rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const uint8_t* p, size_t n) {
        total += n;
        while (n > 0) {
            size_t k = std::min(n, 128 - used);
            std::memcpy(buf + used, p, k);
            used += k; p += k; n -= k;
            if (used == 128) { block(buf); used = 0; }
        }
    }
    std::array<uint8_t, 64> final() {
        const uint64_t bits = total * 8;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (used != 112) update(&zero, 1);
        uint8_t len[16] = {};
        for (int i = 0; i < 8; ++i) len[8 + i] = (uint8_t)(bits >> (56 - 8 * i));
        update(len, 16);
        std::array<uint8_t, 64> out;
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j) out[(size_t)(i * 8 + j)] = (uint8_t)(h[i] >> (56 - 8 * j));
        return out;
    }
};
}  // namespace

std::array<uint8_t, 32> sha256(const uint8_t* d, size_t n) {
    Sha256 s;
    s.update(d, n);
    return s.final();
}

std::array<uint8_t, 64> sha512(const uint8_t* d, size_t n) {
    Sha512 s;
    s.update(d, n);
    return s.final();
}

std::array<uint8_t, 32> hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t n) {
    uint8_t k[64] = {};
    if (keyLen > 64) {
        auto h = sha256(key, keyLen);
        std::memcpy(k, h.data(), 32);
    } else if (keyLen > 0) {
        std::memcpy(k, key, keyLen);
    }
    uint8_t ip[64], op[64];
    for (int i = 0; i < 64; ++i) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    Sha256 in;
    in.update(ip, 64);
    in.update(data, n);
    auto ih = in.final();
    Sha256 out;
    out.update(op, 64);
    out.update(ih.data(), 32);
    wipe(k, sizeof k);
    return out.final();
}

Bytes pbkdf2Sha256(const std::string& password, const uint8_t* salt, size_t saltLen, int iterations, size_t outLen) {
    Bytes out;
    const auto* pw = (const uint8_t*)password.data();
    // clé HMAC préparée une seule fois (300 000 tours : il faut que ce soit rapide)
    uint8_t k[64] = {};
    if (password.size() > 64) {
        auto h = sha256(pw, password.size());
        std::memcpy(k, h.data(), 32);
    } else {
        std::memcpy(k, pw, password.size());
    }
    uint8_t ip[64], op[64];
    for (int i = 0; i < 64; ++i) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    Sha256 inner0, outer0;
    inner0.update(ip, 64);
    outer0.update(op, 64);
    auto mac = [&](const uint8_t* d, size_t n) {
        Sha256 a = inner0;
        a.update(d, n);
        auto ih = a.final();
        Sha256 b = outer0;
        b.update(ih.data(), 32);
        return b.final();
    };
    for (uint32_t block = 1; out.size() < outLen; ++block) {
        Bytes first(salt, salt + saltLen);
        for (int s = 24; s >= 0; s -= 8) first.push_back((uint8_t)(block >> s));
        auto u = mac(first.data(), first.size());
        auto t = u;
        for (int i = 1; i < iterations; ++i) {
            u = mac(u.data(), 32);
            for (int j = 0; j < 32; ++j) t[(size_t)j] ^= u[(size_t)j];
        }
        for (int j = 0; j < 32 && out.size() < outLen; ++j) out.push_back(t[(size_t)j]);
    }
    wipe(k, sizeof k);
    return out;
}

// ====================================================================== AES-256-GCM

namespace {
const uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59,
    0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1,
    0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83,
    0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
    0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c,
    0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
    0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee,
    0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6,
    0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9,
    0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1,
    0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

struct Aes256 {
    uint8_t rk[240];
    explicit Aes256(const uint8_t key[32]) {
        std::memcpy(rk, key, 32);
        uint8_t rcon = 1;
        for (int i = 8; i < 60; ++i) {
            uint8_t t[4];
            std::memcpy(t, rk + (i - 1) * 4, 4);
            if (i % 8 == 0) {
                uint8_t x = t[0];
                t[0] = SBOX[t[1]] ^ rcon; t[1] = SBOX[t[2]]; t[2] = SBOX[t[3]]; t[3] = SBOX[x];
                rcon = (uint8_t)((rcon << 1) ^ ((rcon & 0x80) ? 0x1b : 0));
            } else if (i % 8 == 4) {
                for (auto& v : t) v = SBOX[v];
            }
            for (int j = 0; j < 4; ++j) rk[i * 4 + j] = rk[(i - 8) * 4 + j] ^ t[j];
        }
    }
    ~Aes256() { wipe(rk, sizeof rk); }
    static uint8_t xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
    void encrypt(const uint8_t in[16], uint8_t out[16]) const {
        uint8_t s[16];
        for (int i = 0; i < 16; ++i) s[i] = in[i] ^ rk[i];
        for (int r = 1; r <= 14; ++r) {
            uint8_t t[16];
            for (int i = 0; i < 16; ++i) t[i] = SBOX[s[i]];
            // décalage des lignes
            for (int c = 0; c < 4; ++c)
                for (int row = 0; row < 4; ++row) s[c * 4 + row] = t[((c + row) % 4) * 4 + row];
            if (r != 14)
                for (int c = 0; c < 4; ++c) {
                    uint8_t* col = s + c * 4;
                    uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3], all = a0 ^ a1 ^ a2 ^ a3;
                    col[0] ^= all ^ xt(a0 ^ a1);
                    col[1] ^= all ^ xt(a1 ^ a2);
                    col[2] ^= all ^ xt(a2 ^ a3);
                    col[3] ^= all ^ xt(a3 ^ a0);
                }
            for (int i = 0; i < 16; ++i) s[i] ^= rk[r * 16 + i];
        }
        std::memcpy(out, s, 16);
    }
};

void gfMul(uint8_t x[16], const uint8_t h[16]) {
    uint8_t z[16] = {}, v[16];
    std::memcpy(v, h, 16);
    for (int i = 0; i < 128; ++i) {
        if (x[i / 8] & (0x80 >> (i % 8)))
            for (int j = 0; j < 16; ++j) z[j] ^= v[j];
        const bool lsb = v[15] & 1;
        for (int j = 15; j > 0; --j) v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1] << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1;
    }
    std::memcpy(x, z, 16);
}

struct Gcm {
    const Aes256& aes;
    uint8_t h[16] = {}, j0[16] = {};
    Gcm(const Aes256& a, const uint8_t* iv, size_t ivLen) : aes(a) {
        const uint8_t zero[16] = {};
        aes.encrypt(zero, h);
        if (ivLen == 12) {
            std::memcpy(j0, iv, 12);
            j0[15] = 1;
        } else {
            ghash(nullptr, 0, iv, ivLen, j0);
        }
    }
    void ghash(const uint8_t* aad, size_t aadLen, const uint8_t* c, size_t cLen, uint8_t out[16]) const {
        uint8_t y[16] = {};
        auto absorb = [&](const uint8_t* d, size_t n) {
            for (size_t i = 0; i < n; i += 16) {
                for (size_t j = 0; j < 16 && i + j < n; ++j) y[j] ^= d[i + j];
                gfMul(y, h);
            }
        };
        absorb(aad, aadLen);
        absorb(c, cLen);
        uint8_t len[16] = {};
        const uint64_t a = (uint64_t)aadLen * 8, b = (uint64_t)cLen * 8;
        for (int i = 0; i < 8; ++i) { len[i] = (uint8_t)(a >> (56 - 8 * i)); len[8 + i] = (uint8_t)(b >> (56 - 8 * i)); }
        for (int j = 0; j < 16; ++j) y[j] ^= len[j];
        gfMul(y, h);
        std::memcpy(out, y, 16);
    }
    void ctr(const uint8_t* in, size_t n, uint8_t* out) const {
        uint8_t cb[16], ks[16];
        std::memcpy(cb, j0, 16);
        for (size_t i = 0; i < n; i += 16) {
            for (int k = 15; k >= 12; --k)
                if (++cb[k] != 0) break;
            aes.encrypt(cb, ks);
            for (size_t j = 0; j < 16 && i + j < n; ++j) out[i + j] = in[i + j] ^ ks[j];
        }
    }
    void tag(const uint8_t* c, size_t n, uint8_t out[16]) const {
        uint8_t s[16], e[16];
        ghash(nullptr, 0, c, n, s);
        aes.encrypt(j0, e);
        for (int i = 0; i < 16; ++i) out[i] = s[i] ^ e[i];
    }
};
}  // namespace

bool aes256GcmDecrypt(const uint8_t key[32], const uint8_t* iv, size_t ivLen, const uint8_t* ct, size_t n, Bytes& plain) {
    if (n < 16) return false;
    Aes256 aes(key);
    Gcm g(aes, iv, ivLen);
    uint8_t t[16];
    g.tag(ct, n - 16, t);
    uint8_t diff = 0;
    for (int i = 0; i < 16; ++i) diff |= (uint8_t)(t[i] ^ ct[n - 16 + (size_t)i]);
    if (diff != 0) return false;
    plain.resize(n - 16);
    g.ctr(ct, n - 16, plain.data());
    return true;
}

Bytes aes256GcmEncrypt(const uint8_t key[32], const uint8_t* iv, size_t ivLen, const uint8_t* p, size_t n) {
    Aes256 aes(key);
    Gcm g(aes, iv, ivLen);
    Bytes out(n + 16);
    g.ctr(p, n, out.data());
    g.tag(out.data(), n, out.data() + n);
    return out;
}

// ====================================================================== Ed25519 (RFC 8032, SHA-512)

namespace {
void hashReduce(uint8_t h[32], const uint8_t* a, size_t na, const uint8_t* b, size_t nb, const uint8_t* c, size_t nc) {
    Sha512 s;
    s.update(a, na);
    s.update(b, nb);
    if (nc) s.update(c, nc);
    auto full = s.final();
    crypto_eddsa_reduce(h, full.data());
}
}  // namespace

std::array<uint8_t, 32> ed25519PublicKey(const uint8_t seed[32]) {
    auto a = sha512(seed, 32);
    uint8_t s[32];
    crypto_eddsa_trim_scalar(s, a.data());
    std::array<uint8_t, 32> pub;
    crypto_eddsa_scalarbase(pub.data(), s);
    wipe(s, 32);
    wipe(a.data(), 64);
    return pub;
}

std::array<uint8_t, 64> ed25519Sign(const uint8_t seed[32], const uint8_t* msg, size_t n) {
    auto a = sha512(seed, 32);
    uint8_t s[32], r[32], h[32];
    crypto_eddsa_trim_scalar(s, a.data());
    auto pub = ed25519PublicKey(seed);
    hashReduce(r, a.data() + 32, 32, msg, n, nullptr, 0);
    std::array<uint8_t, 64> sig;
    crypto_eddsa_scalarbase(sig.data(), r);
    hashReduce(h, sig.data(), 32, pub.data(), 32, msg, n);
    crypto_eddsa_mul_add(sig.data() + 32, h, s, r);
    wipe(s, 32);
    wipe(r, 32);
    wipe(a.data(), 64);
    return sig;
}

bool ed25519Verify(const uint8_t pub[32], const uint8_t* msg, size_t n, const uint8_t sig[64]) {
    uint8_t h[32];
    Sha512 s;
    s.update(sig, 32);
    s.update(pub, 32);
    s.update(msg, n);
    auto full = s.final();
    crypto_eddsa_reduce(h, full.data());
    return crypto_eddsa_check_equation(sig, pub, h) == 0;
}

// ====================================================================== base32, hexa

static const char* B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

std::string base32Encode(const uint8_t* d, size_t n) {
    std::string out;
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        buf = (buf << 8) | d[i];
        bits += 8;
        while (bits >= 5) {
            out += B32[(buf >> (bits - 5)) & 31];
            bits -= 5;
        }
        buf &= (1u << bits) - 1;
    }
    if (bits > 0) out += B32[(buf << (5 - bits)) & 31];
    return out;
}

bool base32Decode(const std::string& s, Bytes& out) {
    out.clear();
    uint32_t buf = 0;
    int bits = 0;
    for (char ch : s) {
        const char* p = ch ? std::strchr(B32, ch) : nullptr;
        if (!p) return false;
        buf = (buf << 5) | (uint32_t)(p - B32);
        bits += 5;
        if (bits >= 8) {
            out.push_back((uint8_t)(buf >> (bits - 8)));
            bits -= 8;
            buf &= (1u << bits) - 1;
        }
    }
    return buf == 0;  // les bits de remplissage doivent être nuls
}

std::string toHex(const uint8_t* d, size_t n, bool upper) {
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += digits[d[i] >> 4];
        s += digits[d[i] & 15];
    }
    return s;
}

bool fromHex(const std::string& s, Bytes& out) {
    out.clear();
    if (s.size() % 2) return false;
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); i += 2) {
        int a = v(s[i]), b = v(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back((uint8_t)(a * 16 + b));
    }
    return true;
}

void wipe(void* p, size_t n) { crypto_wipe(p, n); }

}  // namespace keycrypto
