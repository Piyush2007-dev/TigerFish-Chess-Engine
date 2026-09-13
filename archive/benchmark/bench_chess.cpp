#include <iostream>
#include <chrono>
#include <random>

#include "chess.cpp"

using namespace std;
using namespace std::chrono;

constexpr int NUM_RUNS = 10000;
constexpr int NUM_PLIES = 80;

int main()
{
    init_rays();
    init_magics();
    random_device rd;
    mt19937 rng(rd());

    MoveGenerator mg;

    double total_us = 0;
    long long total_positions = 0;
    long long total_moves = 0;

    for (int run = 0; run < NUM_RUNS; run++)
    {
        Board board;
        MoveList moves;

        for (int ply = 0; ply < NUM_PLIES; ply++)
        {
            moves.clear();

            auto start = high_resolution_clock::now();

            mg.generate_moves(board, moves);

            auto end = high_resolution_clock::now();

            if (moves.size() == 0)
                break;

            total_us += duration_cast<nanoseconds>(
                end - start).count();

            total_moves += moves.size();
            total_positions++;

            uniform_int_distribution<int> dist(
                0,
                moves.size() - 1);

            Move move =
                moves.move_list[dist(rng)];

            board.make_move(move, moves);
        }

        if ((run + 1) % 1000 == 0)
            cout << "Completed "
                 << run + 1
                 << " runs\n";
    }

    cout << "\nPositions: "
         << total_positions
         << '\n';

    cout << "Average moves: "
         << (double)total_moves / total_positions
         << '\n';

    cout << "Average time: "
         << total_us / total_positions
         << " ns\n";
}