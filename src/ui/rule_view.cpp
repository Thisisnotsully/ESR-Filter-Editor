#include "rule_view.hpp"

#include <algorithm>
#include <initializer_list>

#include "add_rule_view.hpp"
#include "rule_draft.h"
#include "ui_text.hpp"

namespace esrfe::ui {



using namespace overlay::text_flags;
using document::Value;
using overlay::Font;
using overlay::UiFormat;
using overlay::UiText;

auto OpenRuleEditor(Ui& ui, const std::shared_ptr<Loaded>& loaded, const std::string& id) -> std::string {
    if (!loaded || loaded->session == nullptr || loaded->game == nullptr) return UiText("The filter cannot be changed here.");
    if (id.empty()) return UiText("This rule has no id (R### or U###), so it cannot be edited.");
    const edit::Session& session = *loaded->session;
    RuleDraft draft;
    if (const edit::OwnRule* own = session.Own(id); own != nullptr) {
        if (own->replaces.empty() && FromRule(own->rule, *loaded->game, draft)) {  // made with the steps
            ui.PushView(std::make_shared<AddRuleView>(loaded, draft, id));
            return {};
        }
        if (EditFromRule(own->rule, *loaded->game, draft)) {
            ui.PushView(std::make_shared<AddRuleView>(loaded, draft, id, own->replaces));
            return {};
        }
        return UiText("The steps cannot tell which items this rule is for, so they cannot edit it.");
    }
    if (session.AuthorRule(id) == nullptr) return UiText("This rule cannot be edited.");
    if (!EditFromRule(session.Effective(id), *loaded->game, draft))
        return UiText("The steps cannot tell which items this rule is for, so they cannot edit it. Its strictness levels, what it does and its items can still be changed here.");
    ui.PushView(std::make_shared<AddRuleView>(loaded, draft, std::string{}, id));
    return {};
}

namespace {

constexpr const char* kLists[] = {"uniqueOrSetIds", "itemCodes", "itemTypes"};

auto Truthy(const Value* v) -> bool { return v != nullptr && v->IsBool() && v->boolean; }

}  // namespace

RuleView::RuleView(std::shared_ptr<Loaded> loaded, int rule) : loaded_(std::move(loaded)), rule_(rule) {
    const document::Rule& r = loaded_->doc.Rules()[static_cast<std::size_t>(rule_)];
    id_ = r.id;
    edit::Session* session = loaded_->session.get();
    editable_ = session != nullptr && !id_.empty() && session->AuthorRule(id_) != nullptr;
    own_ = session != nullptr && !id_.empty() && session->Own(id_) != nullptr && loaded_->game != nullptr;

    badge_.translate = false;
    badge_.font = Font::Bold;
    badge_.flags = kCenter | kVCenter | kSingleLine;
    badge_.useThemeColor = false;
    what_.translate = false;
    what_.font = Font::Title;
    what_.flags = kLeft | kVCenter | kSingleLine;

    lines_.drawRowBackground = false;
    lines_.rowGap = 0;
    lines_.paintRow = [this](overlay::Canvas& canvas, int index, const overlay::Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(content_.size())) return;
        const Line& line = content_[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const overlay::Box text{row.left + canvas.S(line.kind == Line::Kind::Heading ? 8 : 24), row.top, row.right - canvas.S(8), row.bottom};
        canvas.PushClip(text);
        switch (line.kind) {
            case Line::Kind::Heading: canvas.Text(line.text, text, theme.headerGold, Font::Bold, kVCenter | kSingleLine); break;
            case Line::Kind::Item: canvas.Text(line.text, text, theme.text, Font::Body, kVCenter | kSingleLine); break;
            case Line::Kind::Preview: {
                const std::string label = UiText("Shown as:") + " ";
                const int w = canvas.TextWidth(label, Font::Body);
                canvas.Text(label, text, theme.text, Font::Body, kVCenter | kSingleLine);
                // The new name in the game's colours on a dark strip, as it looks on the ground.
                const overlay::Box name{text.left + w, row.top + canvas.S(2),
                                        std::min(text.right, text.left + w + canvas.TextWidth(line.text, Font::Bold) + canvas.S(16)),
                                        row.bottom - canvas.S(2)};
                canvas.FillAlpha(name, overlay::Rgb{0, 0, 0}, 200);
                canvas.Text(line.text, name, theme.body, Font::Bold, kCenter | kVCenter | kSingleLine);
                break;
            }
            case Line::Kind::Note: canvas.Text(line.text, text, theme.dim, Font::Small, kVCenter | kSingleLine); break;
        }
        canvas.PopClip();
    };

    panel_.title = "Your changes";

    on_.label = "Rule is on";
    on_.onChange = [this](bool on, Ui& ui) {
        if (own_) Commit(ui, [this, on](edit::Session& s) { s.SetOwnOff(id_, !on); });
        else Commit(ui, [this, on](edit::Session& s) { s.SetOff(id_, !on); });
    };
    change_.label = "Edit this rule";
    change_.onClick = [this](Ui& ui) {
        if (const std::string why = OpenRuleEditor(ui, loaded_, id_); !why.empty()) {
            status_.text = why;
            ui.Repaint();
        }
    };
    delete_.label = "Delete this rule";
    delete_.style = overlay::Button::Style::Danger;
    delete_.onClick = [this](Ui& ui) {
        overlay::DialogSpec spec;
        spec.title = "Delete your rule?";
        spec.message = UiFormat("Rule {0} goes away for good.", {id_});
        spec.confirm = "Delete";
        spec.danger = true;
        std::shared_ptr<bool> alive = alive_;
        ui.ShowDialog(spec, [this, alive, &ui](bool yes) {
            if (!*alive || !yes) return;
            CommitChange(
                ui, loaded_, alive_, [this](edit::Session& s) { s.RemoveOwn(id_); },
                [this](Ui& u, bool ok, bool, const std::string& error) {
                    if (ok) u.PopView();
                    else status_.text = UiFormat("Not saved: {0}", {error});
                },
                nullptr);
        });
    };

    actionLabel_.text = "What it does";
    actionLabel_.font = Font::Small;
    hide_.label = "Hide";
    show_.label = "Always show";
    neither_.label = "Neither";
    hide_.onClick = [this](Ui& ui) { SetAction(Act::Hide, ui); };
    show_.onClick = [this](Ui& ui) { SetAction(Act::Show, ui); };
    neither_.onClick = [this](Ui& ui) { SetAction(Act::Neither, ui); };

    levelsLabel_.text = "Filter levels it is used on";
    levelsLabel_.font = Font::Small;
    for (const document::FilterLevel& l : loaded_->doc.GetSettings().filterLevels) {
        levelIds_.push_back(l.id);
        overlay::Checkbox& box = levels_.emplace_back();
        box.label = PickLanguage(l.name, loaded_->language);
        box.translate = false;
        const std::size_t index = levels_.size() - 1;
        box.onChange = [this, index](bool on, Ui& ui) { SetLevel(index, on, ui); };
    }

    itemsLabel_.text = "Items (untick one to leave it alone)";
    itemsLabel_.font = Font::Small;
    items_.rowGap = 0;
    items_.paintRow = [this](overlay::Canvas& canvas, int index, const overlay::Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(entries_.size())) return;
        const Entry& e = entries_[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const int size = canvas.S(14);
        const overlay::Box tick{row.left + canvas.S(8), row.CentreY() - size / 2, row.left + canvas.S(8) + size, row.CentreY() - size / 2 + size};
        canvas.Fill(tick, theme.field);
        canvas.Frame(tick, theme.border);
        if (e.on) canvas.Fill(tick.Inflated(-canvas.S(3)), theme.gold);
        const overlay::Box text{tick.right + canvas.S(8), row.top, row.right - canvas.S(6), row.bottom};
        canvas.PushClip(text);
        canvas.Text(e.name, text, e.on ? theme.text : theme.dim, Font::Body, kVCenter | kSingleLine);
        canvas.PopClip();
    };
    items_.onClick = [this](int index, Ui& ui) { ToggleItem(index, ui); };
    items_.onActivate = [this](int index, Ui& ui) { ToggleItem(index, ui); };

    reset_.label = "Undo my changes to this rule";
    reset_.onClick = [this](Ui& ui) {
        Commit(ui, [this](edit::Session& s) { s.Reset(id_); });
    };

    status_.translate = false;
    status_.font = Font::Small;
    status_.useThemeColor = false;
    status_.flags = kLeft;  // wraps
    note_.translate = false;
    note_.font = Font::Small;
    note_.useThemeColor = false;
    note_.flags = kLeft;
    if (!editable_) {
        if (session == nullptr) note_.text = UiFormat("This filter cannot be changed here: {0}", {loaded_->sessionError});
        else if (own_) note_.text.clear();
        else if (id_.rfind('U', 0) == 0) note_.text = UiText("This is one of your own rules, kept from a rule the author removed.");
        else note_.text = UiText("This rule has no R### id in its title, so the editor cannot keep a change to it through the author's updates.");
    }

    back_.label = "< Back";
    back_.onClick = [](Ui& ui) { ui.PopView(); };

    // The items, as the author wrote them.
    if (editable_) {
        const Value& author = *session->AuthorRule(id_);
        const Value* quality = author.Find("quality");
        const bool sets = quality != nullptr && quality->IsString() && quality->Str() == "set";
        for (const char* key : kLists) {
            const Value* list = author.Find(key);
            if (list == nullptr || !list->IsArray()) continue;
            for (const Value& entry : list->array) entries_.push_back(Entry{key, entry, EntryName(key, entry, loaded_->context, sets), true});
        }
        if (entries_.size() < 2) entries_.clear();  // one item: switch the rule off instead
        const Value current = Current();
        actionShown_ = author.Find("hide") != nullptr || author.Find("show") != nullptr || current.Find("hide") != nullptr ||
                       current.Find("show") != nullptr;
    }
    Describe();
    SyncControls();
}

auto RuleView::Current() const -> Value {
    if (editable_ || own_) return loaded_->session->Effective(id_);
    return loaded_->doc.Rules()[static_cast<std::size_t>(rule_)].value;
}

void RuleView::Describe() {
    const Value rule = Current();
    summary_ = esrfe::ui::Describe(rule, loaded_->context);
    content_.clear();
    const RuleSummary& s = summary_;
    const document::Rule& r = loaded_->doc.Rules()[static_cast<std::size_t>(rule_)];
    const auto add = [this](Line::Kind kind, std::string text) { content_.push_back(Line{kind, std::move(text)}); };

    add(Line::Kind::Heading, s.whatAll.size() > 1 ? UiFormat("Items ({0})", {std::to_string(s.whatAll.size())}) : UiText("Items"));
    if (s.whatAll.empty()) add(Line::Kind::Item, UiText("Any item"));
    for (const std::string& name : s.whatAll) add(Line::Kind::Item, name);
    add(Line::Kind::Heading, UiText("When"));
    if (s.conditions.empty()) add(Line::Kind::Item, UiText("Every time"));
    for (const std::string& c : s.conditions) add(Line::Kind::Item, c);
    add(Line::Kind::Heading, UiText("What it does"));
    const bool off = (editable_ || own_) && loaded_->session->IsOff(id_);
    if (off) add(Line::Kind::Item, UiText("Nothing: you switched this rule off"));
    switch (s.action) {
        case Action::Hide: add(Line::Kind::Item, UiText("Hides the item on the ground")); break;
        case Action::Show: add(Line::Kind::Item, UiText("Always shows the item: later rules cannot hide it")); break;
        default: break;
    }
    if (!s.preview.empty()) add(Line::Kind::Preview, s.preview);
    for (const std::string& e : s.effects) {
        if (!s.preview.empty() && e == UiFormat("Shown as: {0}", {s.preview})) continue;
        add(Line::Kind::Item, e);
    }
    if (s.action == Action::Other && s.effects.empty()) add(Line::Kind::Item, UiText("Nothing the editor knows how to describe"));
    add(Line::Kind::Heading, UiText("About this rule"));
    if (!r.title.empty()) add(Line::Kind::Note, r.title);
    const std::string place = loaded_->Place(rule_);
    if (!place.empty()) add(Line::Kind::Note, UiFormat("In: {0}", {place}));
    add(Line::Kind::Note, UiFormat("Rule {0} of {1}; rules run from the top, and the first one that hides an item wins.",
                                   {std::to_string(rule_ + 1), std::to_string(loaded_->doc.ActiveCount())}));
    badge_.text = off ? UiText("Off") : s.actionLabel;
    what_.text = s.what;
    lines_.SetCount(static_cast<int>(content_.size()));
}

void RuleView::SyncControls() {
    change_.visible = own_ || editable_;
    delete_.visible = own_;
    if (own_) {
        for (Widget* w : std::initializer_list<Widget*>{&actionLabel_, &hide_, &show_, &neither_, &levelsLabel_, &itemsLabel_, &items_, &reset_, &note_})
            w->visible = false;
        for (overlay::Checkbox& box : levels_) box.visible = false;
        panel_.visible = true;
        on_.visible = true;
        status_.visible = true;
        on_.on = !loaded_->session->IsOff(id_);
        return;
    }
    const bool show = editable_;
    for (Widget* w : std::initializer_list<Widget*>{&panel_, &on_, &reset_, &status_}) w->visible = show;
    note_.visible = !show;
    if (!show) {
        for (Widget* w : std::initializer_list<Widget*>{&actionLabel_, &hide_, &show_, &neither_, &levelsLabel_, &itemsLabel_, &items_})
            w->visible = false;
        for (overlay::Checkbox& box : levels_) box.visible = false;
        panel_.visible = !note_.text.empty();
        return;
    }
    edit::Session& session = *loaded_->session;
    const bool off = session.IsOff(id_);
    on_.on = !off;
    const Value rule = Current();

    for (Widget* w : std::initializer_list<Widget*>{&actionLabel_, &hide_, &show_, &neither_}) w->visible = actionShown_;
    const bool hides = Truthy(rule.Find("hide"));
    const bool shows = Truthy(rule.Find("show"));
    hide_.selected = hides;
    show_.selected = shows && !hides;
    neither_.selected = !hides && !shows;

    levelsLabel_.visible = !levels_.empty();
    const Value* active = rule.Find("activeInLevels");
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        bool on = active == nullptr || !active->IsArray();
        if (!on) {
            for (const Value& v : active->array) {
                std::int64_t id = 0;
                if (v.AsInteger(id) && id == levelIds_[i]) on = true;
            }
        }
        levels_[i].on = on;
        levels_[i].visible = true;
        levels_[i].enabled = !off;
    }

    const edit::RuleChange* change = session.Change(id_);
    for (Entry& e : entries_) {
        const std::vector<Value>* removed = change != nullptr ? change->RemovedFrom(e.key) : nullptr;
        e.on = removed == nullptr || std::none_of(removed->begin(), removed->end(), [&](const Value& v) { return v == e.value; });
    }
    itemsLabel_.visible = !entries_.empty();
    items_.visible = !entries_.empty();
    items_.SetCount(static_cast<int>(entries_.size()));
    reset_.visible = change != nullptr;
    for (Widget* w : std::initializer_list<Widget*>{&hide_, &show_, &neither_, &items_}) w->enabled = !off;
}

void RuleView::SetAction(Act act, Ui& ui) {
    Commit(ui, [this, act](edit::Session& s) {
        const Value yes = Value::Bool(true);
        const Value* author = s.AuthorRule(id_);
        // Exactly the author's form when the player picks what the author had.
        const auto put = [&](const char* key, bool want) {
            const Value* had = author != nullptr ? author->Find(key) : nullptr;
            if (want) s.SetField(id_, key, Truthy(had) ? had : &yes);
            else s.SetField(id_, key, had != nullptr && !Truthy(had) ? had : nullptr);
        };
        put("hide", act == Act::Hide);
        put("show", act == Act::Show);
    });
}

void RuleView::SetLevel(std::size_t index, bool on, Ui& ui) {
    std::vector<std::int64_t> chosen;
    for (std::size_t i = 0; i < levels_.size(); ++i)
        if (i == index ? on : levels_[i].on) chosen.push_back(levelIds_[i]);
    if (chosen.empty()) {
        levels_[index].on = true;
        status_.text = UiText("A rule needs at least one level. Switch it off instead.");
        ui.Repaint();
        return;
    }
    Commit(ui, [this, chosen](edit::Session& s) {
        const Value* author = s.AuthorRule(id_);
        const Value* had = author != nullptr ? author->Find("activeInLevels") : nullptr;
        if (chosen.size() == levelIds_.size()) {  // every level: as the author wrote it, or no list at all
            const bool authorAll = had == nullptr || (had->IsArray() && had->array.size() == levelIds_.size());
            s.SetField(id_, "activeInLevels", authorAll ? had : nullptr);
            return;
        }
        Value list = Value::Array();
        for (std::int64_t id : chosen) list.array.push_back(Value::Integer(id));
        s.SetField(id_, "activeInLevels", &list);
    });
}

void RuleView::ToggleItem(int index, Ui& ui) {
    if (index < 0 || index >= static_cast<int>(entries_.size()) || !editable_ || loaded_->session->IsOff(id_)) return;
    const Entry& e = entries_[static_cast<std::size_t>(index)];
    if (e.on) {
        int left = 0;
        for (const Entry& other : entries_)
            if (other.key == e.key && other.on) ++left;
        if (left <= 1) {
            status_.text = UiText("A rule needs at least one item. Switch it off instead.");
            ui.Repaint();
            return;
        }
    }
    const std::string key = e.key;
    const Value value = e.value;
    const bool remove = e.on;
    Commit(ui, [this, key, value, remove](edit::Session& s) { s.SetItem(id_, key, value, remove); });
}

void RuleView::Commit(Ui& ui, std::function<void(edit::Session&)> change) {
    if (!editable_ && !own_) return;
    CommitChange(
        ui, loaded_, alive_, std::move(change),
        [this](Ui&, bool ok, bool switched, const std::string& error) { Saved(ok, switched, error); },
        [this](Ui&) {
            SyncControls();  // the control goes back to what it was
            status_.text = UiText("Nothing changed.");
        });
}

void RuleView::Saved(bool ok, bool switched, const std::string& error) {
    Describe();
    SyncControls();
    if (!ok) {
        status_.text = UiFormat("Not saved: {0}", {error});
        return;
    }
    status_.text = switched ? UiText("Saved. The game now uses your version of the filter.") : UiText("Saved. The game uses it at once.");
}

auto RuleView::Title() const -> std::string {
    return id_.empty() ? UiFormat("Rule {0}", {std::to_string(rule_ + 1)}) : UiFormat("Rule {0}", {id_});
}

auto RuleView::InitialFocus() -> Widget* { return editable_ || own_ ? static_cast<Widget*>(&on_) : &lines_; }

void RuleView::Layout(const LayoutContext& c) {
    overlay::Measurer& m = c.measure;
    const overlay::Box& area = c.content;
    const overlay::Theme& theme = m.GetTheme();
    const int gap = m.S(8);
    const int head = std::max(m.S(36), m.LineHeight(Font::Title) + m.S(12));
    badge_.box = overlay::Box{area.left, area.top, area.left + m.S(130), area.top + head};
    badge_.color = (editable_ || own_) && loaded_->session->IsOff(id_) ? theme.dim : ActionColor(summary_.action, theme);
    what_.box = overlay::Box{badge_.box.right + m.S(12), area.top, area.right, area.top + head};
    const int buttonHeight = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
    back_.box = overlay::Box{area.left, area.bottom - buttonHeight, area.left + m.S(120), area.bottom};

    const bool side = editable_ || own_ || !note_.text.empty();
    const int split = side ? area.left + area.Width() * 55 / 100 : area.right + gap;
    lines_.box = overlay::Box{area.left, badge_.box.bottom + gap, split - gap, back_.box.top - gap};
    lines_.rowHeight = std::max(m.S(26), m.LineHeight(Font::Bold) + m.S(8));
    lines_.SetCount(static_cast<int>(content_.size()));

    panel_.box = overlay::Box{split, badge_.box.bottom + gap, area.right, area.bottom};
    note_.box = panel_.box.Inflated(-m.S(16));
    note_.color = theme.dim;
    if (own_) {
        const int left = panel_.box.left + m.S(16);
        const int right = panel_.box.right - m.S(16);
        int y = panel_.box.top + m.S(8) + m.LineHeight(Font::Bold) + m.S(12);
        const int rowH = std::max(m.S(28), m.LineHeight(Font::Body) + m.S(8));
        on_.box = overlay::Box{left, y, right, y + rowH};
        y += rowH + gap;
        change_.box = overlay::Box{left, y, right, y + buttonHeight};
        y += buttonHeight + gap;
        delete_.box = overlay::Box{left, y, right, y + buttonHeight};
        const int statusH = m.LineHeight(Font::Small) * 2 + m.S(4);
        status_.box = overlay::Box{left, panel_.box.bottom - m.S(12) - statusH, right, panel_.box.bottom - m.S(12)};
        status_.color = theme.dim;
        return;
    }
    if (!editable_) return;
    const int left = panel_.box.left + m.S(16);
    const int right = panel_.box.right - m.S(16);
    int y = panel_.box.top + m.S(8) + m.LineHeight(Font::Bold) + m.S(12);
    const int rowH = std::max(m.S(28), m.LineHeight(Font::Body) + m.S(8));
    const int small = m.LineHeight(Font::Small) + m.S(4);
    on_.box = overlay::Box{left, y, right, y + rowH};
    y += rowH + gap;
    if (actionShown_) {
        actionLabel_.box = overlay::Box{left, y, right, y + small};
        y += small + m.S(4);
        const int chipH = std::max(m.S(26), m.LineHeight(Font::Small) + m.S(10));
        int x = left;
        for (overlay::Chip* chip : {&hide_, &show_, &neither_}) {
            const int w = std::max(m.S(64), m.TextWidth(UiText(chip->label), Font::Small) + m.S(18));
            chip->box = overlay::Box{x, y, x + w, y + chipH};
            x += w + m.S(6);
        }
        y += chipH + gap;
    }
    if (!levels_.empty()) {
        levelsLabel_.box = overlay::Box{left, y, right, y + small};
        y += small + m.S(4);
        for (overlay::Checkbox& box : levels_) {
            box.box = overlay::Box{left, y, right, y + rowH};
            y += rowH;
        }
        y += gap;
    }
    // From the bottom: the status, the undo button; the items fill what is left.
    const int statusH = m.LineHeight(Font::Small) * 2 + m.S(4);
    status_.box = overlay::Box{left, panel_.box.bottom - m.S(12) - statusH, right, panel_.box.bottom - m.S(12)};
    status_.color = status_.text.rfind(UiText("Not saved"), 0) == 0 ? theme.danger : theme.dim;
    int bottom = status_.box.top - gap;
    if (reset_.visible) {
        reset_.box = overlay::Box{left, bottom - buttonHeight, right, bottom};
        bottom = reset_.box.top - gap;
    }
    if (change_.visible) {
        change_.box = overlay::Box{left, bottom - buttonHeight, right, bottom};
        bottom = change_.box.top - gap;
    }
    if (!entries_.empty()) {
        itemsLabel_.box = overlay::Box{left, y, right, y + small};
        y += small + m.S(4);
        items_.box = overlay::Box{left, y, right, std::max(y + rowH, bottom)};
        items_.rowHeight = rowH;
        items_.SetCount(static_cast<int>(entries_.size()));
    }
}

void RuleView::FocusList(std::vector<Widget*>& out) {
    out = {&badge_, &what_, &lines_, &panel_, &note_, &on_, &actionLabel_, &hide_, &show_, &neither_, &levelsLabel_};
    for (overlay::Checkbox& box : levels_) out.push_back(&box);
    for (Widget* w : std::initializer_list<Widget*>{&itemsLabel_, &items_, &reset_, &change_, &delete_, &status_, &back_}) out.push_back(w);
}

void RuleView::Paint(Canvas& canvas) {
    const overlay::Rgb color = badge_.color;
    canvas.FillAlpha(badge_.box, color, 64);
    canvas.Frame(badge_.box, color);
}

void RuleView::HelpRows(const Widget*, std::vector<overlay::HelpRow>& out) const {
    out.push_back({overlay::Glyph::B, "", "Back to the rules"});
}

}  // namespace esrfe::ui
