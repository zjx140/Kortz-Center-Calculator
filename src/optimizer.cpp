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
    bool buyerDesignated;
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

struct Candidate {
    bool available = false;
    bool allBagsFull = false;
    bool allDesignatedTaken = false;
    bool bonusEarned = false;
    int stateKey = 0;
    int minimumUsed = 0;
    int totalUsed = 0;
    std::int64_t lootValue = 0;
    std::int64_t totalValue = 0;
    std::shared_ptr<const DecisionNode> decision;
};

constexpr int kMaximumPlayers = 4;

}  // namespace

OptimizationResult optimizeLoot(const std::vector<LootInput>& inputs,
                                int playerCount,
                                int capacityPerPlayerPercent,
                                std::int64_t designatedBonusPerPlayer,
                                std::int64_t eliteChallengeBonusPerPlayer) {
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
    bool designatedSetUnavailable = false;
    for (std::size_t inputIndex = 0; inputIndex < inputs.size(); ++inputIndex) {
        const auto& input = inputs[inputIndex];
        if (input.quantity <= 0 || input.capacityPercent <= 0 || input.unitValue < 0) {
            continue;
        }
        if (input.buyerDesignated) {
            result.designatedQuantity += input.quantity;
        }
        if (input.requiresMultiplayer && playerCount == 1) {
            result.excluded202Quantity += input.quantity;
            if (input.buyerDesignated) {
                result.excludedDesignated202Quantity += input.quantity;
            }
            designatedSetUnavailable = designatedSetUnavailable || input.buyerDesignated;
            continue;
        }
        const int usableQuantity = std::min(
            input.quantity,
            playerCount * capacityPerPlayerPercent / input.capacityPercent);
        if (input.buyerDesignated && usableQuantity < input.quantity) {
            designatedSetUnavailable = true;
        }
        for (int copy = 0; copy < usableQuantity; ++copy) {
            items.push_back({inputIndex, input.capacityPercent / capacityUnit,
                             input.unitValue, input.buyerDesignated});
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

    const auto processItems = [&](const std::vector<ExpandedItem>& orderedItems,
                                  bool requireDesignated) {
        std::unordered_map<int, StateEntry> states;
        states.emplace(0, StateEntry{});
        for (const auto& item : orderedItems) {
            const bool maySkip = !requireDesignated || !item.buyerDesignated;
            std::unordered_map<int, StateEntry> nextStates = maySkip ? states :
                std::unordered_map<int, StateEntry>{};
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
            if (states.empty()) {
                break;
            }
        }
        return states;
    };

    std::array<int, kMaximumPlayers> fullState{};
    for (int player = 0; player < playerCount; ++player) {
        fullState[static_cast<std::size_t>(player)] = scaledCapacity;
    }
    const int fullStateKey = encodeState(fullState);

    const auto selectCandidate = [&](const std::unordered_map<int, StateEntry>& states,
                                     bool allDesignatedTaken) {
        Candidate candidate;
        if (states.empty()) {
            return candidate;
        }
        int selectedStateKey = 0;
        const auto full = states.find(fullStateKey);
        if (result.exactFillRequired && full != states.end()) {
            selectedStateKey = fullStateKey;
            candidate.allBagsFull = true;
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
                    (entry.value > bestValue ||
                     (entry.value == bestValue && totalUsed > bestTotalUsed));
                const bool betterForMultiple = playerCount > 1 &&
                    (minimumUsed > bestMinimumUsed ||
                     (minimumUsed == bestMinimumUsed && totalUsed > bestTotalUsed) ||
                     (minimumUsed == bestMinimumUsed && totalUsed == bestTotalUsed &&
                      entry.value > bestValue));
                if (betterForSolo || betterForMultiple) {
                    selectedStateKey = stateKey;
                    bestMinimumUsed = minimumUsed;
                    bestTotalUsed = totalUsed;
                    bestValue = entry.value;
                }
            }
            const auto selectedUsed = decodeState(selectedStateKey);
            candidate.allBagsFull = std::all_of(
                selectedUsed.begin(), selectedUsed.begin() + playerCount,
                [scaledCapacity](int used) { return used == scaledCapacity; });
        }

        const auto selected = states.find(selectedStateKey);
        if (selected == states.end()) {
            return candidate;
        }
        const auto selectedUsed = decodeState(selectedStateKey);
        candidate.available = true;
        candidate.stateKey = selectedStateKey;
        candidate.allDesignatedTaken = allDesignatedTaken;
        candidate.bonusEarned = allDesignatedTaken && result.designatedQuantity > 0 &&
                                designatedBonusPerPlayer > 0;
        candidate.lootValue = selected->second.value;
        candidate.totalValue = candidate.lootValue +
            (candidate.bonusEarned ? designatedBonusPerPlayer * playerCount : 0);
        candidate.minimumUsed = selectedUsed[0];
        for (int player = 0; player < playerCount; ++player) {
            const int used = selectedUsed[static_cast<std::size_t>(player)];
            candidate.minimumUsed = std::min(candidate.minimumUsed, used);
            candidate.totalUsed += used;
        }
        candidate.decision = selected->second.decision;
        return candidate;
    };

    const auto regularStates = processItems(items, false);
    const Candidate regularCandidate = selectCandidate(regularStates, false);

    Candidate designatedCandidate;
    if (result.designatedQuantity > 0 && !designatedSetUnavailable) {
        std::vector<ExpandedItem> designatedFirst = items;
        std::stable_sort(designatedFirst.begin(), designatedFirst.end(),
                         [](const ExpandedItem& left, const ExpandedItem& right) {
                             return left.buyerDesignated && !right.buyerDesignated;
                         });
        const auto designatedStates = processItems(designatedFirst, true);
        designatedCandidate = selectCandidate(designatedStates, true);
    }

    const auto candidateIsBetter = [&](const Candidate& left, const Candidate& right) {
        if (!left.available) {
            return false;
        }
        if (!right.available) {
            return true;
        }
        if (result.exactFillRequired && left.allBagsFull != right.allBagsFull) {
            return left.allBagsFull;
        }
        if (left.allBagsFull || !result.exactFillRequired) {
            if (left.totalValue != right.totalValue) {
                return left.totalValue > right.totalValue;
            }
            return left.bonusEarned && !right.bonusEarned;
        }
        if (left.minimumUsed != right.minimumUsed) {
            return left.minimumUsed > right.minimumUsed;
        }
        if (left.totalUsed != right.totalUsed) {
            return left.totalUsed > right.totalUsed;
        }
        return left.totalValue > right.totalValue;
    };

    const Candidate selectedCandidate =
        candidateIsBetter(designatedCandidate, regularCandidate) ?
        designatedCandidate : regularCandidate;
    if (!selectedCandidate.available) {
        return result;
    }

    result.allBagsFull = selectedCandidate.allBagsFull;
    result.allDesignatedTaken = selectedCandidate.allDesignatedTaken;
    result.designatedBonusEarned = selectedCandidate.bonusEarned;
    result.designatedBonusPerPlayer =
        selectedCandidate.bonusEarned ? designatedBonusPerPlayer : 0;
    result.designatedBonusValue =
        selectedCandidate.bonusEarned ? designatedBonusPerPlayer * playerCount : 0;
    result.lootValue = selectedCandidate.lootValue;
    result.totalValue = selectedCandidate.totalValue;
    result.lootShare = {
        result.lootValue / playerCount,
        result.lootValue % playerCount,
    };
    result.guaranteedShare = {
        result.totalValue / playerCount,
        result.totalValue % playerCount,
    };

    const auto buildPlayerResults = [&](const Candidate& candidate) {
        std::vector<std::vector<int>> chosenCounts(
            static_cast<std::size_t>(playerCount), std::vector<int>(inputs.size(), 0));
        for (auto decision = candidate.decision; decision; decision = decision->previous) {
            ++chosenCounts[static_cast<std::size_t>(decision->playerIndex)][decision->inputIndex];
        }

        std::vector<PlayerResult> playerResults(static_cast<std::size_t>(playerCount));
        const auto selectedUsed = decodeState(candidate.stateKey);
        for (int player = 0; player < playerCount; ++player) {
            auto& playerResult = playerResults[static_cast<std::size_t>(player)];
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
        return playerResults;
    };

    result.players = buildPlayerResults(selectedCandidate);

    if (designatedCandidate.available && designatedCandidate.allDesignatedTaken &&
        eliteChallengeBonusPerPlayer > 0) {
        auto& elite = result.eliteChallenge;
        elite.available = true;
        elite.allBagsFull = designatedCandidate.allBagsFull;
        elite.lootValue = designatedCandidate.lootValue;
        elite.buyerDesignatedBonusPerPlayer =
            designatedCandidate.bonusEarned ? designatedBonusPerPlayer : 0;
        elite.buyerDesignatedBonusValue =
            designatedCandidate.bonusEarned ? designatedBonusPerPlayer * playerCount : 0;
        elite.guaranteedTotalValue = designatedCandidate.totalValue;
        elite.bonusPerPlayer = eliteChallengeBonusPerPlayer;
        elite.teamBonusValue = eliteChallengeBonusPerPlayer * playerCount;
        elite.referenceTotalValue = elite.guaranteedTotalValue + elite.teamBonusValue;
        elite.lootShare = {
            elite.lootValue / playerCount,
            elite.lootValue % playerCount,
        };
        elite.guaranteedShare = {
            elite.guaranteedTotalValue / playerCount,
            elite.guaranteedTotalValue % playerCount,
        };
        elite.referenceShare = {
            elite.referenceTotalValue / playerCount,
            elite.referenceTotalValue % playerCount,
        };
        elite.players = buildPlayerResults(designatedCandidate);
        elite.sameAsPrimaryPlan = elite.players.size() == result.players.size();
        for (std::size_t playerIndex = 0;
             elite.sameAsPrimaryPlan && playerIndex < elite.players.size(); ++playerIndex) {
            const auto& elitePlayer = elite.players[playerIndex];
            const auto& primaryPlayer = result.players[playerIndex];
            if (elitePlayer.totalValue != primaryPlayer.totalValue ||
                elitePlayer.usedCapacityPercent != primaryPlayer.usedCapacityPercent ||
                elitePlayer.selections.size() != primaryPlayer.selections.size()) {
                elite.sameAsPrimaryPlan = false;
                break;
            }
            for (std::size_t selectionIndex = 0;
                 selectionIndex < elitePlayer.selections.size(); ++selectionIndex) {
                const auto& eliteSelection = elitePlayer.selections[selectionIndex];
                const auto& primarySelection = primaryPlayer.selections[selectionIndex];
                if (eliteSelection.inputIndex != primarySelection.inputIndex ||
                    eliteSelection.quantity != primarySelection.quantity) {
                    elite.sameAsPrimaryPlan = false;
                    break;
                }
            }
        }
    }
    return result;
}

}  // namespace kortz
