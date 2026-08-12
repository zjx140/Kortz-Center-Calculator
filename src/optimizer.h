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

struct EqualShare {
    std::int64_t perPlayer = 0;
    std::int64_t roundingRemainder = 0;
};

struct EliteChallengeReference {
    std::int64_t lootValue = 0;
    std::int64_t buyerDesignatedBonusPerPlayer = 0;
    std::int64_t buyerDesignatedBonusValue = 0;
    std::int64_t guaranteedTotalValue = 0;
    std::int64_t bonusPerPlayer = 0;
    std::int64_t teamBonusValue = 0;
    std::int64_t referenceTotalValue = 0;
    bool available = false;
    bool allBagsFull = false;
    bool sameAsPrimaryPlan = false;
    EqualShare lootShare;
    EqualShare guaranteedShare;
    EqualShare referenceShare;
    std::vector<PlayerResult> players;
};

struct OptimizationResult {
    std::int64_t totalValue = 0;
    std::int64_t lootValue = 0;
    std::int64_t designatedBonusPerPlayer = 0;
    std::int64_t designatedBonusValue = 0;
    int excluded202Quantity = 0;
    int excludedDesignated202Quantity = 0;
    int designatedQuantity = 0;
    int playerCount = 1;
    bool allBagsFull = false;
    bool exactFillRequired = false;
    bool allDesignatedTaken = false;
    bool designatedBonusEarned = false;
    EqualShare lootShare;
    EqualShare guaranteedShare;
    std::vector<PlayerResult> players;
    EliteChallengeReference eliteChallenge;
};

OptimizationResult optimizeLoot(const std::vector<LootInput>& inputs,
                                int playerCount,
                                int capacityPerPlayerPercent = 100,
                                std::int64_t designatedBonusPerPlayer = 0,
                                std::int64_t eliteChallengeBonusPerPlayer = 0);

}  // namespace kortz
