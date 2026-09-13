#include "../../engine/search.cpp"
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>

using namespace std;
using namespace std::chrono;

int main() {
    init_rays();
    init_magics();

    const string fen = "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R1BQKB1R w KQ d6 0 7";
    const int thread_counts[] = {1, 12,24};
    const int time_limits[] = {2000, 5000, 10000};

    cout << "position=middlegame warmup=500ms\n";
    cout << "threads,time_ms,elapsed_ms,depth,nodes,nps,tt_hits,tt_misses,bestmove\n";

    for (int threads : thread_counts) {
        for (int limit : time_limits) {
            Board board;
            if (!board.set_fen(fen)) return 1;

            Engine engine;
           // engine.thread_count = threads;
           // engine.configure_search(0, 500, nullptr);
           // engine.best_move(board, 256);

            engine.configure_search(0, limit, nullptr);
            auto start = steady_clock::now();
            uint16_t best = engine.best_move(board, 256);
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
            uint64_t nps = elapsed > 0 ? engine.nodes_searched * 1000 / elapsed : engine.nodes_searched;

            cout << threads << ',' << limit << ',' << elapsed << ','
                 << engine.completed_depth << ',' << engine.nodes_searched << ','
                 << nps << ',' << engine.tt.total_hits.load() << ','
                 << engine.tt.total_misses.load() << ','
                 << (best ? move_to_uci(best) : "0000") << '\n';
        }
    }
    return 0;
}
