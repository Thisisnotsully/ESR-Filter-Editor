#include "add_rule_view.hpp"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <set>

#include "groups.h"
#include "text_fold.hpp"
#include "ui_text.hpp"

namespace esrfe::ui {

using namespace overlay::text_flags;
using document::Value;
using overlay::Font;
using overlay::UiFormat;
using overlay::UiText;

namespace {

constexpr const char* kStepNames[] = {"1 What happens", "2 Item Type", "3 Items", "4 Tier", "5 Stats", "6 Strictness", "7 Style", "8 Check"};
constexpr const char* kStepHeadings[] = {
    "What should happen to these items?", "What type of item?",          "Which items?",          "Which tiers?",
    "Which stats?",                       "On which strictness levels?", "What style should it have?", "Your rule",
};
constexpr const char* kStepHints[] = {
    "Hide: on the strictness levels you pick. Show: never hidden on the levels you pick, with your style. Always Show: "
    "never hidden on any level.",
    "Choose the item type first: the next steps only offer what fits it.",
    "Choose a list on the left, then tick items on the right. Or search by name.",
    "Leave every box as it is for all of them.",
    "Tick the stats the item must have, and how much. Rules with stats only work on identified items.",
    "Leave every box ticked for every strictness level.",
    "Everything is optional: leave the game's look by choosing nothing. The pictures show the label on the ground and the "
    "minimap icon.",
    "Check it, then add it. Your rules come first in the filter: what they hide is hidden, what they show is never hidden.",
};
const char* const kTierLabels[] = {"Normal", "Exceptional", "Elite"};
const gamedata::Tier kTiers[] = {gamedata::Tier::Normal, gamedata::Tier::Exceptional, gamedata::Tier::Elite};
const char* const kMythLabels[] = {"With Mythicals", "Without Mythicals", "Only Mythicals"};

auto ToOverlay(const Rgb& c) -> overlay::Rgb {
    return overlay::Rgb{static_cast<std::uint8_t>(c.r), static_cast<std::uint8_t>(c.g), static_cast<std::uint8_t>(c.b)};
}

auto TierWord(gamedata::Tier t) -> const char* {
    switch (t) {
        case gamedata::Tier::Normal: return "Normal";
        case gamedata::Tier::Exceptional: return "Exceptional";
        case gamedata::Tier::Elite: return "Elite";
        default: return "";
    }
}

// The colour the game gives a name of this kind (the label preview).
auto KindColor(ItemKind kind) -> overlay::Rgb {
    switch (kind) {
        case ItemKind::Magic: return {105, 105, 255};
        case ItemKind::Rare: return {255, 255, 100};
        case ItemKind::Unique: return {199, 179, 119};
        case ItemKind::Set: return {0, 255, 0};
        case ItemKind::Other: return {255, 168, 0};
        default: return {255, 255, 255};
    }
}

void PaintTick(overlay::Canvas& canvas, const overlay::Box& row, bool on) {
    const overlay::Theme& theme = canvas.GetTheme();
    const int size = canvas.S(14);
    const overlay::Box tick{row.left + canvas.S(8), row.CentreY() - size / 2, row.left + canvas.S(8) + size, row.CentreY() - size / 2 + size};
    canvas.Fill(tick, theme.field);
    canvas.Frame(tick, theme.border);
    if (on) canvas.Fill(tick.Inflated(-canvas.S(3)), theme.gold);
}

}  // namespace

AddRuleView::AddRuleView(std::shared_ptr<Loaded> loaded, RuleDraft draft, std::string editingId)
    : loaded_(std::move(loaded)), game_(*loaded_->game), draft_(std::move(draft)), editing_(std::move(editingId)) {
    for (int i = 0; i < kSteps; ++i) {
        overlay::Chip& chip = stepChips_.emplace_back();
        chip.label = kStepNames[i];
        chip.onClick = [this, i](Ui& ui) {
            GoTo(static_cast<Step>(i));
            ui.Repaint();
        };
    }
    heading_.font = Font::Bold;
    heading_.useThemeColor = false;
    hint_.font = Font::Small;
    hint_.flags = kLeft;
    hint_.useThemeColor = false;

    // 1 What happens.
    hide_.label = "Hide";
    show_.label = "Show";
    always_.label = "Always Show (all strictness levels)";
    const auto act = [this](DraftAction a) {
        return [this, a](Ui& ui) {
            draft_.action = a;
            Changed();
            ui.Repaint();
        };
    };
    hide_.onClick = act(DraftAction::Hide);
    show_.onClick = act(DraftAction::Show);
    always_.onClick = act(DraftAction::AlwaysShow);
    for (overlay::Chip* c : {&hide_, &show_, &always_}) c->font = Font::Body;

    // 2 Kind.
    for (const auto& [kind, name] : KindChoices()) {
        overlay::Chip& chip = kinds_.emplace_back();
        chip.label = name;
        chip.font = Font::Body;
        const ItemKind k = kind;
        chip.onClick = [this, k](Ui& ui) {
            if (draft_.kind != k) {
                draft_.kind = k;
                itemSearch_.SetValue("");
                Changed();
                GoTo(Step::Items);  // straight on to the items of that kind
            }
            ui.Repaint();
        };
    }

    // 3 Items.
    itemSearch_.search = true;
    itemSearch_.placeholder = "Search by name…";
    itemSearch_.onChange = [this](const std::string&, Ui& ui) {
        RebuildEntries();
        entryList_.SetScroll(0);
        ui.Repaint();
    };
    categoryList_.rowGap = 0;
    categoryList_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(categories_.size())) return;
        const Category& c = categories_[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const Box text{row.left + canvas.S(c.heading ? 8 : 18), row.top, row.right - canvas.S(6), row.bottom};
        canvas.PushClip(text);
        canvas.Text(c.name, text, c.heading ? theme.headerGold : theme.text, c.heading ? Font::Bold : Font::Body, kVCenter | kSingleLine);
        canvas.PopClip();
    };
    categoryList_.onSelect = [this](int, Ui& ui) {
        itemSearch_.SetValue("");
        RebuildEntries();
        entryList_.SetScroll(0);
        ui.Repaint();
    };
    entryList_.rowGap = 0;
    entryList_.emptyText = "Choose a list on the left";
    entryList_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(entries_.size())) return;
        const Entry& e = entries_[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const bool on = Picked(e);
        PaintTick(canvas, row, on);
        const int tagWidth = e.tag.empty() ? 0 : std::min(row.Width() / 2, canvas.TextWidth(e.tag, Font::Small) + canvas.S(12));
        const Box text{row.left + canvas.S(30), row.top, row.right - tagWidth - canvas.S(6), row.bottom};
        canvas.PushClip(text);
        canvas.Text(e.name, text, on ? theme.text : theme.dim, Font::Body, kVCenter | kSingleLine);
        canvas.PopClip();
        if (!e.tag.empty()) {
            const Box tag{row.right - tagWidth, row.top, row.right - canvas.S(6), row.bottom};
            canvas.PushClip(tag);
            canvas.Text(e.tag, tag, theme.dim, Font::Small, kRight | kVCenter | kSingleLine);
            canvas.PopClip();
        }
    };
    entryList_.onClick = [this](int index, Ui& ui) { ToggleEntry(index, ui); };
    entryList_.onActivate = [this](int index, Ui& ui) { ToggleEntry(index, ui); };
    all_.label = "Tick all";
    all_.onClick = [this](Ui& ui) {
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
            if (!Picked(entries_[static_cast<std::size_t>(i)])) ToggleEntry(i, ui);
    };
    none_.label = "Untick all";
    none_.onClick = [this](Ui& ui) {
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
            if (Picked(entries_[static_cast<std::size_t>(i)])) ToggleEntry(i, ui);
    };
    chosen_.translate = false;
    chosen_.font = Font::Small;
    chosen_.flags = kLeft;
    chosen_.useThemeColor = false;

    // 4 Tier.
    for (std::size_t i = 0; i < 3; ++i) {
        overlay::Checkbox& box = tiers_.emplace_back();
        box.label = kTierLabels[i];
        box.onChange = [this, i](bool on, Ui& ui) {
            const DraftOffer offer = Offer(draft_, game_);
            if (draft_.tiers.empty()) draft_.tiers = offer.tierChoices;  // every tier was meant: now all but this one
            if (on) draft_.tiers.insert(kTiers[i]);
            else draft_.tiers.erase(kTiers[i]);
            if (draft_.tiers == offer.tierChoices) draft_.tiers.clear();
            Changed();
            ui.Repaint();
        };
    }
    qualityLabel_.text = "Quality";
    qualityLabel_.font = Font::Small;
    white_.label = "White (normal)";
    superior_.label = "Superior";
    const auto quality = [this](int q) {
        return [this, q](bool on, Ui& ui) {
            std::set<int> now = draft_.baseQualities.empty() ? std::set<int>{2, 3} : draft_.baseQualities;
            if (on) now.insert(q);
            else now.erase(q);
            if (now.empty()) {
                status_.text = UiText("Keep White, Superior or both.");
                SyncControls();
                ui.Repaint();
                return;
            }
            draft_.baseQualities = now.size() == 2 ? std::set<int>{} : now;
            Changed();
            ui.Repaint();
        };
    };
    white_.onChange = quality(2);
    superior_.onChange = quality(3);
    mythLabel_.text = "Mythical bases";
    mythLabel_.font = Font::Small;
    for (std::size_t i = 0; i < 3; ++i) {
        overlay::Chip& chip = myth_.emplace_back();
        chip.label = kMythLabels[i];
        chip.onClick = [this, i](Ui& ui) {
            draft_.mythical = static_cast<RuleDraft::Mythicals>(i);
            Changed();
            ui.Repaint();
        };
    }

    // 5 Bonuses.
    statSearch_.search = true;
    statSearch_.placeholder = "Search the stats: life, skills, resist…";
    statSearch_.onChange = [this](const std::string&, Ui& ui) {
        RebuildStats();
        statList_.SetScroll(0);
        ui.Repaint();
    };
    statList_.rowGap = 0;
    statList_.emptyText = "No stats";
    statList_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(statRows_.size())) return;
        const std::size_t at = static_cast<std::size_t>(statRows_[static_cast<std::size_t>(index)]);
        if (at >= offer_.stats.size()) return;
        const StatOption& o = offer_.stats[at];
        const overlay::Theme& theme = canvas.GetTheme();
        const StatPick* pick = nullptr;
        for (const StatPick& p : draft_.stats)
            if (p.stat == o.stat && p.param == o.param) pick = &p;
        PaintTick(canvas, row, pick != nullptr);
        const std::string right = pick != nullptr ? UiFormat("at least {0}", {std::to_string(pick->atLeast)})
                                                  : UiFormat("{0} to {1}", {std::to_string(o.min), std::to_string(o.max)});
        const int rightWidth = canvas.TextWidth(right, Font::Small) + canvas.S(12);
        const Box text{row.left + canvas.S(30), row.top, row.right - rightWidth, row.bottom};
        canvas.PushClip(text);
        canvas.Text(o.label, text, pick != nullptr ? theme.text : theme.dim, Font::Body, kVCenter | kSingleLine);
        canvas.PopClip();
        canvas.Text(right, Box{row.right - rightWidth, row.top, row.right - canvas.S(6), row.bottom}, pick != nullptr ? theme.gold : theme.dim,
                    Font::Small, kRight | kVCenter | kSingleLine);
    };
    statList_.onClick = [this](int index, Ui& ui) { ToggleStat(index, ui); };
    statList_.onActivate = [this](int index, Ui& ui) { ToggleStat(index, ui); };
    statList_.onSelect = [this](int, Ui& ui) {
        SyncControls();
        ui.Repaint();
    };
    atLeastLabel_.text = "At least";
    atLeastLabel_.font = Font::Small;
    atLeast_.onChange = [this](int value, Ui& ui) {
        const int row = statList_.Selected();
        if (row < 0 || row >= static_cast<int>(statRows_.size())) return;
        const std::size_t at = static_cast<std::size_t>(statRows_[static_cast<std::size_t>(row)]);
        if (at >= offer_.stats.size()) return;
        const StatOption& o = offer_.stats[at];
        for (StatPick& p : draft_.stats)
            if (p.stat == o.stat && p.param == o.param) p.atLeast = value;
        Changed();
        ui.Repaint();
    };

    // 6 Levels.
    for (const document::FilterLevel& l : loaded_->doc.GetSettings().filterLevels) {
        levelIds_.push_back(l.id);
        overlay::Checkbox& box = levels_.emplace_back();
        box.label = PickLanguage(l.name, loaded_->language);
        box.translate = false;
        const std::size_t index = levels_.size() - 1;
        box.onChange = [this, index](bool on, Ui& ui) {
            std::vector<std::int64_t> chosen;
            for (std::size_t i = 0; i < levels_.size(); ++i)
                if (i == index ? on : levels_[i].on) chosen.push_back(levelIds_[i]);
            if (chosen.empty()) {
                levels_[index].on = true;
                status_.text = UiText("A rule needs at least one level.");
                ui.Repaint();
                return;
            }
            draft_.levels = chosen.size() == levelIds_.size() ? std::vector<std::int64_t>{} : chosen;
            Changed();
            ui.Repaint();
        };
    }

    // 7 Look.
    borderLabel_.text = "Border";
    backLabel_.text = "Background";
    nameLabel_.text = "Name colour";
    soundLabel_.text = "Drop sound";
    iconLabel_.text = "Minimap icon";
    iconSizeLabel_.text = "Size";
    for (overlay::Label* l : {&borderLabel_, &backLabel_, &nameLabel_, &soundLabel_, &iconLabel_, &iconSizeLabel_}) l->font = Font::Small;
    iconSizeValue_.translate = false;
    iconSizeValue_.font = Font::Small;
    iconSizeValue_.flags = kLeft | kVCenter | kSingleLine;
    borderNone_.label = "None";
    backNone_.label = "None";
    nameGame_.label = "The game's";
    borderNone_.onClick = [this](Ui& ui) { draft_.look.border.reset(); Changed(); ui.Repaint(); };
    backNone_.onClick = [this](Ui& ui) { draft_.look.background.reset(); Changed(); ui.Repaint(); };
    nameGame_.onClick = [this](Ui& ui) { draft_.look.nameColor = -1; Changed(); ui.Repaint(); };
    for (std::size_t i = 0; i < Swatches().size(); ++i) {
        const NamedColor& c = Swatches()[i];
        for (auto* row : {&borderSw_, &backSw_, &beamSw_}) {
            overlay::Swatch& s = row->emplace_back();
            s.label = c.english;
            s.color = ToOverlay(c.rgb);
        }
        const Rgb rgb = c.rgb;
        borderSw_.back().onClick = [this, rgb](Ui& ui) { draft_.look.border = rgb; Changed(); ui.Repaint(); };
        backSw_.back().onClick = [this, rgb](Ui& ui) { draft_.look.background = rgb; Changed(); ui.Repaint(); };
        beamSw_.back().onClick = [this, rgb](Ui& ui) {
            draft_.look.beam = true;
            draft_.look.beamColor = rgb;
            Changed();
            ui.Repaint();
        };
    }
    for (std::size_t i = 0; i < NameColors().size(); ++i) {
        const NameColor& c = NameColors()[i];
        for (auto* row : {&nameSw_, &chatSw_}) {
            overlay::Swatch& s = row->emplace_back();
            s.label = c.english;
            s.color = ToOverlay(c.rgb);
        }
        const int index = static_cast<int>(i);
        nameSw_.back().onClick = [this, index](Ui& ui) { draft_.look.nameColor = index; Changed(); ui.Repaint(); };
        chatSw_.back().onClick = [this, index](Ui& ui) { draft_.look.chatColor = index; Changed(); ui.Repaint(); };
    }
    beam_.label = "Light beam where it drops";
    beam_.onChange = [this](bool on, Ui& ui) { draft_.look.beam = on; Changed(); ui.Repaint(); };
    if (loaded_->session != nullptr) soundFiles_ = loaded_->session->SoundFiles();
    sound_.translate = false;
    sound_.options = {UiText("No sound")};
    for (const std::string& f : soundFiles_) sound_.options.push_back(f);
    sound_.onChange = [this](int index, Ui& ui) {
        draft_.look.sound = index <= 0 ? std::string{} : soundFiles_[static_cast<std::size_t>(index - 1)];
        Changed();
        if (!draft_.look.sound.empty()) PlayPreview(ui);
        ui.Repaint();
    };
    play_.label = "Play";
    play_.onClick = [this](Ui& ui) { PlayPreview(ui); };
    chat_.label = "Chat line when it drops";
    chat_.onChange = [this](bool on, Ui& ui) { draft_.look.chat = on; Changed(); ui.Repaint(); };
    chatText_.placeholder = "{name} is the item's name";
    chatText_.onChange = [this](const std::string& text, Ui& ui) {
        draft_.look.chatText = text.empty() ? std::string("{name}") : text;
        draft_.look.chat = true;
        Changed();
        ui.Repaint();
    };
    if (loaded_->session != nullptr) iconNames_ = loaded_->session->IconNames();
    icon_.translate = false;
    icon_.options = {UiText("No icon")};
    for (const std::string& n : iconNames_) icon_.options.push_back(n);
    icon_.selected = 0;
    for (std::size_t i = 0; i < iconNames_.size(); ++i)
        if (iconNames_[i] == draft_.look.icon) icon_.selected = static_cast<int>(i) + 1;
    icon_.onChange = [this](int index, Ui& ui) {
        draft_.look.icon = index <= 0 ? std::string{} : iconNames_[static_cast<std::size_t>(index - 1)];
        Changed();
        ui.Repaint();
    };
    iconSize_.minimum = 8;
    iconSize_.maximum = 64;
    iconSize_.step = 1;
    iconSize_.SetValue(draft_.look.iconSize);
    iconSize_.onChange = [this](int v, Ui& ui) {
        draft_.look.iconSize = v;
        iconSizeValue_.text = UiFormat("{0} px", {std::to_string(v)});
        ui.Repaint();
    };
    iconSize_.onCommit = [this](int, Ui& ui) {
        Changed();
        ui.Repaint();
    };

    // 8 Check.
    checkLines_.drawRowBackground = false;
    checkLines_.rowGap = 0;
    checkLines_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(checkText_.size())) return;
        const std::string& line = checkText_[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const bool heading = !line.empty() && line[0] == '#';
        const Box text{row.left + canvas.S(heading ? 8 : 24), row.top, row.right - canvas.S(8), row.bottom};
        canvas.PushClip(text);
        canvas.Text(heading ? line.substr(1) : line, text, heading ? theme.headerGold : theme.text, heading ? Font::Bold : Font::Body,
                    kVCenter | kSingleLine);
        canvas.PopClip();
    };

    summary_.translate = false;
    summary_.font = Font::Small;
    summary_.useThemeColor = false;
    status_.translate = false;
    status_.font = Font::Small;
    status_.useThemeColor = false;
    back_.label = "< Back";
    back_.onClick = [this](Ui& ui) {
        int i = static_cast<int>(step_) - 1;
        while (i >= 0 && !StepUsed(static_cast<Step>(i))) --i;
        if (i < 0) {
            ui.PopView();
            return;
        }
        GoTo(static_cast<Step>(i));
        ui.Repaint();
    };
    next_.label = "Next >";
    next_.onClick = [this](Ui& ui) {
        int i = static_cast<int>(step_) + 1;
        while (i < kSteps && !StepUsed(static_cast<Step>(i))) ++i;
        if (i < kSteps) GoTo(static_cast<Step>(i));
        ui.Repaint();
    };
    finish_.label = editing_.empty() ? "Add rule" : "Save rule";
    finish_.style = overlay::Button::Style::Primary;
    finish_.onClick = [this](Ui& ui) { Save(ui); };

    chatText_.SetValue(draft_.look.chatText == "{name}" ? std::string{} : draft_.look.chatText);
    for (std::size_t i = 0; i < soundFiles_.size(); ++i)
        if (soundFiles_[i] == draft_.look.sound) sound_.selected = static_cast<int>(i) + 1;
    if (sound_.selected < 0) sound_.selected = 0;
    Changed();
    // The hover key's item: its list chosen in the items step.
    if (PickCount(draft_) > 0) {
        for (int c = 0; c < static_cast<int>(categories_.size()); ++c) {
            bool has = false;
            for (const Entry& e : categories_[static_cast<std::size_t>(c)].entries) has = has || Picked(e);
            if (has) {
                categoryList_.Select(c, nullptr);
                break;
            }
        }
        RebuildEntries();
    }
    GoTo(Step::Action);
}

auto AddRuleView::Title() const -> std::string { return editing_.empty() ? UiText("New rule") : UiFormat("Change your rule {0}", {editing_}); }

auto AddRuleView::ActionChip(DraftAction action) -> overlay::Chip& {
    return action == DraftAction::Hide ? hide_ : action == DraftAction::Show ? show_ : always_;
}

auto AddRuleView::StepUsed(Step step) const -> bool {
    const bool byBase = draft_.kind == ItemKind::Base || draft_.kind == ItemKind::Magic || draft_.kind == ItemKind::Rare;
    switch (step) {
        case Step::Items: return draft_.kind != ItemKind::None;
        case Step::Tier: return byBase && (offer_.tiers || offer_.qualities);
        case Step::Bonuses: return draft_.kind == ItemKind::Base || draft_.kind == ItemKind::Magic || draft_.kind == ItemKind::Rare;
        case Step::Levels: return draft_.action != DraftAction::AlwaysShow && !levelIds_.empty();
        case Step::Look: return draft_.action == DraftAction::Show || draft_.action == DraftAction::AlwaysShow;
        default: return true;
    }
}

void AddRuleView::GoTo(Step step) {
    step_ = step;
    ShowStep();
    SyncControls();
}

auto AddRuleView::Picked(const Entry& e) const -> bool {
    switch (e.type) {
        case Entry::Type::Base: return std::find(draft_.bases.begin(), draft_.bases.end(), e.code) != draft_.bases.end();
        case Entry::Type::Coupon: return std::find(draft_.coupons.begin(), draft_.coupons.end(), e.code) != draft_.coupons.end();
        case Entry::Type::Unique: return std::find(draft_.uniques.begin(), draft_.uniques.end(), e.row) != draft_.uniques.end();
        case Entry::Type::SetItem: return std::find(draft_.setItems.begin(), draft_.setItems.end(), e.row) != draft_.setItems.end();
    }
    return false;
}

void AddRuleView::RebuildCategories() {
    categories_.clear();
    categoriesFor_ = draft_.kind;
    const auto baseName = [this](const std::string& code) { return EntryName("itemCodes", Value::String(code), loaded_->context); };
    switch (draft_.kind) {
        case ItemKind::Base:
        case ItemKind::Magic:
        case ItemKind::Rare:
        case ItemKind::Other: {
            for (const gamedata::Group& group : game_.groups.All()) {
                std::vector<Category> subs;
                for (const gamedata::Subgroup& sub : group.subgroups) {
                    Category c;
                    c.name = gamedata::Groups::Name(sub, game_.strings, loaded_->language);
                    for (const std::string& code : sub.bases) {
                        const gamedata::Base* b = game_.bases.Find(code);
                        if (b == nullptr || !KindAllows(draft_.kind, game_, *b)) continue;
                        Entry e;
                        e.type = Entry::Type::Base;
                        e.code = code;
                        e.name = baseName(code);
                        e.tag = b->mythical ? UiText("Mythical") : UiText(TierWord(b->tier));
                        c.entries.push_back(std::move(e));
                    }
                    if (c.entries.empty()) continue;
                    // Runes: ESR's by the colour of their names, the classic ones (El to Zod) together.
                    if (std::any_of(c.entries.begin(), c.entries.end(), [this](const Entry& e) { return RuneColor(game_, e.code) != 0; })) {
                        std::vector<char> order;
                        std::map<char, Category> byColor;
                        for (Entry& e : c.entries) {
                            const char color = RuneColor(game_, e.code);
                            if (byColor.count(color) == 0) order.push_back(color);
                            byColor[color].entries.push_back(std::move(e));
                        }
                        for (char color : order) {
                            Category part = std::move(byColor[color]);
                            part.name = color == 0 ? UiFormat("{0}: classic and others", {c.name})
                                                   : UiFormat("{0}: {1}", {c.name, UiText(RuneColorName(color))});
                            subs.push_back(std::move(part));
                        }
                        continue;
                    }
                    subs.push_back(std::move(c));
                }
                if (subs.empty()) continue;
                const std::string name = gamedata::Groups::Name(group, game_.strings, loaded_->language);
                if (subs.size() == 1) {
                    subs[0].name = name;
                    categories_.push_back(std::move(subs[0]));
                    continue;
                }
                categories_.push_back(Category{name, true, {}});
                for (Category& c : subs) categories_.push_back(std::move(c));
            }
            break;
        }
        case ItemKind::Unique: {
            std::map<int, Category> byTier;  // 1-3 normal..elite, 0 no tier
            const char* names[] = {"Rings, amulets, charms, jewels and others", "Normal tier", "Exceptional tier", "Elite tier"};
            for (const gamedata::Unique& u : game_.uniques.All()) {
                if (u.disabled) continue;
                Entry e;
                e.type = Entry::Type::Unique;
                e.row = u.row;
                e.name = gamedata::Strings::Plain(game_.uniques.Name(u, game_.strings, loaded_->language));
                if (e.name.empty()) continue;
                e.tag = baseName(u.code);
                byTier[static_cast<int>(TierOfUnique(game_, u.code))].entries.push_back(std::move(e));
            }
            for (int t : {1, 2, 3, 0}) {
                Category c = std::move(byTier[t]);
                c.name = UiText(names[t]);
                std::sort(c.entries.begin(), c.entries.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
                if (!c.entries.empty()) categories_.push_back(std::move(c));
            }
            std::map<int, Category> coupons;
            for (const Coupon& coupon : Coupons(game_)) {
                Entry e;
                e.type = Entry::Type::Coupon;
                e.code = coupon.code;
                e.name = baseName(coupon.code);
                coupons[static_cast<int>(coupon.tier)].entries.push_back(std::move(e));
            }
            const char* couponNames[] = {"Ancient Coupons: other", "Ancient Coupons: Normal", "Ancient Coupons: Exceptional", "Ancient Coupons: Elite"};
            for (int t : {1, 2, 3, 0}) {
                Category c = std::move(coupons[t]);
                c.name = UiText(couponNames[t]);
                std::sort(c.entries.begin(), c.entries.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
                if (!c.entries.empty()) categories_.push_back(std::move(c));
            }
            break;
        }
        case ItemKind::Set: {
            // By tier first, as uniques, then set by set.
            std::map<int, Category> byTier;  // 1-3 normal..elite, 0 no tier
            const char* tierNames[] = {"Rings, amulets, charms, jewels and others", "Normal tier", "Exceptional tier", "Elite tier"};
            for (const gamedata::SetItem& item : game_.sets.Items()) {
                if (item.disabled) continue;
                Entry e;
                e.type = Entry::Type::SetItem;
                e.row = item.row;
                e.name = gamedata::Strings::Plain(game_.sets.Name(item, game_.strings, loaded_->language));
                if (e.name.empty()) continue;
                e.tag = baseName(item.code);
                byTier[static_cast<int>(TierOfUnique(game_, item.code))].entries.push_back(std::move(e));
            }
            for (int t : {1, 2, 3, 0}) {
                Category c = std::move(byTier[t]);
                c.name = UiText(tierNames[t]);
                std::sort(c.entries.begin(), c.entries.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
                if (!c.entries.empty()) categories_.push_back(std::move(c));
            }
            std::vector<Category> sets;
            for (const gamedata::Set& set : game_.sets.All()) {
                Category c;
                c.name = gamedata::Strings::Plain(game_.sets.SetName(set, game_.strings, loaded_->language));
                for (int row : set.items) {
                    const gamedata::SetItem* item = game_.sets.Item(row);
                    if (item == nullptr || item->disabled) continue;
                    Entry e;
                    e.type = Entry::Type::SetItem;
                    e.row = row;
                    e.name = gamedata::Strings::Plain(game_.sets.Name(*item, game_.strings, loaded_->language));
                    e.tag = baseName(item->code);
                    c.entries.push_back(std::move(e));
                }
                if (!c.entries.empty()) sets.push_back(std::move(c));
            }
            std::sort(sets.begin(), sets.end(), [](const Category& a, const Category& b) { return a.name < b.name; });
            categories_.push_back(Category{UiText("By set"), true, {}});
            for (Category& c : sets) categories_.push_back(std::move(c));
            break;
        }
        case ItemKind::None: break;
    }
    categoryList_.SetCount(static_cast<int>(categories_.size()));
    categoryList_.Select(categories_.empty() ? -1 : (categories_[0].heading && categories_.size() > 1 ? 1 : 0), nullptr);
    categoryList_.SetScroll(0);
    RebuildEntries();
}

void AddRuleView::RebuildEntries() {
    entries_.clear();
    const std::string query = itemSearch_.Value();
    if (!query.empty()) {
        const overlay::text::ParsedQuery parsed = overlay::text::ParseQuery(query);
        std::set<std::string> seen;  // an item can be in two lists (a set item: its tier and its set)
        for (const Category& c : categories_)
            for (const Entry& e : c.entries) {
                const std::string key = std::to_string(static_cast<int>(e.type)) + ":" + e.code + ":" + std::to_string(e.row);
                if (seen.count(key) != 0) continue;
                if (overlay::text::Matches(parsed, overlay::text::SearchTextUtf8(e.name + "\n" + e.tag + "\n" + e.code))) {
                    entries_.push_back(e);
                    seen.insert(key);
                }
            }
    } else {
        const int row = categoryList_.Selected();
        if (row >= 0 && row < static_cast<int>(categories_.size())) {
            const Category& c = categories_[static_cast<std::size_t>(row)];
            if (c.heading) {  // a group: all of its subgroups
                for (std::size_t i = static_cast<std::size_t>(row) + 1; i < categories_.size() && !categories_[i].heading; ++i)
                    entries_.insert(entries_.end(), categories_[i].entries.begin(), categories_[i].entries.end());
            } else {
                entries_ = c.entries;
            }
        }
    }
    entryList_.SetCount(static_cast<int>(entries_.size()));
}

void AddRuleView::RebuildStats() {
    statRows_.clear();
    const overlay::text::ParsedQuery parsed = overlay::text::ParseQuery(statSearch_.Value());
    for (std::size_t i = 0; i < offer_.stats.size(); ++i)
        if (parsed.empty() || overlay::text::Matches(parsed, overlay::text::SearchTextUtf8(offer_.stats[i].label))) statRows_.push_back(static_cast<int>(i));
    statList_.SetCount(static_cast<int>(statRows_.size()));
}

void AddRuleView::ToggleEntry(int row, Ui& ui) {
    if (row < 0 || row >= static_cast<int>(entries_.size())) return;
    const Entry& e = entries_[static_cast<std::size_t>(row)];
    const auto flip = [](auto& list, const auto& value) {
        const auto it = std::find(list.begin(), list.end(), value);
        if (it != list.end()) list.erase(it);
        else list.push_back(value);
    };
    draft_.every = false;  // ticking items replaces "every ..." or an item type
    draft_.types.clear();
    switch (e.type) {
        case Entry::Type::Base: flip(draft_.bases, e.code); break;
        case Entry::Type::Coupon: flip(draft_.coupons, e.code); break;
        case Entry::Type::Unique: flip(draft_.uniques, e.row); break;
        case Entry::Type::SetItem: flip(draft_.setItems, e.row); break;
    }
    Changed();
    ui.Repaint();
}

void AddRuleView::ToggleStat(int row, Ui& ui) {
    if (row < 0 || row >= static_cast<int>(statRows_.size())) return;
    const std::size_t at = static_cast<std::size_t>(statRows_[static_cast<std::size_t>(row)]);
    if (at >= offer_.stats.size()) return;
    const StatOption o = offer_.stats[at];
    const auto it = std::find_if(draft_.stats.begin(), draft_.stats.end(), [&](const StatPick& p) { return p.stat == o.stat && p.param == o.param; });
    if (it != draft_.stats.end()) draft_.stats.erase(it);
    else draft_.stats.push_back(StatPick{o.stat, o.param, o.min});
    statList_.Select(row, nullptr);
    Changed();
    ui.Repaint();
}

void AddRuleView::PlayPreview(Ui&) {
    if (draft_.look.sound.empty() || loaded_->session == nullptr || loaded_->source == nullptr) return;
    // As the game plays it: the loot filter's volume for sound files times the filter's volume.
    const int volume = loaded_->source->FileSoundVolume() * loaded_->session->SoundVolume() / 100;
    loaded_->source->PlaySound(loaded_->session->SoundPath(draft_.look.sound), volume);
}

auto AddRuleView::RulesNow() const -> std::vector<edit::OwnRule> {
    // The title: what the rule is about and when, as the list says it.
    const std::vector<edit::OwnRule> draftRules = ToRules(draft_, game_, "x");
    if (draftRules.empty()) return {};
    const RuleSummary s = Describe(draftRules.front().rule, loaded_->context);
    std::string title = overlay::StripColorCodes(s.what);
    for (const std::string& c : s.conditions) title += ", " + c;
    if (title.size() > 120) title = title.substr(0, 117) + "...";
    return ToRules(draft_, game_, title);
}

auto AddRuleView::PreviewName() const -> std::string {
    for (const Category& c : categories_)
        for (const Entry& e : c.entries)
            if (Picked(e)) return e.name;
    return UiText("Item name");
}

auto AddRuleView::Summary() const -> std::string {
    if (draft_.action == DraftAction::None) return UiText("Choose what should happen.");
    if (draft_.kind == ItemKind::None) return UiText("Choose the kind of item.");
    if (!Complete(draft_)) return UiText("Choose at least one item.");
    std::string line = summaryNow_.actionLabel + ": " + overlay::StripColorCodes(summaryNow_.what);
    for (const std::string& c : summaryNow_.conditions) line += "  ·  " + c;
    return line;
}

void AddRuleView::Changed() {
    Tidy(draft_, game_);
    if (categoriesFor_ != draft_.kind) RebuildCategories();
    offer_ = Offer(draft_, game_);
    RebuildStats();  // the rows point into offer_.stats: rebuilt whenever it is (a stale row crashed on a rarity change)
    checkText_.clear();
    if (Complete(draft_)) {
        const std::vector<edit::OwnRule> rules = RulesNow();
        summaryNow_ = Describe(rules.front().rule, loaded_->context);
        checkText_.push_back("#" + UiText("What it does"));
        switch (draft_.action) {
            case DraftAction::Hide: checkText_.push_back(UiText("Hides them on the ground")); break;
            case DraftAction::Show: checkText_.push_back(UiText("Shows them on the levels below: no rule can hide them there")); break;
            default: checkText_.push_back(UiText("Always shows them, on every level: no rule can hide them")); break;
        }
        for (const std::string& e : summaryNow_.effects) checkText_.push_back(overlay::StripColorCodes(e));
        std::vector<std::string> items;
        for (const edit::OwnRule& r : rules) {
            const RuleSummary part = Describe(r.rule, loaded_->context);
            items.insert(items.end(), part.whatAll.begin(), part.whatAll.end());
        }
        checkText_.push_back("#" + UiFormat("Items ({0})", {std::to_string(items.size())}));
        for (const std::string& n : items) checkText_.push_back(n);
        checkText_.push_back("#" + UiText("When"));
        if (summaryNow_.conditions.empty()) checkText_.push_back(UiText("Every time"));
        for (const std::string& c : summaryNow_.conditions) checkText_.push_back(c);
        if (rules.size() > 1) checkText_.push_back(UiText("(Saved as two rules: one for the uniques, one for the coupons.)"));
    } else {
        checkText_.push_back(Summary());
    }
    checkLines_.SetCount(static_cast<int>(checkText_.size()));
    summary_.text = Summary();
    status_.text.clear();
    ShowStep();
    SyncControls();
}

void AddRuleView::SyncControls() {
    for (int i = 0; i < kSteps; ++i) {
        overlay::Chip& chip = stepChips_[static_cast<std::size_t>(i)];
        chip.selected = static_cast<int>(step_) == i;
        chip.enabled = StepUsed(static_cast<Step>(i));
    }
    hide_.selected = draft_.action == DraftAction::Hide;
    show_.selected = draft_.action == DraftAction::Show;
    always_.selected = draft_.action == DraftAction::AlwaysShow;
    for (std::size_t i = 0; i < kinds_.size(); ++i) kinds_[i].selected = KindChoices()[i].first == draft_.kind;

    if (PickCount(draft_) == 0) {
        chosen_.text = UiText("Nothing chosen yet.");
    } else if (draft_.every || !draft_.types.empty()) {  // from the edit-item key: a whole kind or item type
        std::string what = overlay::StripColorCodes(summaryNow_.what);
        for (const std::string& c : summaryNow_.conditions) what += ", " + c;
        chosen_.text = UiFormat("Chosen: {0}. Tick items here to choose them instead.", {what});
    } else {
        std::vector<std::string> names;
        for (const Category& c : categories_)
            for (const Entry& e : c.entries)
                if (Picked(e) && std::find(names.begin(), names.end(), e.name) == names.end()) names.push_back(e.name);
        chosen_.text = UiFormat("Chosen ({0}): {1}", {std::to_string(PickCount(draft_)), JoinNames(names, 4)});
    }
    for (std::size_t i = 0; i < 3; ++i) {
        tiers_[i].enabled = offer_.tierChoices.count(kTiers[i]) != 0;
        tiers_[i].on = tiers_[i].enabled && (draft_.tiers.empty() || draft_.tiers.count(kTiers[i]) != 0);
    }
    white_.on = draft_.baseQualities.empty() || draft_.baseQualities.count(2) != 0;
    superior_.on = draft_.baseQualities.empty() || draft_.baseQualities.count(3) != 0;
    for (std::size_t i = 0; i < 3; ++i) myth_[i].selected = draft_.mythical == static_cast<RuleDraft::Mythicals>(i);

    // The selected bonus's "at least".
    const int row = statList_.Selected();
    const StatPick* pick = nullptr;
    const StatOption* option = nullptr;
    if (row >= 0 && row < static_cast<int>(statRows_.size()) && statRows_[static_cast<std::size_t>(row)] < static_cast<int>(offer_.stats.size())) {
        option = &offer_.stats[static_cast<std::size_t>(statRows_[static_cast<std::size_t>(row)])];
        for (const StatPick& p : draft_.stats)
            if (p.stat == option->stat && p.param == option->param) pick = &p;
    }
    atLeast_.enabled = pick != nullptr;
    if (option != nullptr) {
        atLeast_.minimum = option->min;
        atLeast_.maximum = option->max;
        atLeast_.step = std::max(1, (option->max - option->min) / 20);
    }
    atLeast_.SetValue(pick != nullptr ? pick->atLeast : (option != nullptr ? option->min : 0));
    for (std::size_t i = 0; i < levels_.size(); ++i)
        levels_[i].on = draft_.levels.empty() || std::find(draft_.levels.begin(), draft_.levels.end(), levelIds_[i]) != draft_.levels.end();

    const Look& look = draft_.look;
    borderNone_.selected = !look.border;
    backNone_.selected = !look.background;
    nameGame_.selected = look.nameColor < 0;
    for (std::size_t i = 0; i < Swatches().size(); ++i) {
        borderSw_[i].selected = look.border && *look.border == Swatches()[i].rgb;
        backSw_[i].selected = look.background && *look.background == Swatches()[i].rgb;
        beamSw_[i].selected = look.beam && look.beamColor == Swatches()[i].rgb;
    }
    for (std::size_t i = 0; i < NameColors().size(); ++i) {
        nameSw_[i].selected = look.nameColor == static_cast<int>(i);
        chatSw_[i].selected = look.chatColor == static_cast<int>(i);
    }
    beam_.on = look.beam;
    chat_.on = look.chat;
    play_.enabled = !look.sound.empty();
    icon_.selected = 0;
    for (std::size_t i = 0; i < iconNames_.size(); ++i)
        if (iconNames_[i] == look.icon) icon_.selected = static_cast<int>(i) + 1;
    iconSize_.SetValue(look.iconSize);
    iconSize_.enabled = !look.icon.empty();
    iconSizeValue_.text = UiFormat("{0} px", {std::to_string(look.iconSize)});
    finish_.enabled = Complete(draft_);
    next_.visible = static_cast<int>(step_) < kSteps - 1;
}

void AddRuleView::ShowStep() {
    const auto on = [this](Step s, std::initializer_list<Widget*> widgets) {
        for (Widget* w : widgets) w->visible = step_ == s;
    };
    on(Step::Action, {&hide_, &show_, &always_});
    for (overlay::Chip& c : kinds_) c.visible = step_ == Step::Kind;
    on(Step::Items, {&itemSearch_, &categoryList_, &entryList_, &all_, &none_, &chosen_});
    const bool tier = step_ == Step::Tier;
    for (overlay::Checkbox& b : tiers_) b.visible = tier && offer_.tierChoices.size() > 0;
    qualityLabel_.visible = tier && offer_.qualities;
    white_.visible = qualityLabel_.visible;
    superior_.visible = qualityLabel_.visible;
    mythLabel_.visible = tier && offer_.mythicals;
    for (overlay::Chip& c : myth_) c.visible = mythLabel_.visible;
    const bool stats = step_ == Step::Bonuses && offer_.statsPossible;
    for (Widget* w : std::initializer_list<Widget*>{&statSearch_, &statList_, &atLeastLabel_, &atLeast_}) w->visible = stats;
    for (overlay::Checkbox& b : levels_) b.visible = step_ == Step::Levels;
    const bool look = step_ == Step::Look;
    for (Widget* w : std::initializer_list<Widget*>{&borderLabel_, &backLabel_, &nameLabel_, &soundLabel_, &borderNone_, &backNone_, &nameGame_,
                                                    &beam_, &chat_, &sound_, &play_, &chatText_, &iconLabel_, &icon_, &iconSizeLabel_, &iconSize_,
                                                    &iconSizeValue_})
        w->visible = look;
    for (auto* row : {&borderSw_, &backSw_, &nameSw_, &beamSw_, &chatSw_})
        for (overlay::Swatch& s : *row) s.visible = look;
    checkLines_.visible = step_ == Step::Check;
    heading_.text = kStepHeadings[static_cast<int>(step_)];
    hint_.text = step_ == Step::Bonuses && !offer_.statsPossible
                     ? (offer_.statsWhy.empty() ? std::string("These items have no stats to choose.") : offer_.statsWhy)
                     : kStepHints[static_cast<int>(step_)];
}

void AddRuleView::Save(Ui& ui) {
    if (!Complete(draft_) || loaded_->session == nullptr) return;
    const std::vector<edit::OwnRule> rules = RulesNow();
    const std::string editing = editing_;
    std::shared_ptr<bool> alive = alive_;
    CommitChange(
        ui, loaded_, alive_,
        [rules, editing](edit::Session& s) {
            for (std::size_t i = 0; i < rules.size(); ++i) {
                if (i == 0 && !editing.empty()) s.ReplaceOwn(editing, rules[i]);
                else s.AddOwn(rules[i]);
            }
        },
        [this, alive](Ui& u, bool ok, bool, const std::string& error) {
            if (!*alive) return;
            if (!ok) {
                status_.text = UiFormat("Not saved: {0}", {error});
                return;
            }
            u.PopView();  // back to the rules: the list shows the new rule
        },
        [this](Ui&) { status_.text = UiText("Nothing changed."); });
}

auto AddRuleView::InitialFocus() -> Widget* { return draft_.action == DraftAction::None ? static_cast<Widget*>(&hide_) : &next_; }

auto AddRuleView::Pad(const overlay::PadEvent& event, Ui& ui) -> bool {
    if (event.button == overlay::pad::Button::LB) {
        back_.onClick(ui);
        return true;
    }
    if (event.button == overlay::pad::Button::RB) {
        next_.onClick(ui);
        return true;
    }
    return false;
}

void AddRuleView::Layout(const LayoutContext& c) {
    overlay::Measurer& m = c.measure;
    const Box& area = c.content;
    const overlay::Theme& theme = m.GetTheme();
    const int gap = m.S(8);
    const int chipH = std::max(m.S(28), m.LineHeight(Font::Small) + m.S(12));
    const int rowH = std::max(m.S(28), m.LineHeight(Font::Body) + m.S(8));
    const int buttonH = std::max(m.S(32), m.LineHeight(Font::Body) + m.S(12));
    const int small = m.LineHeight(Font::Small) + m.S(4);

    int x = area.left;
    for (overlay::Chip& chip : stepChips_) {
        const int w = m.TextWidth(UiText(chip.label), Font::Small) + m.S(20);
        chip.box = Box{x, area.top, x + w, area.top + chipH};
        x += w + m.S(6);
    }
    int y = area.top + chipH + m.S(12);
    heading_.box = Box{area.left, y, area.right, y + m.LineHeight(Font::Bold) + m.S(4)};
    heading_.color = theme.headerGold;
    y = heading_.box.bottom;
    hint_.box = Box{area.left, y, area.right, y + m.LineHeight(Font::Small) * 2 + m.S(4)};
    hint_.color = theme.dim;
    y = hint_.box.bottom + gap;

    back_.box = Box{area.left, area.bottom - buttonH, area.left + m.S(120), area.bottom};
    const int finishW = m.TextWidth(UiText(finish_.label), Font::Body) + m.S(40);
    finish_.box = Box{area.right - finishW, area.bottom - buttonH, area.right, area.bottom};
    next_.box = Box{finish_.box.left - gap - m.S(120), area.bottom - buttonH, finish_.box.left - gap, area.bottom};
    status_.box = Box{back_.box.right + gap, area.bottom - buttonH, next_.box.left - gap, area.bottom};
    status_.color = status_.text.rfind(UiText("Not saved"), 0) == 0 ? theme.danger : theme.dim;
    summary_.box = Box{area.left, back_.box.top - gap - small, area.right, back_.box.top - gap};
    summary_.color = Complete(draft_) ? theme.gold : theme.dim;
    const Box body{area.left, y, area.right, summary_.box.top - gap};
    previewBox_ = Box{};
    minimapBox_ = Box{};

    switch (step_) {
        case Step::Action: {
            int cx = body.left;
            for (overlay::Chip* chip : {&hide_, &show_, &always_}) {
                const int w = std::max(m.S(150), m.TextWidth(UiText(chip->label), Font::Body) + m.S(30));
                chip->box = Box{cx, body.top, cx + w, body.top + buttonH + m.S(8)};
                cx += w + gap;
            }
            break;
        }
        case Step::Kind: {
            int cy = body.top;
            for (overlay::Chip& chip : kinds_) {
                chip.box = Box{body.left, cy, body.left + std::min(body.Width(), m.S(380)), cy + buttonH + m.S(4)};
                cy += buttonH + m.S(4) + m.S(6);
            }
            break;
        }
        case Step::Items: {
            itemSearch_.box = Box{body.left, body.top, body.right, body.top + std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10))};
            const int listTop = itemSearch_.box.bottom + gap;
            const int chosenH = m.LineHeight(Font::Small) * 2 + m.S(6);
            const int listBottom = body.bottom - chosenH - gap;
            const int leftW = std::clamp(body.Width() * 30 / 100, m.S(180), m.S(320));
            categoryList_.box = Box{body.left, listTop, body.left + leftW, listBottom};
            categoryList_.rowHeight = rowH;
            const int buttonsW = m.S(110);
            entryList_.box = Box{categoryList_.box.right + gap, listTop, body.right - buttonsW - gap, listBottom};
            entryList_.rowHeight = rowH;
            all_.box = Box{body.right - buttonsW, listTop, body.right, listTop + buttonH};
            none_.box = Box{body.right - buttonsW, all_.box.bottom + m.S(6), body.right, all_.box.bottom + m.S(6) + buttonH};
            chosen_.box = Box{body.left, listBottom + gap, body.right, body.bottom};
            chosen_.color = theme.text;
            break;
        }
        case Step::Tier: {
            int cy = body.top;
            if (qualityLabel_.visible) {
                qualityLabel_.box = Box{body.left, cy, body.right, cy + small};
                cy += small;
                white_.box = Box{body.left, cy, body.left + m.S(320), cy + rowH};
                cy += rowH;
                superior_.box = Box{body.left, cy, body.left + m.S(320), cy + rowH};
                cy += rowH + m.S(14);
            }
            for (overlay::Checkbox& box : tiers_) {
                box.box = Box{body.left, cy, body.left + m.S(320), cy + rowH};
                cy += rowH;
            }
            cy += m.S(14);
            mythLabel_.box = Box{body.left, cy, body.right, cy + small};
            cy += small;
            int cx = body.left;
            for (overlay::Chip& chip : myth_) {
                const int w = m.TextWidth(UiText(chip.label), Font::Small) + m.S(24);
                chip.box = Box{cx, cy, cx + w, cy + chipH};
                cx += w + m.S(6);
            }
            break;
        }
        case Step::Bonuses: {
            statSearch_.box = Box{body.left, body.top, body.right, body.top + std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10))};
            const int numberH = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
            atLeast_.box = Box{body.left + m.S(90), body.bottom - numberH, body.left + m.S(260), body.bottom};
            atLeastLabel_.box = Box{body.left, atLeast_.box.top, atLeast_.box.left - gap, atLeast_.box.bottom};
            statList_.box = Box{body.left, statSearch_.box.bottom + gap, body.right, atLeast_.box.top - gap};
            statList_.rowHeight = rowH;
            break;
        }
        case Step::Levels: {
            int cy = body.top;
            for (overlay::Checkbox& box : levels_) {
                box.box = Box{body.left, cy, body.left + m.S(360), cy + rowH};
                cy += rowH;
            }
            break;
        }
        case Step::Look: {
            const int split = body.left + body.Width() * 68 / 100;  // the colour rows get most of the width
            const Box left{body.left, body.top, split - gap * 2, body.bottom};
            const Box right{split, body.top, body.right, body.bottom};
            const int swH = std::max(m.S(24), m.LineHeight(Font::Small) + m.S(8));
            int cy = left.top;
            const auto row = [&](overlay::Label* label, overlay::Widget* first, auto& swatches) {
                if (label != nullptr) {
                    label->box = Box{left.left, cy, left.right, cy + small};
                    cy += small;
                }
                int cx = left.left;
                if (first != nullptr) {
                    const int w = m.S(86);
                    first->box = Box{cx, cy, cx + w, cy + swH};
                    cx += w + m.S(4);
                }
                const int count = static_cast<int>(swatches.size());
                const int w = count > 0 ? std::max(m.S(20), (left.right - cx - m.S(3) * (count - 1)) / count) : 0;
                for (overlay::Swatch& s : swatches) {
                    s.box = Box{cx, cy, cx + w, cy + swH};
                    cx += w + m.S(3);
                }
                cy += swH + m.S(8);
            };
            row(&borderLabel_, &borderNone_, borderSw_);
            row(&backLabel_, &backNone_, backSw_);
            row(&nameLabel_, &nameGame_, nameSw_);
            beam_.box = Box{left.left, cy, left.right, cy + rowH};
            cy += rowH;
            row(nullptr, nullptr, beamSw_);
            soundLabel_.box = Box{left.left, cy, left.right, cy + small};
            cy += small;
            const int dropH = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
            sound_.box = Box{left.left, cy, left.left + std::min(m.S(300), left.Width() - m.S(100)), cy + dropH};
            play_.box = Box{sound_.box.right + gap, cy, sound_.box.right + gap + m.S(90), cy + dropH};
            cy += dropH + m.S(8);
            chat_.box = Box{left.left, cy, left.left + m.S(260), cy + rowH};
            chatText_.box = Box{chat_.box.right + gap, cy, left.right, cy + rowH};
            cy += rowH + m.S(4);
            row(nullptr, nullptr, chatSw_);
            iconLabel_.box = Box{left.left, cy, left.right, cy + small};
            cy += small;
            icon_.box = Box{left.left, cy, left.left + std::min(m.S(300), left.Width() / 2), cy + dropH};
            iconSizeLabel_.box = Box{icon_.box.right + gap * 2, cy, icon_.box.right + gap * 2 + m.S(40), cy + dropH};
            iconSizeLabel_.flags = kVCenter | kSingleLine;
            iconSizeValue_.box = Box{left.right - m.S(60), cy, left.right, cy + dropH};
            iconSizeValue_.color = theme.text;
            iconSize_.box = Box{iconSizeLabel_.box.right + m.S(4), cy, iconSizeValue_.box.left - gap, cy + dropH};
            // Right: the label on the ground, then the minimap.
            previewBox_ = Box{right.left, right.top, right.right, right.top + right.Height() * 55 / 100};
            minimapBox_ = Box{right.left, previewBox_.bottom + gap, right.right, right.bottom};
            break;
        }
        case Step::Check: {
            checkLines_.box = body;
            checkLines_.rowHeight = std::max(m.S(26), m.LineHeight(Font::Bold) + m.S(8));
            break;
        }
    }
}

void AddRuleView::FocusList(std::vector<Widget*>& out) {
    out.clear();
    for (overlay::Chip& c : stepChips_) out.push_back(&c);
    for (Widget* w : std::initializer_list<Widget*>{&heading_, &hint_, &hide_, &show_, &always_}) out.push_back(w);
    for (overlay::Chip& c : kinds_) out.push_back(&c);
    for (Widget* w : std::initializer_list<Widget*>{&itemSearch_, &categoryList_, &entryList_, &all_, &none_, &chosen_, &qualityLabel_, &white_, &superior_})
        out.push_back(w);
    for (overlay::Checkbox& b : tiers_) out.push_back(&b);
    out.push_back(&mythLabel_);
    for (overlay::Chip& c : myth_) out.push_back(&c);
    for (Widget* w : std::initializer_list<Widget*>{&statSearch_, &statList_, &atLeastLabel_, &atLeast_}) out.push_back(w);
    for (overlay::Checkbox& b : levels_) out.push_back(&b);
    out.push_back(&borderLabel_);
    out.push_back(&borderNone_);
    for (overlay::Swatch& s : borderSw_) out.push_back(&s);
    out.push_back(&backLabel_);
    out.push_back(&backNone_);
    for (overlay::Swatch& s : backSw_) out.push_back(&s);
    out.push_back(&nameLabel_);
    out.push_back(&nameGame_);
    for (overlay::Swatch& s : nameSw_) out.push_back(&s);
    out.push_back(&beam_);
    for (overlay::Swatch& s : beamSw_) out.push_back(&s);
    for (Widget* w : std::initializer_list<Widget*>{&soundLabel_, &sound_, &play_, &chat_, &chatText_}) out.push_back(w);
    for (overlay::Swatch& s : chatSw_) out.push_back(&s);
    for (Widget* w : std::initializer_list<Widget*>{&iconLabel_, &icon_, &iconSizeLabel_, &iconSize_, &iconSizeValue_}) out.push_back(w);
    for (Widget* w : std::initializer_list<Widget*>{&checkLines_, &summary_, &status_, &back_, &next_, &finish_}) out.push_back(w);
}

void AddRuleView::Paint(Canvas& canvas) {
    if (step_ != Step::Look || previewBox_.Width() <= 0) return;
    PaintLabelPreview(canvas, previewBox_);
    if (minimapBox_.Height() > canvas.S(40)) PaintMinimapPreview(canvas, minimapBox_);
}

void AddRuleView::PaintLabelPreview(Canvas& canvas, const Box& p) {
    // The label on the ground: a dark floor, the beam rising behind, the label with its background, border and name.
    const overlay::Theme& theme = canvas.GetTheme();
    canvas.Fill(p, overlay::Rgb{22, 20, 18});
    canvas.Frame(p, theme.border);
    const Look& look = draft_.look;
    const std::string name = PreviewName();
    const int textW = canvas.TextWidth(name, Font::Bold);
    const int labelW = textW + canvas.S(20);
    const int labelH = canvas.LineHeight(Font::Bold) + canvas.S(8);
    const int cx = p.CentreX();
    const int labelTop = p.bottom - labelH - p.Height() / 5;
    if (look.beam) {
        const overlay::Rgb beam = ToOverlay(look.beamColor);
        const int w = canvas.S(10);
        for (int i = 0; i < 6; ++i) {
            const Box band{cx - w / 2 - i * canvas.S(2), p.top + canvas.S(6), cx + w / 2 + i * canvas.S(2), labelTop};
            canvas.FillAlpha(band, beam, static_cast<std::uint8_t>(60 - i * 9));
        }
    }
    const Box label{cx - labelW / 2, labelTop, cx + labelW / 2, labelTop + labelH};
    if (look.background) canvas.FillAlpha(label, ToOverlay(*look.background), 230);
    else canvas.FillAlpha(label, overlay::Rgb{0, 0, 0}, 170);
    if (look.border) {
        const overlay::Rgb border = ToOverlay(*look.border);
        canvas.Frame(label, border);
        canvas.Frame(label.Inflated(-1), border);
    }
    const overlay::Rgb ink = look.nameColor >= 0 ? ToOverlay(NameColors()[static_cast<std::size_t>(look.nameColor)].rgb) : KindColor(draft_.kind);
    canvas.Text(name, label, ink, Font::Bold, kCenter | kVCenter | kSingleLine | kNoHighlight);
    // The chat line under it.
    if (look.chat) {
        std::string line = look.chatText;
        const std::size_t at = line.find("{name}");
        if (at != std::string::npos) line.replace(at, 6, name);
        const NameColor& c = NameColors()[static_cast<std::size_t>(std::clamp(look.chatColor, 0, static_cast<int>(NameColors().size()) - 1))];
        canvas.Text(line, Box{p.left + canvas.S(10), p.bottom - canvas.S(6) - canvas.LineHeight(Font::Small), p.right - canvas.S(10), p.bottom - canvas.S(4)},
                    ToOverlay(c.rgb), Font::Small, kSingleLine | kNoHighlight);
    }
    if (!look.sound.empty())
        canvas.Text(UiFormat("Sound: {0}", {look.sound}), Box{p.left + canvas.S(10), p.top + canvas.S(6), p.right - canvas.S(10), p.top + canvas.S(6) + canvas.LineHeight(Font::Small)},
                    theme.dim, Font::Small, kSingleLine | kNoHighlight);
}

auto AddRuleView::IconPicture(const std::string& name) -> const Picture* {
    if (name.empty() || loaded_->session == nullptr) return nullptr;
    auto found = iconPictures_.find(name);
    if (found == iconPictures_.end()) {
        Picture picture;
        if (!ReadSprite(loaded_->session->IconPath(name), 128, picture)) picture = Picture{};
        found = iconPictures_.emplace(name, std::move(picture)).first;
    }
    return found->second.width > 0 ? &found->second : nullptr;
}

void AddRuleView::PaintMinimapPreview(Canvas& canvas, const Box& p) {
    // The minimap where it dropped: a dark map, the player's marker, the icon at its size (as the game draws it at 100%).
    const overlay::Theme& theme = canvas.GetTheme();
    canvas.Fill(p, overlay::Rgb{14, 16, 20});
    canvas.Frame(p, theme.border);
    canvas.Text(UiText("Minimap"), Box{p.left + canvas.S(8), p.top + canvas.S(4), p.right - canvas.S(8), p.top + canvas.S(4) + canvas.LineHeight(Font::Small)},
                theme.dim, Font::Small, kSingleLine | kNoHighlight);
    // A few walls, so it reads as a map.
    const overlay::Rgb wall{70, 74, 82};
    const int cx = p.CentreX(), cy = p.CentreY() + canvas.S(8);
    const int u = canvas.S(26);
    canvas.Fill(Box{cx - 3 * u, cy - 2 * u, cx + 3 * u, cy - 2 * u + canvas.S(2)}, wall);
    canvas.Fill(Box{cx - 3 * u, cy + 2 * u, cx + 3 * u, cy + 2 * u + canvas.S(2)}, wall);
    canvas.Fill(Box{cx - 3 * u, cy - 2 * u, cx - 3 * u + canvas.S(2), cy + 2 * u}, wall);
    canvas.Fill(Box{cx + 3 * u, cy - 2 * u, cx + 3 * u + canvas.S(2), cy + 2 * u + canvas.S(2)}, wall);
    // The player.
    const int px = cx - 2 * u, dot = canvas.S(5);
    canvas.Fill(Box{px - dot, cy - dot, px + dot, cy + dot}, overlay::Rgb{230, 230, 230});
    const Look& look = draft_.look;
    const Picture* picture = IconPicture(look.icon);
    const Box icon{cx + u - canvas.S(look.iconSize) / 2, cy - canvas.S(look.iconSize) / 2, cx + u + canvas.S(look.iconSize) / 2,
                   cy + canvas.S(look.iconSize) / 2};
    if (picture != nullptr) {
        canvas.Pixels("icon:" + look.icon, picture->width, picture->height, picture->bgra, icon.Intersect(p));
    } else {
        canvas.Text(look.icon.empty() ? UiText("No icon") : UiFormat("{0} could not be read", {look.icon}),
                    Box{p.left + canvas.S(8), p.bottom - canvas.S(6) - canvas.LineHeight(Font::Small), p.right - canvas.S(8), p.bottom - canvas.S(4)},
                    theme.dim, Font::Small, kSingleLine | kNoHighlight);
    }
}

void AddRuleView::HelpRows(const Widget*, std::vector<overlay::HelpRow>& out) const {
    out.push_back({overlay::Glyph::LBRB, "", "Previous / next step"});
}

}  // namespace esrfe::ui
