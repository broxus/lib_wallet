#include "wallet_tokens_list.h"

#include "wallet/wallet_common.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/address_label.h"
#include "ui/image/image_prepare.h"
#include "ton/ton_state.h"
#include "styles/style_wallet.h"
#include "styles/palette.h"
#include "ui/layers/generic_box.h"
#include "wallet_phrases.h"

#include <QtWidgets/qlayout.h>
#include <iostream>

namespace Wallet
{

namespace
{
struct TokenItemLayout
{
	QImage image;
	Ui::Text::String title;
	Ui::Text::String balanceGrams;
	Ui::Text::String balanceNano;
	Ui::Text::String address;
	int addressWidth = 0;
};

[[nodiscard]] const style::TextStyle &AddressStyle() {
	const static auto result = Ui::ComputeAddressStyle(st::defaultTextStyle);
	return result;
}

[[nodiscard]] TokenItemLayout PrepareLayout(const TokenItem &data) {
	const auto balance = FormatAmount(static_cast<int64_t>(data.balance), data.token);
	const auto address = data.address;
	const auto addressPartWidth = [&](int from, int length = -1)
	{
		return AddressStyle().font->width(address.mid(from, length));
	};

	auto result = TokenItemLayout();
	result.image = Ui::InlineTokenIcon(data.token, st::walletTokensListRowIconSize);
	result.title.setText(st::walletTokensListRowTitleStyle.style, Ton::toString(data.token));
	result.balanceGrams.setText(st::walletTokensListRowGramsStyle, balance.gramsString);
	result.balanceNano.setText(st::walletTokensListRowNanoStyle, balance.separator + balance.nanoString);
	result.address = Ui::Text::String(AddressStyle(), address, _defaultOptions, st::walletAddressWidthMin);
	result.addressWidth = (AddressStyle().font->spacew / 2) + std::max(
		addressPartWidth(0, address.size() / 2),
		addressPartWidth(address.size() / 2));

	return result;
}

} // namespace

class TokensListRow final
{
public:
	explicit TokensListRow(const TokenItem &token);
	TokensListRow(const TokensListRow &) = delete;
	TokensListRow &operator=(const TokensListRow &) = delete;
	~TokensListRow() = default;

	Ton::TokenKind kind() const;

	void paint(Painter &p, int x, int y) const;

	bool refresh(const TokenItem &token);
	void resizeToWidth(int width);

private:
	TokenItem _tokenItem;
	TokenItemLayout _layout;
	int _width = 0;
	int _height = 0;
};

TokensListRow::TokensListRow(const TokenItem &token)
	: _tokenItem(token)
	, _layout(PrepareLayout(token)) {
}

TokensList::~TokensList() = default;

void TokensListRow::paint(Painter &p, int /*x*/, int /*y*/) const {
	const auto padding = st::walletTokensListRowContentPadding;

	const auto availableWidth = _width - padding.left() - padding.right();
	const auto availableHeight = _height - padding.top() - padding.bottom();

	// draw icon
	const auto iconTop = padding.top() * 2;
	const auto iconLeft = iconTop;

	{
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgRipple);
		p.drawRoundedRect(
			QRect(iconLeft, iconTop, st::walletTokensListRowIconSize, st::walletTokensListRowIconSize),
			st::roundRadiusLarge,
			st::roundRadiusLarge);
	}
	p.drawImage(iconLeft, iconTop, _layout.image);

	// draw token name
	p.setPen(st::walletTokensListRowTitleStyle.textFg);
	const auto titleTop = iconTop
		+ st::walletTokensListRowIconSize;
	const auto titleLeft = iconLeft
		+ (st::walletTokensListRowIconSize - _layout.title.maxWidth()) / 2;
	_layout.title.draw(p, titleLeft, titleTop, availableWidth);

	// draw balance
	p.setPen(st::walletTokensListRow.textFg);

	const auto nanoTop = padding.top()
		+ st::walletTokensListRowGramsStyle.font->ascent
		- st::walletTokensListRowNanoStyle.font->ascent;
	const auto nanoLeft = availableWidth - _layout.balanceNano.maxWidth();
	_layout.balanceNano.draw(p, nanoLeft, nanoTop, availableWidth);

	const auto gramTop = padding.top();
	const auto gramLeft = availableWidth
		- _layout.balanceNano.maxWidth()
		- _layout.balanceGrams.maxWidth();
	_layout.balanceGrams.draw(p, gramLeft, gramTop, availableWidth);

	// draw address
	p.setPen(st::walletTokensListRowTitleStyle.textFg);

	const auto addressTop = availableHeight
		- padding.bottom()
		- AddressStyle().font->ascent * 2;
	const auto addressLeft = availableWidth
		- _layout.addressWidth;
	_layout.address.draw(p, addressLeft, addressTop, _layout.addressWidth, style::al_bottomright);
}

bool TokensListRow::refresh(const TokenItem &item) {
	if (_tokenItem.token != item.token || _tokenItem.balance == item.balance) {
		return false;
	}
	_layout = PrepareLayout(item);
	_tokenItem = item;
	return true;
}

void TokensListRow::resizeToWidth(int width) {
	if (_width == width) {
		return;
	}

	_width = width;
	_height = st::walletTokensListRowHeight;
	// TODO: handle contents resize
}

Ton::TokenKind TokensListRow::kind() const {
	return _tokenItem.token;
}

TokensList::TokensList(not_null<Ui::RpWidget *> parent, rpl::producer<TokensListState> state)
	: _widget(parent) {
	setupContent(std::move(state));
}

void TokensList::setGeometry(QRect geometry) {
	_widget.setGeometry(geometry);
}

rpl::producer<TokenItem> TokensList::openRequests() const {
	return _openRequests.events();
}

rpl::producer<> TokensList::gateOpenRequets() const {
	return _gateOpenRequests.events();
}

rpl::producer<int> TokensList::heightValue() const {
	return _height.value();
}

rpl::lifetime &TokensList::lifetime() {
	return _widget.lifetime();
}

void TokensList::setupContent(rpl::producer<TokensListState> &&state) {
	_widget.paintRequest(
	) | rpl::start_with_next(
		[=](QRect clip)
		{
			Painter(&_widget).fillRect(clip, st::walletTopBg);
		}, lifetime());

	// title
	const auto titleLabel = Ui::CreateChild<Ui::FlatLabel>(
		&_widget, ph::lng_wallet_tokens_list_accounts(),
		st::walletTokensListTitle);
	titleLabel->show();

	// content
	const auto layoutWidget = Ui::CreateChild<Ui::FixedHeightWidget>(&_widget, 0);
	layoutWidget->setContentsMargins(st::walletTokensListPadding);
	auto *layout = new QVBoxLayout{layoutWidget};
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(st::walletTokensListRowSpacing);

	// open gate button
	const auto gateButton = Ui::CreateChild<Ui::RoundButton>(
		&_widget,
		ph::lng_wallet_tokens_list_swap(),
		st::walletCoverButton);
	gateButton->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);

	gateButton->clicks(
	) | rpl::start_with_next([=]() {
		_gateOpenRequests.fire({});
	}, gateButton->lifetime());

	//
	rpl::combine(
		_widget.sizeValue(),
		layoutWidget->heightValue()
	) | rpl::start_with_next([=](QSize size, int contentHeight) {
		const auto width = std::min(size.width(), st::walletRowWidthMax);
		const auto left = (size.width() - width) / 2;

		const auto topSectionHeight = st::walletTokensListRowsTopOffset;
		const auto bottomSectionHeight = gateButton->height() + 2 * st::walletTokensListGateButtonOffset;

		const auto gateButtonWidth = width / 2;
		const auto gateButtonTop = std::max(
			(topSectionHeight + contentHeight + (bottomSectionHeight - gateButton->height()) / 2),
			(size.height() - (bottomSectionHeight + gateButton->height()) / 2)
		);

		_height = st::walletTokensListRowsTopOffset + contentHeight + bottomSectionHeight;

		titleLabel->move(
			left + st::walletTokensListPadding.left(),
			st::walletTokensListPadding.top());
		layoutWidget->setGeometry(QRect(left, topSectionHeight, width, contentHeight));
		gateButton->setGeometry(QRect(
			(size.width() - gateButtonWidth) / 2,
			gateButtonTop,
			gateButtonWidth,
			gateButton->height()));
	}, lifetime());

	//
	std::move(
		state
	) | rpl::start_with_next(
		[this, layoutWidget, layout](TokensListState &&state) {
			refreshItemValues(state.tokens);
			if (!mergeListChanged(std::move(state.tokens))) {
				return;
			}

			for (size_t i = 0; i < _rows.size(); ++i) {
				if (i < _buttons.size()) {
					continue;
				}

				auto button = std::make_unique<Ui::RoundButton>(
					&_widget,
					rpl::single(QString()),
					st::walletTokensListRow);

				auto* label = Ui::CreateChild<Ui::FixedHeightWidget>(button.get());
				button->sizeValue(
				) | rpl::start_with_next([=](QSize size) {
					label->setGeometry(QRect(0, 0, size.width(), size.height()));
				}, button->lifetime());

				label->paintRequest(
				) | rpl::start_with_next([this, label, i](QRect clip) {
					auto p = Painter(label);
					_rows[i]->resizeToWidth(label->width());
					_rows[i]->paint(p, clip.left(), clip.top());
				}, label->lifetime());

				button->clicks(
				) | rpl::start_with_next([this, i](Qt::MouseButton mouseButton) {
					if (mouseButton != Qt::MouseButton::LeftButton) {
						return;
					}
					_openRequests.fire_copy(_listData[i]);
				}, button->lifetime());

				layout->addWidget(button.get());

				_buttons.emplace_back(std::move(button));
			}

			for (size_t i = _buttons.size(); i > state.tokens.size() + 1; --i) {
				layout->removeWidget(_buttons.back().get());
				_buttons.pop_back();
			}

			layoutWidget->setFixedHeight(
				_buttons.size() * (st::walletTokensListRowHeight + st::walletTokensListRowSpacing)
				- (_buttons.empty() ? 0 : st::walletTokensListRowSpacing)
				+ st::walletTokensListPadding.top()
				+ st::walletTokensListPadding.bottom());
		}, lifetime());
}

void TokensList::refreshItemValues(std::map<Ton::TokenKind, TokenItem> &data) {
	for (size_t i = 0; i < _rows.size(); ++i) {
		const auto it = data.find(_listData[i].token);
		if (it == data.end()) {
			continue;
		}

		if (_rows[i]->refresh(it->second)) {
			_listData[i] = it->second;
		}
	}
}

bool TokensList::mergeListChanged(std::map<Ton::TokenKind, TokenItem> &&data) {
	for (auto & item : _listData) {
		auto it = data.find(item.token);
		if (it != data.end()) {
			data.erase(it);
		}
	}
	if (data.empty()) {
		return false;
	}

	for (auto& [kind, state] : data) {
		_rows.push_back(makeRow(state));
		_listData.push_back(std::move(state));
	}
	return true;
}

std::unique_ptr<TokensListRow> TokensList::makeRow(const TokenItem &data) {
	return std::make_unique<TokensListRow>(data);
}

rpl::producer<TokensListState> MakeTokensListState(
	rpl::producer<Ton::WalletViewerState> state) {
	return std::move(
		state
	) | rpl::map(
		[=](const Ton::WalletViewerState &data)
		{
			const auto &account = data.wallet.account;
			const auto unlockedTonBalance = account.fullBalance - account.lockedBalance;

			TokensListState result{};
			result.tokens.insert({Ton::TokenKind::DefaultToken, TokenItem {
				.token = Ton::TokenKind::DefaultToken,
				.address = data.wallet.address,
				.balance = unlockedTonBalance,
			}});
			for (const auto &[token, state] : data.wallet.tokenStates) {
				result.tokens.insert({token, TokenItem {
					.token = token,
					.address = data.wallet.address,
					.balance = state.fullBalance,
				}});
			}
			return result;
		});
}

bool operator==(const TokenItem &a, const TokenItem &b) {
	return a.token == b.token;
}

bool operator!=(const TokenItem &a, const TokenItem &b) {
	return a.token != b.token;
}

} // namespace Wallet
