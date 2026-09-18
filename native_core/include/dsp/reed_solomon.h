#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace dsp {

// Reed-Solomon (15, 9) Codec over GF(2^4)
// Primitive polynomial: p(x) = x^4 + x + 1 (19)
class ReedSolomon {
public:
    static const int N = 15;
    static const int K = 9;
    static const int NROOTS = 6; // 15 - 9 = 6 parity symbols

    ReedSolomon() {
        init_gf();
        init_generator();
    }

    void encode(const uint8_t* msg, uint8_t* parity) {
        std::vector<uint8_t> bb(NROOTS, 0);
        for (int i = 0; i < K; ++i) {
            uint8_t feedback = gf_add(msg[i], bb[0]);
            for (int j = 0; j < NROOTS - 1; ++j) {
                bb[j] = gf_add(bb[j + 1], gf_mul(gen_[j + 1], feedback));
            }
            bb[NROOTS - 1] = gf_mul(gen_[NROOTS], feedback);
        }
        for (int i = 0; i < NROOTS; ++i) {
            parity[i] = bb[i];
        }
    }

    bool decode(uint8_t* codeword) {
        // 1. Compute syndromes for roots alpha^1 .. alpha^6
        std::vector<uint8_t> syn(NROOTS, 0);
        bool has_error = false;
        for (int i = 0; i < NROOTS; ++i) {
            uint8_t root = exp_table_[i + 1];
            uint8_t val = 0;
            for (int j = 0; j < N; ++j) {
                val = gf_add(gf_mul(val, root), codeword[j]);
            }
            syn[i] = val;
            if (val != 0) has_error = true;
        }
        if (!has_error) return true; // Clean codeword

        // 2. Single-error locator & evaluator
        for (int p = 0; p < N; ++p) {
            int power = (N - 1 - p) % 15;
            int inv_pow = (15 - (1 * power) % 15) % 15;
            uint8_t inv_r1 = exp_table_[inv_pow];
            uint8_t cand_err = gf_mul(syn[0], inv_r1);

            bool match = true;
            for (int r = 0; r < NROOTS; ++r) {
                int r_idx = r + 1;
                uint8_t exp_syn = gf_mul(cand_err, exp_table_[(r_idx * power) % 15]);
                if (exp_syn != syn[r]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                codeword[p] = gf_add(codeword[p], cand_err);
                return true;
            }
        }

        return false; // Error exceeds single symbol correction capability
    }

    uint8_t gf_add(uint8_t a, uint8_t b) const { return a ^ b; }

    uint8_t gf_mul(uint8_t a, uint8_t b) const {
        if (a == 0 || b == 0) return 0;
        return exp_table_[(log_table_[a] + log_table_[b]) % 15];
    }

private:
    uint8_t exp_table_[32];
    uint8_t log_table_[16];
    std::vector<uint8_t> gen_;

    void init_gf() {
        uint8_t x = 1;
        for (int i = 0; i < 15; ++i) {
            exp_table_[i] = x;
            exp_table_[i + 15] = x;
            log_table_[x] = i;
            x <<= 1;
            if (x & 16) x ^= 19;
        }
        log_table_[0] = 0;
    }

    void init_generator() {
        // g(x) = prod_{i=1..6} (x - alpha^i)
        gen_ = { 1 };
        for (int i = 1; i <= NROOTS; ++i) {
            uint8_t root = exp_table_[i];
            std::vector<uint8_t> new_gen(gen_.size() + 1, 0);
            for (size_t j = 0; j < gen_.size(); ++j) {
                new_gen[j] = gf_add(new_gen[j], gen_[j]);
                new_gen[j + 1] = gf_add(new_gen[j + 1], gf_mul(gen_[j], root));
            }
            gen_ = new_gen;
        }
    }
};

} // namespace dsp
