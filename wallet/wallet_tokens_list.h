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

struct TokensListState {
	std::map<Ton::TokenKind, TokenItem> tokens;
};

class TokensListRow;

class TokensList final {
public:
	TokensList(not_null<Ui::RpWidget*> parent, rpl::producer<TokensListState> state);
	~TokensList();

	void setGeometry(QRect geometry);

	[[nodiscard]] rpl::producer<TokenItem> openRequests() const;
	[[nodiscard]] rpl::producer<> gateOpenRequets() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void setupContent(rpl::producer<TokensListState> &&state);
	void refreshItemValues(std::map<Ton::TokenKind, TokenItem> &data);
	bool mergeListChanged(std::map<Ton::TokenKind, TokenItem> &&data);

	[[nodiscard]] std::unique_ptr<TokensListRow> makeRow(const TokenItem &data);

	Ui::RpWidget _widget;

	std::vector<TokenItem> _listData{};

	std::vector<std::unique_ptr<TokensListRow>> _rows;
	std::vector<std::unique_ptr<Ui::RoundButton>> _buttons;
	rpl::variable<int> _height;

	rpl::event_stream<TokenItem> _openRequests;
	rpl::event_stream<> _gateOpenRequests;
};

[[nodiscard]] rpl::producer<TokensListState> MakeTokensListState(
	rpl::producer<Ton::WalletViewerState> state);

} // namespace Wallet
