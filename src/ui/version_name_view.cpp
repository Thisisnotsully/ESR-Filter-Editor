#include "version_name_view.hpp"

#include <algorithm>

#include "ui_text.hpp"

namespace esrfe::ui {

using namespace overlay::text_flags;
using overlay::Font;
using overlay::UiFormat;
using overlay::UiText;

VersionNameView::VersionNameView(std::shared_ptr<Loaded> loaded, std::function<void(Ui&)> make, std::function<void(Ui&)> cancelled)
    : loaded_(std::move(loaded)), make_fn_(std::move(make)), cancelled_(std::move(cancelled)) {
    edit::Session& session = *loaded_->session;
    versions_ = edit::VersionsOf(session.GetFolders(), session.BaseId());
    const std::string title = loaded_->doc.GetSettings().title.empty() ? UiText("this filter") : loaded_->doc.GetSettings().title;

    about_.translate = false;
    about_.font = Font::Body;
    about_.flags = kLeft;
    about_.useThemeColor = false;
    about_.text = UiFormat("Your changes go into your own version of \"{0}\". The game switches to it now and keeps your filter level. "
                           "{1}'s updates still come through: the editor puts your changes on top of each new version.\n\n"
                           "Give it a name. You can have several versions, each with its own changes, and pick one in the Filter list.",
                           {title, loaded_->Author()});
    nameLabel_.text = "Name";
    nameLabel_.font = Font::Small;
    name_.SetValue(edit::SuggestVersionName(session.GetFolders(), title));
    name_.onChange = [this](const std::string&, Ui& ui) {
        problem_.text.clear();
        ui.Repaint();
    };
    name_.onSubmit = [this](const std::string&, Ui& ui) { Make(ui); };
    problem_.translate = false;
    problem_.font = Font::Small;
    problem_.flags = kLeft;
    problem_.useThemeColor = false;
    orLabel_.text = "Or put this change into a version you have:";
    orLabel_.font = Font::Small;
    orLabel_.visible = !versions_.empty();
    versionButtons_.resize(versions_.size());
    for (std::size_t i = 0; i < versions_.size(); ++i) {
        versionButtons_[i].label = versions_[i].title;
        versionButtons_[i].translate = false;
        versionButtons_[i].onClick = [this, i](Ui& ui) { UseVersion(i, ui); };
    }
    cancel_.label = "Cancel";
    cancel_.onClick = [this](Ui& ui) {
        ui.PopView();
        if (cancelled_) cancelled_(ui);
    };
    make_.label = "Make my version";
    make_.style = overlay::Button::Style::Primary;
    make_.onClick = [this](Ui& ui) { Make(ui); };
}

auto VersionNameView::Title() const -> std::string { return UiText("Your own version"); }

void VersionNameView::Make(Ui& ui) {
    std::string error;
    if (!loaded_->session->NameVersion(name_.Value(), error)) {
        problem_.text = UiText(error);
        ui.Repaint();
        return;
    }
    ui.PopView();
    if (make_fn_) make_fn_(ui);
}

void VersionNameView::UseVersion(std::size_t index, Ui& ui) {
    if (index >= versions_.size()) return;
    edit::Session& session = *loaded_->session;
    const edit::Folders folders = session.GetFolders();
    std::string error;
    edit::Session version;
    if (!version.Open(versions_[index].id, edit::PathOfId(versions_[index].id, folders), folders, error)) {
        problem_.text = UiFormat("That version could not be opened: {0}", {error});
        ui.Repaint();
        return;
    }
    session = std::move(version);  // the change goes into it; saving switches the game to it
    ui.PopView();
    if (make_fn_) make_fn_(ui);
}

void VersionNameView::Layout(const LayoutContext& c) {
    overlay::Measurer& m = c.measure;
    const Box& area = c.content;
    const overlay::Theme& theme = m.GetTheme();
    const int gap = m.S(8);
    const int width = std::min(area.Width(), m.S(720));
    const int left = area.left;
    const int right = left + width;
    const int buttonH = std::max(m.S(32), m.LineHeight(Font::Body) + m.S(12));
    const int fieldH = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
    int y = area.top;
    about_.box = Box{left, y, right, y + m.LineHeight(Font::Body) * 6 + m.S(8)};
    about_.color = theme.text;
    y = about_.box.bottom + gap;
    nameLabel_.box = Box{left, y, right, y + m.LineHeight(Font::Small) + m.S(4)};
    y = nameLabel_.box.bottom;
    name_.box = Box{left, y, right, y + fieldH};
    y = name_.box.bottom + m.S(4);
    problem_.box = Box{left, y, right, y + m.LineHeight(Font::Small) + m.S(6)};
    problem_.color = theme.danger;
    y = problem_.box.bottom + gap;
    if (!versions_.empty()) {
        orLabel_.box = Box{left, y, right, y + m.LineHeight(Font::Small) + m.S(6)};
        y = orLabel_.box.bottom;
        for (overlay::Button& b : versionButtons_) {
            b.box = Box{left, y, right, y + buttonH};
            y += buttonH + m.S(4);
        }
    }
    const int makeW = m.TextWidth(UiText(make_.label), Font::Body) + m.S(40);
    make_.box = Box{area.right - makeW, area.bottom - buttonH, area.right, area.bottom};
    cancel_.box = Box{make_.box.left - gap - m.S(120), area.bottom - buttonH, make_.box.left - gap, area.bottom};
}

void VersionNameView::FocusList(std::vector<Widget*>& out) {
    out = {&about_, &nameLabel_, &name_, &problem_, &orLabel_};
    for (overlay::Button& b : versionButtons_) out.push_back(&b);
    out.push_back(&cancel_);
    out.push_back(&make_);
}

}  // namespace esrfe::ui
