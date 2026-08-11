#include "optimizer.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    using kortz::LootInput;

    {
        const std::vector<LootInput> inputs = {
            {L"画作", L"普通区域", 50, 122500, 1, false},
            {L"宝石", L"202展览室", 30, 127500, 2, true},
            {L"戒指", L"普通区域", 10, 35000, 4, false},
        };
        const auto solo = kortz::optimizeLoot(inputs, 1);
        check(solo.totalValue == 262500, "solo maximum value");
        check(solo.players.size() == 1, "solo player count");
        check(solo.players[0].usedCapacityPercent == 90, "solo used capacity");
        check(solo.excluded202Quantity == 2, "solo excludes 202 loot");
    }

    {
        const std::vector<LootInput> inputs = {
            {L"画作", L"普通区域", 50, 122500, 2, false},
            {L"宝石", L"202展览室", 30, 127500, 4, true},
            {L"戒指", L"普通区域", 10, 35000, 8, false},
        };
        const auto result = kortz::optimizeLoot(inputs, 2);
        check(result.allBagsFull, "two players both full");
        check(result.exactFillRequired, "multiplayer requires exact fill");
        check(result.totalValue == 790000, "two-player global maximum value");
        check(result.players.size() == 2, "two-player result count");
        check(result.players[0].usedCapacityPercent == 100, "player one full");
        check(result.players[1].usedCapacityPercent == 100, "player two full");
        check(result.excluded202Quantity == 0, "multiplayer includes 202 loot");
    }

    {
        const std::vector<LootInput> inputs = {
            {L"半包", L"普通区域", 50, 100, 3, false},
            {L"三成", L"202展览室", 30, 80, 3, true},
            {L"两成", L"普通区域", 20, 60, 3, false},
        };
        const auto result = kortz::optimizeLoot(inputs, 3);
        check(result.allBagsFull, "three players all full");
        check(result.players.size() == 3, "three-player result count");
        for (const auto& player : result.players) {
            check(player.usedCapacityPercent == 100, "each of three players is full");
        }
    }

    {
        const std::vector<LootInput> inputs = {
            {L"三成目标", L"普通区域", 30, 100, 6, false},
            {L"两成目标", L"普通区域", 20, 75, 1, false},
        };
        const auto result = kortz::optimizeLoot(inputs, 2);
        check(!result.allBagsFull, "packing-incompatible loot is rejected as full");
        check(result.players[0].usedCapacityPercent < 100 ||
              result.players[1].usedCapacityPercent < 100,
              "inexact fallback shows an underfilled player");
    }

    {
        const std::vector<LootInput> inputs = {
            {L"A", L"普通区域", 50, 100, 6, false},
            {L"B", L"普通区域", 30, 90, 8, false},
            {L"C", L"普通区域", 20, 70, 8, false},
        };
        const auto result = kortz::optimizeLoot(inputs, 4);
        check(result.allBagsFull, "four players all full");
        check(result.players.size() == 4, "four-player result count");
        for (const auto& player : result.players) {
            check(player.usedCapacityPercent == 100, "each of four players is full");
        }
    }

    std::cout << "optimizer tests passed\n";
    return 0;
}
