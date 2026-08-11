#include "optimizer.h"

#include <algorithm>
#include <array>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace kortz {
namespace {

struct ExpandedItem {
    std::size_t inputIndex;
    int weight;
    std::int64_t value;
};

struct DecisionNode {
    std::shared_ptr<const DecisionNode> previous;
    std::size_t inputIndex;
    int playerIndex;
};

struct StateEntry {
    std::int64_t value = 0;
    std::shared_ptr<const DecisionNode> decision;
};

constexpr int kMaximumPlayers = 4;

}  // namespace

OptimizationResult optimizeLoot(const std::vector<LootInput>& inputs,
                                int playerCount,
                                int capacityPerPlayerPercent) {
    OptimizationResult result;
    playerCount = std::clamp(playerCount, 1, kMaximumPlayers);
    result.playerCount = playerCount;
    result.exactFillRequired = playerCount >= 2;
    result.players.resize(static_cast<std::size_t>(playerCount));
    if (capacityPerPlayerPercent <= 0) {
        return result;
    }

    int capacityUnit = capacityPerPlayerPercent;
    for (const auto& input : inputs) {
        if (input.quantity > 0 && input.capacityPercent > 0 &&
            !(input.requiresMultiplayer && playerCount == 1)) {
            capacityUnit = std::gcd(capacityUnit, input.capacityPercent);
        }
    }
    capacityUnit = std::max(1, capacityUnit);
    const int scaledCapacity = capacityPerPlayerPercent / capacityUnit;
    const int stateBase = scaledCapacity + 1;

    std::vector<ExpandedItem> items;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        if (input.quantity <= 0 || input.capacityPercent <= 0 || input.unitValue < 0) {
            continue;
        }
        if (input.requiresMultiplayer && playerCount == 1) {
            result.excluded202Quantity += input.quantity;
            continue;
        }
        const int usableQuantity = std::min(
            input.quantity,
            playerCount * capacityPerPlayerPercent / input.capacityPercent);
        for (int copy = 0; copy < usableQuantity; ++copy) {
            items.push_back({i, input.capacityPercent / capacityUnit, input.unitValue});
        }
    }

    const auto encodeState = [stateBase, playerCount](const std::array<int, kMaximumPlayers>& used) {
        int key = 0;
        int multiplier = 1;
        for (int player = 0; player < playerCount; ++player) {
            key += used[static_cast<std::size_t>(player)] * multiplier;
            multiplier *= stateBase;
        }
        return key;
    };
    const auto decodeState = [stateBase, playerCount](int key) {
        std::array<int, kMaximumPlayers> used{};
        for (int player = 0; player < playerCount; ++player) {
            used[static_cast<std::size_t>(player)] = key % stateBase;
            key /= stateBase;
        }
        return used;
    };

    std::unordered_map<int, StateEntry> states;
    states.emplace(0, StateEntry{});
    for (const auto& item : items) {
        auto nextStates = states;
        for (const auto& [stateKey, entry] : states) {
            const auto used = decodeState(stateKey);
            for (int player = 0; player < playerCount; ++player) {
                const auto playerIndex = static_cast<std::size_t>(player);
                if (used[playerIndex] + item.weight > scaledCapacity) {
                    continue;
                }
                auto nextUsed = used;
                nextUsed[playerIndex] += item.weight;
                const int nextKey = encodeState(nextUsed);
                const auto candidateValue = entry.value + item.value;
                const auto existing = nextStates.find(nextKey);
                if (existing == nextStates.end() || candidateValue > existing->second.value) {
                    auto decision = std::make_shared<DecisionNode>();
                    decision->previous = entry.decision;
                    decision->inputIndex = item.inputIndex;
                    decision->playerIndex = player;
                    nextStates[nextKey] = {candidateValue, std::move(decision)};
                }
            }
        }
        states = std::move(nextStates);
    }

    std::array<int, kMaximumPlayers> fullState{};
    for (int player = 0; player < playerCount; ++player) {
        fullState[static_cast<std::size_t>(player)] = scaledCapacity;
    }
    const int fullStateKey = encodeState(fullState);
    int selectedStateKey = 0;

    if (result.exactFillRequired && states.find(fullStateKey) != states.end()) {
        selectedStateKey = fullStateKey;
        result.allBagsFull = true;
    } else {
        int bestMinimumUsed = -1;
        int bestTotalUsed = -1;
        std::int64_t bestValue = -1;
        for (const auto& [stateKey, entry] : states) {
            const auto used = decodeState(stateKey);
            int minimumUsed = used[0];
            int totalUsed = 0;
            for (int player = 0; player < playerCount; ++player) {
                minimumUsed = std::min(minimumUsed, used[static_cast<std::size_t>(player)]);
                totalUsed += used[static_cast<std::size_t>(player)];
            }
            const bool betterForSolo = playerCount == 1 &&
                (entry.value > bestValue || (entry.value == bestValue && totalUsed > bestTotalUsed));
            const bool betterForMultiple = playerCount > 1 &&
                (minimumUsed > bestMinimumUsed ||
                 (minimumUsed == bestMinimumUsed && totalUsed > bestTotalUsed) ||
                 (minimumUsed == bestMinimumUsed && totalUsed == bestTotalUsed && entry.value > bestValue));
            if (betterForSolo || betterForMultiple) {
                selectedStateKey = stateKey;
                bestMinimumUsed = minimumUsed;
                bestTotalUsed = totalUsed;
                bestValue = entry.value;
            }
        }
        const auto selectedUsed = decodeState(selectedStateKey);
        result.allBagsFull = std::all_of(
            selectedUsed.begin(), selectedUsed.begin() + playerCount,
            [scaledCapacity](int used) { return used == scaledCapacity; });
    }

    const auto selectedEntry = states.find(selectedStateKey);
    if (selectedEntry == states.end()) {
        return result;
    }
    result.totalValue = selectedEntry->second.value;
    std::vector<std::vector<int>> chosenCounts(
        static_cast<std::size_t>(playerCount), std::vector<int>(inputs.size(), 0));
    for (auto decision = selectedEntry->second.decision; decision; decision = decision->previous) {
        ++chosenCounts[static_cast<std::size_t>(decision->playerIndex)][decision->inputIndex];
    }

    const auto selectedUsed = decodeState(selectedStateKey);
    for (int player = 0; player < playerCount; ++player) {
        auto& playerResult = result.players[static_cast<std::size_t>(player)];
        playerResult.usedCapacityPercent =
            selectedUsed[static_cast<std::size_t>(player)] * capacityUnit;
        for (std::size_t inputIndex = 0; inputIndex < inputs.size(); ++inputIndex) {
            const int count = chosenCounts[static_cast<std::size_t>(player)][inputIndex];
            if (count > 0) {
                playerResult.selections.push_back({inputIndex, count});
                playerResult.totalValue += inputs[inputIndex].unitValue * count;
            }
        }
    }
    return result;
}

}  // namespace kortz
