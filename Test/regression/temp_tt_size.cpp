#include "../../engine/search.cpp"
#include <iostream>

int main() {
    std::cout << "entry_bytes=" << sizeof(TranspositionEntry) << '\n';
    std::cout << "table_bytes=" << sizeof(TranspositionEntry) * TranspositionTable::TABLE_SIZE << '\n';
    std::cout << "table_mib=" << (sizeof(TranspositionEntry) * TranspositionTable::TABLE_SIZE) / (1024.0 * 1024.0) << '\n';
}
