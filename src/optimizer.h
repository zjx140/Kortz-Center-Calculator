#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kortz {

struct LootInput {
    std::wstring name;
    std::wstring location;
    int capacityPercent = 0;
    std::int64_t unitValue = 0;
    int quantity = 0;
    bool requiresMultiplayer = false;
    bool buyerDesignated = false;
};

struct LootSelection {
    std::size_t inputIndex = 0;
    int quantity = 0;
};

struct PlayerResult {
    std::int64_t totalValue = 0;
    int usedCapacityPercent = 0;
    std::vector<LootSelection> selections;
};

struct OptimizationResult {
    std::int64_t totalValue = 0;
    std::int64_t lootValue = 0;
    std::int64_t designatedBonusValue = 0;
    int excluded202Quantity = 0;
    int excludedDesignated202Quantity = 0;
    int designatedQuantity = 0;
    int playerCount = 1;
    bool allBagsFull = false;
    bool exactFillRequired = false;
    bool allDesignatedTaken = false;
    bool designatedBonusEarned = false;
    std::vector<PlayerResult> players;
};

OptimizationResult optimizeLoot(const std::vector<LootInput>& inputs,
                                int playerCount,
                                int capacityPerPlayerPercent = 100,
                                std::int64_t designatedSetBonus = 0);

}  // namespace kortz
