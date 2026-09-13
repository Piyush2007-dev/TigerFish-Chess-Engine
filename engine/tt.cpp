#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — TRANSPOSITION TABLE (newengine/tt.cpp)
// Cache-aligned 32-byte clusters, power-of-2 bitmask indexing, hardware prefetch.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include <cstring>
#include <cstdlib>
#include <atomic>

namespace tigerfish {

// ── Bound Types ──────────────────────────────────────────────────────────────
enum TTBound : uint8_t {
    BOUND_NONE  = 0,
    BOUND_EXACT = 1, // PV Node (score is exact)
    BOUND_LOWER = 2, // Cut Node (Beta cutoff, score >= beta)
    BOUND_UPPER = 3  // All Node (Failed low, score <= alpha)
};

// ── Packed 16-Byte TT Cluster Entry with Lockless Key-Data XOR Verification ────
struct alignas(16) TTClusterEntry {
    uint64_t key;  // Full 64-bit Zobrist key XOR'd with packed data
    uint64_t data; // Packed 64-bit payload: [move:16][score:16][eval:16][depth:8][meta:8]
};
static_assert(sizeof(TTClusterEntry) == 16, "TTClusterEntry must be exactly 16 bytes");
static_assert(std::is_trivial_v<TTClusterEntry>, "TTClusterEntry must be trivial POD");

// ── Unpacked TT Entry POD for caller ergonomics ─────────────────────────────
struct TTEntry {
    uint64_t key = 0;
    Move move = MOVE_NONE;    // Best move from this position
    int16_t score = 0;        // Normalized score
    int16_t eval = 0;         // Static evaluation
    int8_t depth = 0;         // Search depth
    uint8_t meta = 0;         // 2 bits bound | 6 bits age

    uint8_t bound() const noexcept { return meta & 3; }
    uint8_t age() const noexcept { return (meta >> 2) & 63; }
    void set_meta(uint8_t b, uint8_t a) noexcept { meta = (b & 3) | ((a & 63) << 2); }
};

// ── 64-Byte Cache-Aligned TT Cluster (4 Entries = Exactly 1 CPU Cacheline) ────
struct alignas(64) TTCluster {
    TTClusterEntry entries[4]; // 4 * 16 = 64 bytes
};
static_assert(sizeof(TTCluster) == 64, "TTCluster must be exactly 64 bytes");
static_assert(std::is_trivial_v<TTCluster>, "TTCluster must be trivial POD");

// ── Transposition Table POD Struct ───────────────────────────────────────────
struct TranspositionTable {
    TTCluster* clusters = nullptr;
    uint64_t cluster_count = 0;
    uint8_t current_age = 0;

    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
};

// ── Mate Score Normalization ─────────────────────────────────────────────────
// Ensures mate distance is relative to root, not search node ply.
inline int score_to_tt(int score, int ply) noexcept {
    if (score >= SCORE_MATE_BOUND) return score + ply;
    if (score <= -SCORE_MATE_BOUND) return score - ply;
    return score;
}

inline int score_from_tt(int score, int ply) noexcept {
    if (score >= SCORE_MATE_BOUND) return score - ply;
    if (score <= -SCORE_MATE_BOUND) return score + ply;
    return score;
}

// ── TT Free Functions ────────────────────────────────────────────────────────
inline void free_tt(TranspositionTable& tt) noexcept {
    if (tt.clusters) {
#if defined(_WIN32) || defined(_MSC_VER)
        _aligned_free(tt.clusters);
#else
        free(tt.clusters);
#endif
        tt.clusters = nullptr;
    }
    tt.cluster_count = 0;
}

inline void init_tt(TranspositionTable& tt, size_t megabytes) noexcept {
    free_tt(tt);

    // Compute nearest power-of-2 cluster count for instant bitmask indexing (no idiv)
    size_t target_bytes = megabytes * 1024 * 1024;
    size_t num_clusters = target_bytes / sizeof(TTCluster);

    // Round down to power of 2
    uint64_t pow2_clusters = 1ULL;
    while ((pow2_clusters << 1) <= num_clusters) {
        pow2_clusters <<= 1;
    }

    tt.cluster_count = pow2_clusters;
    size_t alloc_bytes = tt.cluster_count * sizeof(TTCluster);

#if defined(_WIN32) || defined(_MSC_VER)
    tt.clusters = static_cast<TTCluster*>(_aligned_malloc(alloc_bytes, 64));
#elif defined(__APPLE__) || defined(__linux__)
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, alloc_bytes) == 0) {
        tt.clusters = static_cast<TTCluster*>(ptr);
    } else {
        tt.clusters = static_cast<TTCluster*>(malloc(alloc_bytes));
    }
#else
    tt.clusters = static_cast<TTCluster*>(malloc(alloc_bytes));
#endif

    if (tt.clusters) {
        std::memset(static_cast<void*>(tt.clusters), 0, alloc_bytes);
    }
    tt.current_age = 0;
    tt.hits = 0;
    tt.misses = 0;
}

inline void clear_tt(TranspositionTable& tt) noexcept {
    if (tt.clusters && tt.cluster_count > 0) {
        std::memset(static_cast<void*>(tt.clusters), 0, tt.cluster_count * sizeof(TTCluster));
    }
    tt.hits = 0;
    tt.misses = 0;
}

inline void prefetch_tt(const TranspositionTable& tt, uint64_t key) noexcept {
    if (!tt.clusters) return;
    uint64_t idx = key & (tt.cluster_count - 1);
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(&tt.clusters[idx], 0, 3);
#elif defined(_MSC_VER)
    _mm_prefetch(reinterpret_cast<const char*>(&tt.clusters[idx]), _MM_HINT_T0);
#endif
}

inline bool probe_tt(TranspositionTable& tt, uint64_t key, TTEntry& entry) noexcept {
    if (!tt.clusters) return false;

    uint64_t idx = key & (tt.cluster_count - 1);
    const TTCluster& cluster = tt.clusters[idx];

    for (int i = 0; i < 4; ++i) {
        uint64_t d = cluster.entries[i].data;
        uint64_t k = cluster.entries[i].key ^ d;

        if (k == key) {
            entry.key = key;
            entry.move = Move(static_cast<uint16_t>(d & 0xFFFF));
            entry.score = static_cast<int16_t>((d >> 16) & 0xFFFF);
            entry.eval = static_cast<int16_t>((d >> 32) & 0xFFFF);
            entry.depth = static_cast<int8_t>((d >> 48) & 0xFF);
            entry.meta = static_cast<uint8_t>((d >> 56) & 0xFF);
            if (entry.bound() != BOUND_NONE) {
                return true;
            }
        }
    }

    return false;
}

inline void store_tt(TranspositionTable& tt, uint64_t key, Move best_move,
                     int score, int eval, int depth, uint8_t bound, int ply) noexcept {
    if (!tt.clusters) return;

    uint64_t idx = key & (tt.cluster_count - 1);
    TTCluster& cluster = tt.clusters[idx];

    int replace_idx = 0;
    int lowest_quality = 999999;
    uint8_t new_meta = (bound & 3) | ((tt.current_age & 63) << 2);
    int16_t norm_score = static_cast<int16_t>(score_to_tt(score, ply));
    int16_t s_eval = static_cast<int16_t>(eval);
    int8_t s_depth = static_cast<int8_t>(depth);

    for (int i = 0; i < 4; ++i) {
        uint64_t cur_d = cluster.entries[i].data;
        uint64_t cur_k = cluster.entries[i].key ^ cur_d;

        if (cur_k == key) {
            Move cur_move = Move(static_cast<uint16_t>(cur_d & 0xFFFF));
            int8_t cur_depth = static_cast<int8_t>((cur_d >> 48) & 0xFF);
            uint8_t cur_meta = static_cast<uint8_t>((cur_d >> 56) & 0xFF);
            uint8_t cur_age = (cur_meta >> 2) & 63;

            if (best_move.is_none()) {
                best_move = cur_move;
            }
            if (cur_depth > depth && cur_age == tt.current_age && bound != BOUND_EXACT) {
                if (!best_move.is_none() && cur_move.is_none()) {
                    uint64_t updated_d = static_cast<uint64_t>(best_move.raw())
                                       | (cur_d & 0xFFFFFFFFFFFF0000ULL);
                    cluster.entries[i].data = updated_d;
                    cluster.entries[i].key = key ^ updated_d;
                }
                return;
            }
            replace_idx = i;
            break;
        }

        uint8_t cur_meta = static_cast<uint8_t>((cur_d >> 56) & 0xFF);
        uint8_t cur_bound = cur_meta & 3;
        if (cur_bound == BOUND_NONE) {
            replace_idx = i;
            break;
        }

        int8_t cur_depth = static_cast<int8_t>((cur_d >> 48) & 0xFF);
        uint8_t cur_age = (cur_meta >> 2) & 63;
        int quality = cur_depth - (tt.current_age - cur_age) * 4;
        if (quality < lowest_quality) {
            lowest_quality = quality;
            replace_idx = i;
        }
    }

    uint64_t data = static_cast<uint64_t>(best_move.raw())
                  | (static_cast<uint64_t>(static_cast<uint16_t>(norm_score)) << 16)
                  | (static_cast<uint64_t>(static_cast<uint16_t>(s_eval)) << 32)
                  | (static_cast<uint64_t>(static_cast<uint8_t>(s_depth)) << 48)
                  | (static_cast<uint64_t>(new_meta) << 56);

    cluster.entries[replace_idx].data = data;
    cluster.entries[replace_idx].key = key ^ data;
}

} // namespace tigerfish
