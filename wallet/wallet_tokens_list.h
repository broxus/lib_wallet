#pragma once

#include "ui/rp_widget.h"
#include "ui/inline_token_icon.h"
#include "ui/click_handler.h"
#include "ui/widgets/buttons.h"

class Painter;

namespace Ton {
struct WalletViewerState;
} // namespace Ton

namespace Wallet {

struct TokenItem {
	Ton::TokenKind token = Ton::TokenKind::DefaultToken;
	QString address = "";
	int64_t balance = 0;
};

bool operator==(const TokenItem &a, const TokenItem &b);
bool operator!=(const TokenItem &a, const TokenItem &b);

struct DePoolItem {
	QString address = "";
	int64_t withdrawValue = 0;
	int64_t reward = 0;
};

bool operator==(const DePoolItem &a, const DePoolItem &b);
bool operator==(const DePoolItem &a, const DePoolItem &b);

using AssetItem = std::variant<TokenItem, DePoolItem>;

struct AssetsListState {
	std::map<QString, DePoolItem> depools;
	std::map<Ton::TokenKind, TokenItem> tokens;
};

class AssetsListRow;

class AssetsList final {
public:
	AssetsList(not_null<Ui::RpWidget*> parent, rpl::producer<AssetsListState> state);
	~AssetsList();

	void setGeometry(QRect geometry);

	[[nodiscard]] rpl::producer<AssetItem> openRequests() const;
	[[nodiscard]] rpl::producer<> gateOpenRequests() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void setupContent(rpl::producer<AssetsListState> &&state);

	void refreshItemValues(const AssetsListState &data);
	bool mergeListChanged(AssetsListState &&data);

	[[nodiscard]] std::unique_ptr<AssetsListRow> makeTokenItemRow(const TokenItem &data);

	Ui::RpWidget _widget;

	std::vector<AssetItem> _listData{};

	std::vector<std::unique_ptr<AssetsListRow>> _rows;
	std::vector<std::unique_ptr<Ui::RoundButton>> _buttons;
	rpl::variable<int> _height;

	rpl::event_stream<AssetItem> _openRequests;
	rpl::event_stream<> _gateOpenRequests;
};

[[nodiscard]] rpl::producer<AssetsListState> MakeTokensListState(
	rpl::producer<Ton::WalletViewerState> state);

} // namespace Wallet
