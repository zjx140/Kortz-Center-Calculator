#include "optimizer.h"

#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr wchar_t kWindowClass[] = L"KortzCenterCalculatorWindow";
constexpr COLORREF kTextColor = RGB(29, 45, 50);
constexpr COLORREF kMutedColor = RGB(87, 107, 112);

enum ControlId {
    IDC_PLAYER_COUNT = 100,
    IDC_LANGUAGE,
    IDC_DIFFICULTY,
    IDC_TARGET_COMBO,
    IDC_VALUE_EDIT,
    IDC_QUANTITY_EDIT,
    IDC_DESIGNATED,
    IDC_ADD_BUTTON,
    IDC_REFERENCE_LABEL,
    IDC_LOOT_LIST,
    IDC_REMOVE_BUTTON,
    IDC_CLEAR_BUTTON,
    IDC_CALCULATE_BUTTON,
    IDC_RESULT_EDIT,
    IDC_STATUS_LABEL,
};

struct CatalogItem {
    std::wstring name;
    std::wstring location;
    int capacityPercent;
    std::int64_t minValue;
    std::int64_t maxValue;
    bool requiresMultiplayer;
};

HINSTANCE g_instance = nullptr;
HFONT g_font = nullptr;
HFONT g_titleFont = nullptr;
HBRUSH g_backgroundBrush = nullptr;
HWND g_mainWindow = nullptr;
HWND g_playerCountCombo = nullptr;
HWND g_playerCountLabel = nullptr;
HWND g_languageCombo = nullptr;
HWND g_languageLabel = nullptr;
HWND g_difficultyCombo = nullptr;
HWND g_difficultyLabel = nullptr;
HWND g_targetCombo = nullptr;
HWND g_valueEdit = nullptr;
HWND g_quantityEdit = nullptr;
HWND g_designatedCheck = nullptr;
HWND g_addButton = nullptr;
HWND g_referenceLabel = nullptr;
HWND g_lootList = nullptr;
HWND g_removeButton = nullptr;
HWND g_clearButton = nullptr;
HWND g_calculateButton = nullptr;
HWND g_resultEdit = nullptr;
HWND g_statusLabel = nullptr;
HWND g_inputGroup = nullptr;
HWND g_listGroup = nullptr;
HWND g_resultGroup = nullptr;
HWND g_titleLabel = nullptr;
HWND g_subtitleLabel = nullptr;
HWND g_targetLabel = nullptr;
HWND g_valueLabel = nullptr;
HWND g_quantityLabel = nullptr;

std::vector<CatalogItem> g_catalog;
std::vector<kortz::LootInput> g_inputs;
bool g_traditionalChinese = false;
bool g_uiInitialized = false;

void refreshLanguageUi();

std::wstring localize(std::wstring_view simplified) {
    if (!g_traditionalChinese || simplified.empty()) {
        return std::wstring(simplified);
    }
    const int sourceLength = static_cast<int>(simplified.size());
    const int convertedLength = LCMapStringEx(
        L"zh-CN", LCMAP_TRADITIONAL_CHINESE,
        simplified.data(), sourceLength, nullptr, 0, nullptr, nullptr, 0);
    if (convertedLength <= 0) {
        return std::wstring(simplified);
    }
    std::wstring converted(static_cast<std::size_t>(convertedLength), L'\0');
    LCMapStringEx(L"zh-CN", LCMAP_TRADITIONAL_CHINESE,
                  simplified.data(), sourceLength, converted.data(), convertedLength,
                  nullptr, nullptr, 0);
    return converted;
}

void setLocalizedText(HWND window, std::wstring_view simplified) {
    const auto text = localize(simplified);
    SetWindowTextW(window, text.c_str());
}

int showLocalizedMessage(std::wstring_view message, std::wstring_view title, UINT type) {
    const auto localizedMessage = localize(message);
    const auto localizedTitle = localize(title);
    return MessageBoxW(g_mainWindow, localizedMessage.c_str(), localizedTitle.c_str(), type);
}

std::wstring formatNumber(std::int64_t value) {
    const bool negative = value < 0;
    auto digits = std::to_wstring(negative ? -value : value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) {
        digits.insert(static_cast<std::size_t>(i), L",");
    }
    return (negative ? L"-" : L"") + digits;
}

std::wstring formatCurrency(std::int64_t value) {
    return L"$" + formatNumber(value);
}

std::wstring getWindowText(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
    if (length > 0) {
        GetWindowTextW(window, text.data(), length + 1);
    }
    text.resize(static_cast<std::size_t>(length));
    return text;
}

bool parsePositiveInteger(const std::wstring& text, std::int64_t& value) {
    std::wstring digits;
    for (const wchar_t ch : text) {
        if (std::iswdigit(ch)) {
            digits.push_back(ch);
        }
    }
    if (digits.empty()) {
        return false;
    }
    try {
        value = std::stoll(digits);
    } catch (...) {
        return false;
    }
    return value > 0;
}

int playerCount() {
    const int selection = ComboBox_GetCurSel(g_playerCountCombo);
    return selection >= 0 ? selection + 1 : 1;
}

bool isMultiplayer() {
    return playerCount() >= 2;
}

std::int64_t designatedBonus() {
    return ComboBox_GetCurSel(g_difficultyCombo) == 1 ? 100000 : 50000;
}

void addCatalogItems(const std::vector<std::wstring>& names,
                     const wchar_t* location,
                     int capacity,
                     std::int64_t minValue,
                     std::int64_t maxValue,
                     bool requiresMultiplayer) {
    for (const auto& name : names) {
        g_catalog.push_back({name, location, capacity, minValue, maxValue, requiresMultiplayer});
    }
}

void buildCatalog() {
    addCatalogItems({L"科卡尔戒指", L"装饰艺术戒指", L"古董戒指"},
                    L"普通区域", 10, 28000, 35000, false);
    addCatalogItems({L"皇家科卡尔蛋", L"欣喜科卡尔蛋", L"装饰科卡尔蛋", L"深渊科卡尔蛋",
                     L"绿色科卡尔蛋", L"飞龙科卡尔蛋", L"森林科卡尔蛋"},
                    L"普通区域", 20, 50000, 64000, false);
    addCatalogItems({L"科卡尔卡卡内特（红色尖晶石）", L"科卡尔卡卡内特（翡翠）",
                     L"科卡尔卡卡内特（蓝宝石）", L"科卡尔卡卡内特（帝王托帕石）",
                     L"科卡尔卡卡内特（黄钻石）", L"科卡尔卡卡内特（坦桑石）"},
                    L"普通区域", 30, 77500, 100000, false);
    addCatalogItems({L"大理石萨比诺斑克里奥尔马", L"象牙白荷兰温血马", L"金色土库曼马",
                     L"银色花斑马", L"浅粉色安达卢西亚马"},
                    L"普通区域", 30, 77500, 100000, false);
    addCatalogItems({L"勿忘死亡（翡翠）", L"勿忘死亡（红宝石）", L"勿忘死亡（金）",
                     L"勿忘死亡（紫晶）", L"勿忘死亡（蓝宝石）", L"勿忘死亡（钻石）"},
                    L"普通区域", 30, 77500, 100000, false);

    const std::vector<std::wstring> bracelets = {
        L"科卡尔手镯", L"法老手镯", L"拜占庭式圆环", L"古董圆环"};
    addCatalogItems(bracelets, L"普通区域", 10, 28000, 35000, false);
    addCatalogItems(bracelets, L"202展览室", 10, 42000, 53000, true);
    addCatalogItems({L"装饰艺术圆环"}, L"202展览室", 10, 42000, 53000, true);
    addCatalogItems({L"陨石碎片"}, L"202展览室", 20, 70000, 96000, true);

    const std::vector<std::wstring> statues = {
        L"生育女神像（红木）", L"生育女神像（金）", L"生育女神像（银）",
        L"生育女神像（铜）", L"生育女神像（象牙）"};
    addCatalogItems(statues, L"普通区域", 20, 50000, 64000, false);
    addCatalogItems(statues, L"202展览室", 20, 70000, 96000, true);

    addCatalogItems({L"祖母绿宝石", L"红宝石", L"黄托帕宝石", L"坦桑石宝石",
                     L"海蓝宝石", L"灰尖晶宝石", L"紫色蓝宝石"},
                    L"202展览室", 30, 100000, 127500, true);
    addCatalogItems({L"阿尔杰农的维纳斯（金）", L"阿尔杰农的维纳斯（银）",
                     L"阿尔杰农的维纳斯（铜）", L"阿尔杰农的维纳斯（象牙）",
                     L"阿尔杰农的维纳斯（大理石）"},
                    L"202展览室", 30, 100000, 127500, true);
    addCatalogItems({L"送客", L"我完蛋了？", L"见我否", L"公爵夫人", L"说明一下",
                     L"领袖", L"肯尼斯人道编辑", L"橙子镇"},
                    L"普通区域", 50, 102500, 122500, false);
    addCatalogItems({L"伟大回头路", L"勿忘蓝图"},
                    L"202展览室", 50, 140000, 162500, true);
    addCatalogItems({L"仙人掌朋友", L"金贵小狗", L"第69号秋千研究", L"猎人被猎"},
                    L"金库", 50, 70000, 92500, false);
    addCatalogItems({L"运送卡车战利品"}, L"运送卡车", 30, 105000, 140000, false);
}

HWND makeControl(DWORD exStyle,
                 const wchar_t* className,
                 const wchar_t* text,
                 DWORD style,
                 int id) {
    return CreateWindowExW(exStyle, className, text, style | WS_CHILD | WS_VISIBLE,
                           0, 0, 0, 0, g_mainWindow,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
}

void setFont(HWND window, HFONT font = nullptr) {
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : g_font), TRUE);
}

void rebuildPlayerCountOptions() {
    const int previousSelection = std::max(0, ComboBox_GetCurSel(g_playerCountCombo));
    ComboBox_ResetContent(g_playerCountCombo);
    const wchar_t* options[] = {L"1 人（单人）", L"2 人", L"3 人", L"4 人"};
    for (const auto* option : options) {
        const auto displayed = localize(option);
        ComboBox_AddString(g_playerCountCombo, displayed.c_str());
    }
    ComboBox_SetCurSel(g_playerCountCombo, previousSelection);
}

void rebuildDifficultyOptions() {
    const int previousSelection = std::max(0, ComboBox_GetCurSel(g_difficultyCombo));
    ComboBox_ResetContent(g_difficultyCombo);
    const wchar_t* options[] = {
        L"简单（奖励 $50,000）",
        L"困难（奖励 $100,000）",
    };
    for (const auto* option : options) {
        const auto displayed = localize(option);
        ComboBox_AddString(g_difficultyCombo, displayed.c_str());
    }
    ComboBox_SetCurSel(g_difficultyCombo, previousSelection);
}

void rebuildTargetOptions() {
    const int previousSelection = std::max(0, ComboBox_GetCurSel(g_targetCombo));
    ComboBox_ResetContent(g_targetCombo);
    for (const auto& item : g_catalog) {
        const std::wstring label = item.name + L"  ·  " + item.location + L"  ·  " +
                                   std::to_wstring(item.capacityPercent) + L"%";
        const auto displayed = localize(label);
        ComboBox_AddString(g_targetCombo, displayed.c_str());
    }
    if (!g_catalog.empty()) {
        ComboBox_SetCurSel(g_targetCombo,
            std::min(previousSelection, static_cast<int>(g_catalog.size()) - 1));
    }
}

void updateListColumnHeaders() {
    const wchar_t* columns[] = {
        L"目标", L"地点", L"单件价值", L"单件占用", L"数量", L"买家指定", L"状态"};
    for (int index = 0; index < 7; ++index) {
        auto displayed = localize(columns[index]);
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = displayed.data();
        ListView_SetColumn(g_lootList, index, &column);
    }
}

void updateReferenceLabel() {
    const int selection = ComboBox_GetCurSel(g_targetCombo);
    if (selection < 0 || static_cast<std::size_t>(selection) >= g_catalog.size()) {
        setLocalizedText(g_referenceLabel, L"请选择目标。实际价值以侦察结果为准。");
        return;
    }
    const auto& item = g_catalog[static_cast<std::size_t>(selection)];
    std::wstring text = L"参考区间：" + formatCurrency(item.minValue) + L" – " +
                        formatCurrency(item.maxValue) + L"    背包占用：" +
                        std::to_wstring(item.capacityPercent) + L"%    地点：" + item.location;
    if (item.requiresMultiplayer) {
        text += isMultiplayer() ? L"（当前人数可参与计算）" : L"（需要 2–4 人游玩）";
    }
    setLocalizedText(g_referenceLabel, text);
    SetWindowTextW(g_valueEdit, formatNumber(item.minValue).c_str());
}

void refreshListView() {
    ListView_DeleteAllItems(g_lootList);
    for (std::size_t i = 0; i < g_inputs.size(); ++i) {
        const auto& input = g_inputs[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        auto displayedName = localize(input.name);
        item.pszText = displayedName.data();
        item.lParam = static_cast<LPARAM>(i);
        ListView_InsertItem(g_lootList, &item);
        auto displayedLocation = localize(input.location);
        ListView_SetItemText(g_lootList, static_cast<int>(i), 1,
                             displayedLocation.data());
        auto value = formatCurrency(input.unitValue);
        ListView_SetItemText(g_lootList, static_cast<int>(i), 2, value.data());
        auto capacity = std::to_wstring(input.capacityPercent) + L"%";
        ListView_SetItemText(g_lootList, static_cast<int>(i), 3, capacity.data());
        auto quantity = std::to_wstring(input.quantity);
        ListView_SetItemText(g_lootList, static_cast<int>(i), 4, quantity.data());
        const auto designated = localize(input.buyerDesignated ? L"是" : L"否");
        ListView_SetItemText(g_lootList, static_cast<int>(i), 5,
                             const_cast<wchar_t*>(designated.c_str()));
        const std::wstring availability = localize(
            input.requiresMultiplayer && !isMultiplayer() ? L"已排除" : L"可参与");
        ListView_SetItemText(g_lootList, static_cast<int>(i), 6,
                             const_cast<wchar_t*>(availability.c_str()));
    }
    int designatedCount = 0;
    const auto totalQuantity = [&] {
        int count = 0;
        for (const auto& input : g_inputs) {
            count += input.quantity;
            if (input.buyerDesignated) {
                designatedCount += input.quantity;
            }
        }
        return count;
    }();
    const std::wstring status = L"已录入 " + std::to_wstring(g_inputs.size()) +
                                L" 类目标，共 " + std::to_wstring(totalQuantity) +
                                L" 件；买家指定 " + std::to_wstring(designatedCount) + L" 件";
    setLocalizedText(g_statusLabel, status);
}

void calculateAndDisplay() {
    if (g_inputs.empty()) {
        setLocalizedText(g_resultEdit,
            L"尚未录入侦察目标。\r\n\r\n请选择目标、填写侦察到的实际价值和数量，然后点击“加入清单”。");
        return;
    }

    const int players = playerCount();
    const auto bonus = designatedBonus();
    const auto result = kortz::optimizeLoot(g_inputs, players, 100, bonus);
    std::wostringstream output;
    output << L"游玩人数：" << players << L" 人    难度："
           << (bonus == 100000 ? L"困难" : L"简单") << L"\r\n";
    if (players >= 2 && !result.allBagsFull) {
        output << L"结论：现有目标无法让所有玩家都恰好装满 100% 背包。\r\n"
               << L"下面显示最接近的均衡分配，仅供调整侦察清单时参考。\r\n"
               << L"参考目标价值：" << formatCurrency(result.lootValue) << L"\r\n";
    } else {
        output << L"目标本身价值：" << formatCurrency(result.lootValue) << L"\r\n";
        if (players >= 2) {
            output << L"结果：所有玩家背包均已装满 100%。\r\n";
        }
    }
    if (result.designatedQuantity == 0) {
        output << L"指定目标奖励：未标记买家指定目标，不计奖励。\r\n";
    } else if (result.excludedDesignated202Quantity > 0) {
        output << L"指定目标奖励：无法获得；有 "
               << result.excludedDesignated202Quantity
               << L" 件买家指定目标位于 202 展览室，单人无法拿取。\r\n";
    } else if (result.designatedBonusEarned) {
        output << L"指定目标奖励：已拿齐 " << result.designatedQuantity << L" 件，获得 "
               << formatCurrency(result.designatedBonusValue) << L"。\r\n";
    } else {
        output << L"指定目标奖励：未获得；最高总收益方案没有拿齐全部 "
               << result.designatedQuantity << L" 件指定目标。\r\n";
    }
    output << (players >= 2 ? L"团队总收益：" : L"总收益：")
           << formatCurrency(result.totalValue) << L"\r\n\r\n";

    std::vector<int> chosen(g_inputs.size(), 0);
    for (std::size_t playerIndex = 0; playerIndex < result.players.size(); ++playerIndex) {
        const auto& player = result.players[playerIndex];
        output << L"玩家 " << (playerIndex + 1) << L"："
               << player.usedCapacityPercent << L"% / 100%，"
               << formatCurrency(player.totalValue) << L"\r\n";
        if (player.selections.empty()) {
            output << L"  • 无可拿取目标\r\n";
        } else {
            for (const auto& selection : player.selections) {
                chosen[selection.inputIndex] += selection.quantity;
                const auto& input = g_inputs[selection.inputIndex];
                const auto subtotal = input.unitValue * selection.quantity;
                output << L"  • " << (input.buyerDesignated ? L"【买家指定】" : L"")
                       << input.name << L"［" << input.location << L"］ × "
                       << selection.quantity << L"  —  "
                       << input.capacityPercent * selection.quantity << L"%，"
                       << formatCurrency(subtotal) << L"\r\n";
            }
        }
        output << L"\r\n";
    }

    bool hasUnselected = false;
    for (std::size_t i = 0; i < g_inputs.size(); ++i) {
        const int remaining = g_inputs[i].quantity - chosen[i];
        if (remaining > 0 && !(g_inputs[i].requiresMultiplayer && !isMultiplayer())) {
            if (!hasUnselected) {
                output << L"无需拿取：\r\n";
                hasUnselected = true;
            }
            output << L"  • " << (g_inputs[i].buyerDesignated ? L"【买家指定】" : L"")
                   << g_inputs[i].name << L"［" << g_inputs[i].location << L"］ × "
                   << remaining << L"\r\n";
        }
    }

    if (result.excluded202Quantity > 0) {
        output << L"\r\n提示：当前为单人游玩，已自动排除 "
               << result.excluded202Quantity << L" 件 202 展览室目标。";
    }
    setLocalizedText(g_resultEdit, output.str());
}

void addCurrentTarget() {
    const int selection = ComboBox_GetCurSel(g_targetCombo);
    if (selection < 0 || static_cast<std::size_t>(selection) >= g_catalog.size()) {
        showLocalizedMessage(L"请先选择侦察到的目标。", L"缺少目标",
                             MB_OK | MB_ICONINFORMATION);
        return;
    }
    const auto& catalogItem = g_catalog[static_cast<std::size_t>(selection)];
    std::int64_t value = 0;
    if (!parsePositiveInteger(getWindowText(g_valueEdit), value)) {
        showLocalizedMessage(L"请输入大于 0 的实际价值。可使用逗号，例如 127,500。",
                             L"价值无效", MB_OK | MB_ICONWARNING);
        SetFocus(g_valueEdit);
        return;
    }
    std::int64_t quantityValue = 0;
    if (!parsePositiveInteger(getWindowText(g_quantityEdit), quantityValue) || quantityValue > 50) {
        showLocalizedMessage(L"数量请输入 1 到 50 之间的整数。",
                             L"数量无效", MB_OK | MB_ICONWARNING);
        SetFocus(g_quantityEdit);
        return;
    }
    if (value < catalogItem.minValue || value > catalogItem.maxValue) {
        const std::wstring message =
            L"输入价值 " + formatCurrency(value) + L" 不在表格参考区间 " +
            formatCurrency(catalogItem.minValue) + L" – " + formatCurrency(catalogItem.maxValue) +
            L" 内。\r\n\r\n实际侦察值优先，是否仍然加入？";
        if (showLocalizedMessage(message, L"确认实际价值",
                                 MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            return;
        }
    }

    const bool buyerDesignated = Button_GetCheck(g_designatedCheck) == BST_CHECKED;
    g_inputs.push_back({catalogItem.name, catalogItem.location, catalogItem.capacityPercent,
                        value, static_cast<int>(quantityValue), catalogItem.requiresMultiplayer,
                        buyerDesignated});
    Button_SetCheck(g_designatedCheck, BST_UNCHECKED);
    refreshListView();
    calculateAndDisplay();
}

void removeSelectedTargets() {
    std::vector<int> selected;
    int item = -1;
    while ((item = ListView_GetNextItem(g_lootList, item, LVNI_SELECTED)) != -1) {
        selected.push_back(item);
    }
    if (selected.empty()) {
        showLocalizedMessage(L"请先在清单中选中要移除的行。", L"未选择目标",
                             MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::sort(selected.rbegin(), selected.rend());
    for (const int index : selected) {
        if (index >= 0 && static_cast<std::size_t>(index) < g_inputs.size()) {
            g_inputs.erase(g_inputs.begin() + index);
        }
    }
    refreshListView();
    calculateAndDisplay();
}

void layoutControls(int width, int height) {
    constexpr int margin = 24;
    const int contentWidth = std::max(760, width - margin * 2);
    const int comboWidth = std::max(320, contentWidth - 640);
    const int listTop = 276;
    const int available = std::max(390, height - listTop - margin);
    const int listGroupHeight = std::max(245, available * 52 / 100);
    const int listGroupBottom = listTop + listGroupHeight;
    const int footerTop = listGroupBottom - 51;
    constexpr int listFooterGap = 8;
    const int listViewTop = listTop + 28;
    const int listViewHeight = std::max(80, footerTop - listFooterGap - listViewTop);
    const int resultTop = listTop + listGroupHeight + 12;
    const int resultHeight = std::max(125, height - resultTop - margin);

    struct ControlPlacement {
        HWND window;
        int x;
        int y;
        int width;
        int height;
    };
    const std::array<ControlPlacement, 26> placements{{
        {g_titleLabel, margin, 15, contentWidth - 460, 40},
        {g_subtitleLabel, margin, 58, contentWidth - 460, 28},
        {g_languageLabel, width - margin - 445, 29, 85, 30},
        {g_languageCombo, width - margin - 355, 24, 120, 180},
        {g_playerCountLabel, width - margin - 220, 29, 80, 30},
        {g_playerCountCombo, width - margin - 135, 24, 135, 180},

        {g_inputGroup, margin, 98, contentWidth, 166},
        {g_targetLabel, margin + 18, 127, comboWidth, 25},
        {g_targetCombo, margin + 18, 153, comboWidth, 330},
        {g_valueLabel, margin + 30 + comboWidth, 127, 150, 25},
        {g_valueEdit, margin + 30 + comboWidth, 153, 150, 34},
        {g_quantityLabel, margin + 192 + comboWidth, 127, 70, 25},
        {g_quantityEdit, margin + 192 + comboWidth, 153, 70, 34},
        {g_designatedCheck, margin + 274 + comboWidth, 153, 150, 34},
        {g_addButton, margin + 434 + comboWidth, 151, 145, 38},
        {g_difficultyLabel, margin + 18, 207, 80, 30},
        {g_difficultyCombo, margin + 103, 202, 210, 180},
        {g_referenceLabel, margin + 330, 207, contentWidth - 348, 32},

        {g_listGroup, margin, listTop, contentWidth, listGroupHeight},
        {g_lootList, margin + 14, listViewTop, contentWidth - 28, listViewHeight},
        {g_statusLabel, margin + 18, footerTop, 490, 36},
        {g_removeButton, width - margin - 430, footerTop, 130, 36},
        {g_clearButton, width - margin - 290, footerTop, 120, 36},
        {g_calculateButton, width - margin - 160, footerTop, 145, 36},

        {g_resultGroup, margin, resultTop, contentWidth, resultHeight},
        {g_resultEdit, margin + 14, resultTop + 27, contentWidth - 28, resultHeight - 41},
    }};

    HDWP deferred = BeginDeferWindowPos(static_cast<int>(placements.size()));
    bool layoutSucceeded = deferred != nullptr;
    if (layoutSucceeded) {
        for (const auto& placement : placements) {
            deferred = DeferWindowPos(
                deferred, placement.window, nullptr,
                placement.x, placement.y, placement.width, placement.height,
                SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
            if (!deferred) {
                layoutSucceeded = false;
                break;
            }
        }
    }
    if (layoutSucceeded) {
        layoutSucceeded = EndDeferWindowPos(deferred) != FALSE;
    }
    if (!layoutSucceeded) {
        for (const auto& placement : placements) {
            MoveWindow(placement.window, placement.x, placement.y,
                       placement.width, placement.height, FALSE);
        }
    }

    if (g_lootList) {
        const int listWidth = std::max(
            760, contentWidth - 32 - GetSystemMetrics(SM_CXVSCROLL));
        constexpr int capacityWidth = 105;
        constexpr int quantityWidth = 70;
        constexpr int designatedWidth = 110;
        constexpr int statusWidth = 100;
        const int flexibleWidth = listWidth - capacityWidth - quantityWidth -
                                  designatedWidth - statusWidth;
        const int targetWidth = flexibleWidth * 52 / 100;
        const int locationWidth = flexibleWidth * 25 / 100;
        const int valueWidth = flexibleWidth - targetWidth - locationWidth;

        ListView_SetColumnWidth(g_lootList, 0, targetWidth);
        ListView_SetColumnWidth(g_lootList, 1, locationWidth);
        ListView_SetColumnWidth(g_lootList, 2, valueWidth);
        ListView_SetColumnWidth(g_lootList, 3, capacityWidth);
        ListView_SetColumnWidth(g_lootList, 4, quantityWidth);
        ListView_SetColumnWidth(g_lootList, 5, designatedWidth);
        ListView_SetColumnWidth(g_lootList, 6, statusWidth);
    }

    // Group boxes are transparent siblings, so repaint the complete parent and
    // all children after an atomic resize. This clears pixels exposed by controls
    // that became smaller instead of leaving their old rows or text behind.
    RedrawWindow(g_mainWindow, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void createUi() {
    g_titleLabel = makeControl(0, L"STATIC", L"科兹中心目标价值计算器", SS_LEFT, 0);
    g_subtitleLabel = makeControl(0, L"STATIC",
        L"录入侦察到的实际价值，自动求出 100% 背包容量内的最高价值组合。", SS_LEFT, 0);
    g_languageLabel = makeControl(0, L"STATIC", L"界面语言", SS_LEFT, 0);
    g_languageCombo = makeControl(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, IDC_LANGUAGE);
    ComboBox_AddString(g_languageCombo, L"简体中文");
    ComboBox_AddString(g_languageCombo, L"繁體中文");
    ComboBox_SetCurSel(g_languageCombo, 0);
    g_playerCountLabel = makeControl(0, L"STATIC", L"游玩人数", SS_LEFT, 0);
    g_playerCountCombo = makeControl(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, IDC_PLAYER_COUNT);

    g_inputGroup = makeControl(0, L"BUTTON", L"1. 添加侦察目标",
                               BS_GROUPBOX | WS_CLIPSIBLINGS, 0);
    g_targetLabel = makeControl(0, L"STATIC", L"目标与地点", SS_LEFT, 0);
    g_valueLabel = makeControl(0, L"STATIC", L"实际价值（$）", SS_LEFT, 0);
    g_quantityLabel = makeControl(0, L"STATIC", L"数量", SS_LEFT, 0);
    g_targetCombo = makeControl(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
                                CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
                                IDC_TARGET_COMBO);
    g_valueEdit = makeControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP,
                              IDC_VALUE_EDIT);
    g_quantityEdit = makeControl(WS_EX_CLIENTEDGE, L"EDIT", L"1",
                                 ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, IDC_QUANTITY_EDIT);
    g_designatedCheck = makeControl(0, L"BUTTON", L"买家指定目标",
                                    BS_AUTOCHECKBOX | WS_TABSTOP, IDC_DESIGNATED);
    g_addButton = makeControl(0, L"BUTTON", L"加入清单", BS_PUSHBUTTON | WS_TABSTOP,
                              IDC_ADD_BUTTON);
    g_difficultyLabel = makeControl(0, L"STATIC", L"任务难度", SS_LEFT, 0);
    g_difficultyCombo = makeControl(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, IDC_DIFFICULTY);
    g_referenceLabel = makeControl(0, L"STATIC", L"请选择目标。实际价值以侦察结果为准。",
                                   SS_LEFT, IDC_REFERENCE_LABEL);

    g_listGroup = makeControl(0, L"BUTTON", L"2. 本次侦察清单",
                              BS_GROUPBOX | WS_CLIPSIBLINGS, 0);
    g_lootList = makeControl(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | WS_TABSTOP,
                             IDC_LOOT_LIST);
    ListView_SetExtendedListViewStyle(g_lootList,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
    SetWindowTheme(g_lootList, L"Explorer", nullptr);
    const wchar_t* columns[] = {
        L"目标", L"地点", L"单件价值", L"单件占用", L"数量", L"买家指定", L"状态"};
    for (int i = 0; i < 7; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.pszText = const_cast<wchar_t*>(columns[i]);
        column.cx = 120;
        column.fmt = (i >= 2 && i <= 4) ? LVCFMT_RIGHT : LVCFMT_LEFT;
        ListView_InsertColumn(g_lootList, i, &column);
    }
    g_statusLabel = makeControl(0, L"STATIC", L"已录入 0 类目标，共 0 件",
                                SS_LEFT | SS_CENTERIMAGE, IDC_STATUS_LABEL);
    g_removeButton = makeControl(0, L"BUTTON", L"移除选中", BS_PUSHBUTTON | WS_TABSTOP,
                                 IDC_REMOVE_BUTTON);
    g_clearButton = makeControl(0, L"BUTTON", L"清空清单", BS_PUSHBUTTON | WS_TABSTOP,
                                IDC_CLEAR_BUTTON);
    g_calculateButton = makeControl(0, L"BUTTON", L"计算最优方案",
                                    BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_CALCULATE_BUTTON);

    g_resultGroup = makeControl(0, L"BUTTON", L"3. 最优拿取方案",
                                BS_GROUPBOX | WS_CLIPSIBLINGS, 0);
    g_resultEdit = makeControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, IDC_RESULT_EDIT);

    setFont(g_titleLabel, g_titleFont);
    const HWND controls[] = {
        g_subtitleLabel, g_languageLabel, g_languageCombo,
        g_playerCountLabel, g_playerCountCombo, g_inputGroup,
        g_targetLabel, g_valueLabel,
        g_quantityLabel, g_targetCombo, g_valueEdit, g_quantityEdit, g_designatedCheck,
        g_addButton, g_difficultyLabel, g_difficultyCombo,
        g_referenceLabel, g_listGroup, g_lootList, g_statusLabel, g_removeButton,
        g_clearButton, g_calculateButton, g_resultGroup, g_resultEdit};
    for (const HWND control : controls) {
        setFont(control);
    }
    setFont(ListView_GetHeader(g_lootList));

    // A group box is a visual sibling, not the parent of the controls inside it.
    // Keep all three frames behind the interactive controls so sibling clipping
    // cannot hide them during a resize repaint.
    SetWindowPos(g_inputGroup, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(g_listGroup, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(g_resultGroup, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    refreshLanguageUi();
}

void refreshLanguageUi() {
    const auto valueBeforeRefresh = getWindowText(g_valueEdit);
    setLocalizedText(g_mainWindow, L"科兹中心目标价值计算器");
    setLocalizedText(g_titleLabel, L"科兹中心目标价值计算器");
    setLocalizedText(g_subtitleLabel,
        L"录入侦察到的实际价值，自动求出各玩家独立 100% 背包的最高价值组合。");
    setLocalizedText(g_languageLabel, L"界面语言");
    setLocalizedText(g_playerCountLabel, L"游玩人数");
    setLocalizedText(g_inputGroup, L"1. 添加侦察目标");
    setLocalizedText(g_targetLabel, L"目标与地点");
    setLocalizedText(g_valueLabel, L"实际价值（$）");
    setLocalizedText(g_quantityLabel, L"数量");
    setLocalizedText(g_designatedCheck, L"买家指定目标");
    setLocalizedText(g_addButton, L"加入清单");
    setLocalizedText(g_difficultyLabel, L"任务难度");
    setLocalizedText(g_listGroup, L"2. 本次侦察清单");
    setLocalizedText(g_removeButton, L"移除选中");
    setLocalizedText(g_clearButton, L"清空清单");
    setLocalizedText(g_calculateButton, L"计算最优方案");
    setLocalizedText(g_resultGroup, L"3. 最优拿取方案");

    rebuildPlayerCountOptions();
    rebuildDifficultyOptions();
    rebuildTargetOptions();
    updateListColumnHeaders();
    updateReferenceLabel();
    if (g_uiInitialized) {
        SetWindowTextW(g_valueEdit, valueBeforeRefresh.c_str());
    }
    refreshListView();
    calculateAndDisplay();
    g_uiInitialized = true;
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_mainWindow = window;
        createUi();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 1080;
        info->ptMinTrackSize.y = 740;
        return 0;
    }
    case WM_SIZE:
        layoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == IDC_LANGUAGE && notification == CBN_SELCHANGE) {
            g_traditionalChinese = ComboBox_GetCurSel(g_languageCombo) == 1;
            refreshLanguageUi();
            return 0;
        }
        if (id == IDC_TARGET_COMBO && notification == CBN_SELCHANGE) {
            updateReferenceLabel();
            return 0;
        }
        if (id == IDC_PLAYER_COUNT && notification == CBN_SELCHANGE) {
            updateReferenceLabel();
            refreshListView();
            calculateAndDisplay();
            return 0;
        }
        if (id == IDC_DIFFICULTY && notification == CBN_SELCHANGE) {
            calculateAndDisplay();
            return 0;
        }
        if (id == IDC_ADD_BUTTON && notification == BN_CLICKED) {
            addCurrentTarget();
            return 0;
        }
        if (id == IDC_REMOVE_BUTTON && notification == BN_CLICKED) {
            removeSelectedTargets();
            return 0;
        }
        if (id == IDC_CLEAR_BUTTON && notification == BN_CLICKED) {
            g_inputs.clear();
            refreshListView();
            calculateAndDisplay();
            return 0;
        }
        if (id == IDC_CALCULATE_BUTTON && notification == BN_CLICKED) {
            calculateAndDisplay();
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, control == g_subtitleLabel || control == g_referenceLabel ||
                             control == g_statusLabel ? kMutedColor : kTextColor);
        return reinterpret_cast<LRESULT>(g_backgroundBrush);
    }
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, g_backgroundBrush);
        return 1;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_instance = instance;
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&commonControls);

    g_backgroundBrush = CreateSolidBrush(RGB(247, 250, 249));
    g_font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_titleFont = CreateFontW(-32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    buildCatalog();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = g_backgroundBrush;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&windowClass)) {
        return 1;
    }

    const HWND window = CreateWindowExW(0, kWindowClass, L"科兹中心目标价值计算器",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 880,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    DeleteObject(g_titleFont);
    DeleteObject(g_font);
    DeleteObject(g_backgroundBrush);
    return static_cast<int>(message.wParam);
}
