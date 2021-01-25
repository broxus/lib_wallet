// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"

#include "wallet_common.h"

namespace Ui {
class IconButton;
class DropdownMenu;
} // namespace Ui

namespace Ton {
struct WalletViewerState;
struct Update;
enum class Currency;
} // namespace Ton

namespace Wallet {

enum class Action;

struct TopBarState {
	QString text;
	bool refreshing = false;
	std::optional<SelectedAsset> selectedAsset = std::nullopt;
};

class TopBar final {
public:
	TopBar(not_null<Ui::RpWidget*> parent, rpl::producer<TopBarState> state);

	[[nodiscard]] rpl::producer<Action> actionRequests() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void setupControls(rpl::producer<TopBarState> &&state);
	void showMenu(not_null<Ui::IconButton*> toggle);

	const not_null<Ui::RpWidget*> _widgetParent;
	Ui::RpWidget _widget;
	rpl::event_stream<Action> _actionRequests;
	base::unique_qptr<Ui::DropdownMenu> _menu;

};

[[nodiscard]] rpl::producer<TopBarState> MakeTopBarState(
	rpl::producer<Ton::WalletViewerState> &&state,
	rpl::producer<Ton::Update> &&updates,
	rpl::producer<std::optional<SelectedAsset>> &&selectedAsset,
	rpl::lifetime &alive);

} // namespace Wallet
