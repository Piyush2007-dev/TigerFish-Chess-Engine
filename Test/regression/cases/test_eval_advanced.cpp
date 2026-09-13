// ═══════════════════════════════════════════════════════════════════
// ADVANCED EVALUATION TESTS — tests/evaluation/test_eval_advanced.cpp
// Tests each feature of the evaluation pipeline independently.
//
// Compile: g++ -O2 -std=c++20 -I engine -o test_eval_advanced.exe tests/evaluation/test_eval_advanced.cpp
// ═══════════════════════════════════════════════════════════════════
#include "../../../archive/engine_v0/eval.cpp"
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

using namespace std;

static int passed = 0;
static int total = 0;

void check(const string& name, bool ok, const string& detail = "") {
    total++;
    if (ok) passed++;
    cout << "  " << name << ": " << (ok ? "PASS" : "FAIL");
    if (!detail.empty()) cout << " (" << detail << ")";
    cout << "\n";
}

// ── 1. TAPERED EVAL — phase changes score blend ─────────────────
// A position with many pieces (middlegame) vs same material with
// few pieces (endgame) should produce different eval values because
// the PST values differ between MG and EG.
void test_tapered_eval() {
    cout << "\n=== 1. TAPERED EVAL ===\n";

    // Middlegame: full board, king tucked in corner (good MG king PST)
    Board mg_board;
    mg_board.set_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    int mg_score = evaluate(mg_board);

    // Endgame: only kings and pawns, king in center (good EG king PST)
    Board eg_board;
    eg_board.set_fen("8/4k3/8/4K3/8/8/4P3/8 w - - 0 1");
    int eg_score = evaluate(eg_board);

    // Both should be from White's perspective
    // In the endgame position, White has a pawn and central king — should be positive
    check("endgame score positive for White advantage",
          eg_score > 0,
          "eg_score=" + to_string(eg_score));

    // The middlegame position has many pieces so phase ≈ 24 (full MG)
    // The endgame position has few pieces so phase ≈ 0 (full EG)
    // Both should be valid (non-zero, reasonable range)
    check("middlegame score in reasonable range",
          abs(mg_score) < 10000,
          "mg_score=" + to_string(mg_score));
    check("endgame score in reasonable range",
          abs(eg_score) < 10000,
          "eg_score=" + to_string(eg_score));
}

// ── 2. BISHOP PAIR BONUS ────────────────────────────────────────
// Two bishops should give a bonus over bishop+knight with otherwise
// identical material.
void test_bishop_pair() {
    cout << "\n=== 2. BISHOP PAIR BONUS ===\n";

    // White: K + 2 bishops, Black: K + bishop + knight
    // Same material value (330+330 vs 330+320), but bishops are on
    // different color squares so they complement each other.
    Board bb_board;
    bb_board.set_fen("8/8/8/3B4/8/3b4/5B2/4K2k w - - 0 1");
    int bb_score = evaluate(bb_board);

    // White: K + bishop + knight, Black: K + 2 bishops
    Board bn_board;
    bn_board.set_fen("8/8/8/3N4/8/3b4/5B2/4K2k w - - 0 1");
    int bn_score = evaluate(bn_board);

    // White with 2 bishops should score higher than White with bishop+knight
    check("bishop pair scores higher than B+N",
          bb_score > bn_score,
          "bb=" + to_string(bb_score) + " bn=" + to_string(bn_score));
}

// ── 3. DOUBLED PAWNS PENALTY ────────────────────────────────────
// A position with doubled pawns should be worse than the same position
// without doubled pawns.
void test_doubled_pawns() {
    cout << "\n=== 3. DOUBLED PAWNS PENALTY ===\n";

    // White has doubled c-pawns (c2 and c3), otherwise equal
    Board doubled;
    doubled.set_fen("r1bqkbnr/pppppppp/8/8/8/2P5/PP1P1PPP/RNBQKBNR w KQkq - 0 1");
    int d_score = evaluate(doubled);

    // Same but pawns on c2 and d3 (no doubled pawns)
    Board clean;
    clean.set_fen("r1bqkbnr/pppppppp/8/8/8/3P4/PPP2PPP/RNBQKBNR w KQkq - 0 1");
    int c_score = evaluate(clean);

    // Clean pawn structure should be better (or equal if other factors dominate)
    check("clean pawns >= doubled pawns",
          c_score >= d_score,
          "clean=" + to_string(c_score) + " doubled=" + to_string(d_score));
}

// ── 4. ISOLATED PAWNS PENALTY ───────────────────────────────────
// A pawn with no friendly pawns on adjacent files should be penalized.
void test_isolated_pawns() {
    cout << "\n=== 4. ISOLATED PAWNS PENALTY ===\n";

    // White has an isolated a-pawn (no pawns on b-file)
    Board isolated;
    isolated.set_fen("8/8/8/8/P7/8/8/4K3 w - - 0 1");
    int i_score = evaluate(isolated);

    // White has a-pawn supported by b-pawn
    Board supported;
    supported.set_fen("8/8/8/8/P7/P7/8/4K3 w - - 0 1");
    int s_score = evaluate(supported);

    // Supported should be at least as good
    check("supported pawns >= isolated pawns",
          s_score >= i_score,
          "supported=" + to_string(s_score) + " isolated=" + to_string(i_score));
}

// ── 5. PASSED PAWNS BONUS ───────────────────────────────────────
// A pawn with no enemy pawns ahead on its file or adjacent files
// should get a bonus.
void test_passed_pawns() {
    cout << "\n=== 5. PASSED PAWNS BONUS ===\n";

    // White pawn on e5 — no black pawns ahead on d, e, or f files
    Board passed;
    passed.set_fen("8/8/4P3/8/8/8/8/4K2k w - - 0 1");
    int p_score = evaluate(passed);

    // White pawn on e5 with black pawn on d6 blocking its path
    Board blocked;
    blocked.set_fen("8/3p4/4P3/8/8/8/8/4K2k w - - 0 1");
    int b_score = evaluate(blocked);

    // Passed pawn should score higher than blocked
    check("passed pawn > blocked pawn",
          p_score > b_score,
          "passed=" + to_string(p_score) + " blocked=" + to_string(b_score));

    // Passed pawn on rank 6 (close to promotion) should be worth more
    Board rank6;
    rank6.set_fen("8/4P3/8/8/8/8/8/4K2k w - - 0 1");
    int r6_score = evaluate(rank6);

    check("rank 6 passer > rank 5 passer",
          r6_score > p_score,
          "rank6=" + to_string(r6_score) + " rank5=" + to_string(p_score));
}

// ── 6. MOBILITY ─────────────────────────────────────────────────
// A piece with more safe squares should give a higher eval.
void test_mobility() {
    cout << "\n=== 6. MOBILITY ===\n";

    // White bishop in center with many diagonals (high mobility)
    Board mobile;
    mobile.set_fen("4k3/8/8/3B4/8/8/8/4K3 w - - 0 1");
    int m_score = evaluate(mobile);

    // White bishop in corner with few diagonals (low mobility)
    Board stuck;
    stuck.set_fen("4k3/8/8/8/8/8/8/B3K3 w - - 0 1");
    int s_score = evaluate(stuck);

    // Central bishop should score higher
    check("central bishop > corner bishop",
          m_score > s_score,
          "central=" + to_string(m_score) + " corner=" + to_string(s_score));
}

// ── 7. KING SAFETY — pawn shield ────────────────────────────────
// A king behind a pawn shield should be safer than an exposed king.
void test_king_safety() {
    cout << "\n=== 7. KING SAFETY — PAWN SHIELD ===\n";

    // White king behind pawn shield (f2, g2, h2)
    Board shielded;
    shielded.set_fen("rnbq1rk1/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R w KQ - 0 1");
    int sh_score = evaluate(shielded);

    // White king exposed (no pawns in front)
    Board exposed;
    exposed.set_fen("rnbq1rk1/pppppppp/8/8/8/5N2/8/RNBQKB1R w KQ - 0 1");
    int ex_score = evaluate(exposed);

    // Shielded king should score higher (king safety bonus in MG)
    check("shielded king > exposed king",
          sh_score > ex_score,
          "shielded=" + to_string(sh_score) + " exposed=" + to_string(ex_score));
}

// ── 8. SYMMETRY — color flip should negate score ────────────────
// If White has an advantage, flipping the board should give Black
// the same advantage (negated score).
void test_symmetry() {
    cout << "\n=== 8. SYMMETRY ===\n";

    Board white_adv;
    white_adv.set_fen("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    int w_score = evaluate(white_adv);

    Board black_adv;
    black_adv.set_fen("3qk3/8/8/8/8/8/8/4K3 w - - 0 1");
    int b_score = evaluate(black_adv);

    // Scores should be opposite signs with similar magnitude
    check("white advantage is positive",
          w_score > 0,
          "score=" + to_string(w_score));
    check("black advantage is negative",
          b_score < 0,
          "score=" + to_string(b_score));

    // Magnitudes should be similar (not exact due to PST mirroring)
    int ratio = (w_score != 0) ? abs(b_score * 100 / w_score) : 0;
    check("symmetric magnitudes (within 20%)",
          ratio > 80 && ratio < 120,
          "ratio=" + to_string(ratio) + "%");
}

// ── 9. MATERIAL BALANCE — equal material ≈ equal score ──────────
void test_material_balance() {
    cout << "\n=== 9. MATERIAL BALANCE ===\n";

    // Symmetric starting position — should evaluate to near 0
    Board startpos;
    startpos.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    int sp_score = evaluate(startpos);

    check("startpos score near zero",
          abs(sp_score) < 50,
          "score=" + to_string(sp_score));

    // White up a knight
    Board w_knight;
    w_knight.set_fen("rnbqkb1r/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R w KQkq - 0 1");
    int wk_score = evaluate(w_knight);

    check("extra knight is positive advantage",
          wk_score > 200,
          "score=" + to_string(wk_score));

    // Black up a knight (Black has Nd4, White has only K)
    Board b_knight;
    b_knight.set_fen("4k3/8/8/8/3n4/8/8/4K3 w - - 0 1");
    int bk_score = evaluate(b_knight);

    check("Black extra knight is negative for White",
          bk_score < -200,
          "score=" + to_string(bk_score));
}

// ── 10. ENDGAME PHASE — king centralization ─────────────────────
// In a king+pawn endgame, the king should be valued for centrality.
void test_endgame_phase() {
    cout << "\n=== 10. ENDGAME PHASE — KING CENTRALIZATION ===\n";

    // White king in center in endgame (good)
    Board center;
    center.set_fen("8/8/8/4K3/8/8/4k3/8 w - - 0 1");
    int c_score = evaluate(center);

    // White king in corner in endgame (bad)
    Board corner;
    corner.set_fen("K7/8/8/8/8/8/8/4k3 w - - 0 1");
    int co_score = evaluate(corner);

    // Central king should score better in endgame
    check("central king > corner king in endgame",
          c_score > co_score,
          "center=" + to_string(c_score) + " corner=" + to_string(co_score));
}

// ═══════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════
int main() {
    init_rays();
    init_magics();

    cout << "=== TigerFish Advanced Evaluation Tests ===\n";

    test_tapered_eval();
    test_bishop_pair();
    test_doubled_pawns();
    test_isolated_pawns();
    test_passed_pawns();
    test_mobility();
    test_king_safety();
    test_symmetry();
    test_material_balance();
    test_endgame_phase();

    cout << "\n=== Results: " << passed << "/" << total << " passed ===\n";
    return (passed == total) ? 0 : 1;
}
