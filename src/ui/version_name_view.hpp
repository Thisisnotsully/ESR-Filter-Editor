// The first change to an author's filter: the player names the version it makes ("Sully's QoL Filter (my
// version)", "Rune farming", ...), or puts the change into a version of that filter they already have.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "browse_view.hpp"

namespace esrfe::ui {

class VersionNameView final : public overlay::View {
public:
    // `make`: the session is ready to save (named, or opened on the version chosen); `cancelled`: nothing made.
    VersionNameView(std::shared_ptr<Loaded> loaded, std::function<void(Ui&)> make, std::function<void(Ui&)> cancelled);

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    auto InitialFocus() -> Widget* override { return &name_; }

    // For tests.
    auto Name() -> overlay::TextBox& { return name_; }
    auto MakeButton() -> overlay::Button& { return make_; }
    auto CancelButton() -> overlay::Button& { return cancel_; }
    [[nodiscard]] auto VersionCount() const -> std::size_t { return versions_.size(); }
    auto VersionButton(std::size_t i) -> overlay::Button& { return versionButtons_[i]; }
    [[nodiscard]] auto Problem() const -> const std::string& { return problem_.text; }

private:
    void Make(Ui& ui);
    void UseVersion(std::size_t index, Ui& ui);

    std::shared_ptr<Loaded> loaded_;
    std::function<void(Ui&)> make_fn_;
    std::function<void(Ui&)> cancelled_;
    std::vector<edit::Version> versions_;

    overlay::Label about_;
    overlay::Label nameLabel_;
    overlay::TextBox name_;
    overlay::Label problem_;
    overlay::Label orLabel_;
    std::vector<overlay::Button> versionButtons_;
    overlay::Button cancel_;
    overlay::Button make_;
};

}  // namespace esrfe::ui
