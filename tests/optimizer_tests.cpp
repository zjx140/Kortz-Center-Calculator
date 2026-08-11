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

    {
        const std::vector<LootInput> inputs = {
            {L"指定目标", L"普通区域", 50, 100000, 1, false, true},
            {L"高价值普通目标", L"普通区域", 50, 130000, 2, false, false},
        };
        const auto result = kortz::optimizeLoot(inputs, 1, 100, 50000);
        check(result.designatedBonusEarned, "simple bonus changes the optimal plan");
        check(result.allDesignatedTaken, "all designated loot is selected");
        check(result.lootValue == 230000, "loot value excludes designated bonus");
        check(result.designatedBonusValue == 50000, "simple designated bonus value");
        check(result.totalValue == 280000, "total includes simple designated bonus");
    }

    {
        const std::vector<LootInput> inputs = {
            {L"指定目标", L"普通区域", 50, 100000, 1, false, true},
            {L"高价值普通目标", L"普通区域", 50, 160000, 2, false, false},
        };
        const auto result = kortz::optimizeLoot(inputs, 1, 100, 50000);
        check(!result.designatedBonusEarned, "bonus is skipped when ordinary loot pays more");
        check(result.lootValue == 320000, "higher non-designated loot wins");
        check(result.totalValue == 320000, "no bonus is added when designated set is incomplete");
    }

    {
        const std::vector<LootInput> inputs = {
            {L"两件指定目标", L"普通区域", 30, 50000, 2, false, true},
            {L"补足目标", L"普通区域", 40, 80000, 1, false, false},
        };
        const auto result = kortz::optimizeLoot(inputs, 1, 100, 100000);
        check(result.designatedBonusEarned, "hard bonus requires every designated copy");
        check(result.designatedQuantity == 2, "all designated quantities are counted");
        check(result.players[0].usedCapacityPercent == 100, "designated set packing is exact");
        check(result.totalValue == 280000, "hard bonus total value");
    }

    {
        const std::vector<LootInput> inputs = {
            {L"202指定目标", L"202展览室", 30, 127500, 1, true, true},
            {L"普通目标", L"普通区域", 50, 150000, 2, false, false},
        };
        const auto result = kortz::optimizeLoot(inputs, 1, 100, 100000);
        check(!result.designatedBonusEarned, "solo cannot earn bonus requiring 202 loot");
        check(result.excludedDesignated202Quantity == 1,
              "excluded designated 202 loot is reported");
        check(result.totalValue == 300000, "solo optimizes only reachable loot");
    }

    std::cout << "optimizer tests passed\n";
    return 0;
}
