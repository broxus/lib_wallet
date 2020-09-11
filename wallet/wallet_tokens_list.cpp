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
	const auto balance = FormatAmount(static_cast<int64_t>(data.balance));
	const auto address = data.address;
	const auto addressPartWidth = [&](int from, int length = -1)
	{
		return AddressStyle().font->width(address.mid(from, length));
	};

	auto result = TokenItemLayout();
	result.image = Ui::InlineTokenIcon(data.icon, st::walletTokensListRowIconSize);
	result.title.setText(st::walletTokensListRowTitleStyle.style, data.name);
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

	[[nodiscard]] QString name() const;

	void paint(Painter &p, int x, int y) const;

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

void TokensListRow::resizeToWidth(int width) {
	if (_width == width) {
		return;
	}

	_width = width;
	_height = st::walletTokensListRowHeight;
	// TODO: handle contents resize
}

QString TokensListRow::name() const {
	return _tokenItem.name;
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

rpl::producer<int> TokensList::heightValue() const {
	return _height.value();
}

rpl::lifetime &TokensList::lifetime() {
	return _widget.lifetime();
}

void TokensList::setupContent(rpl::producer<TokensListState> &&state) {
	// title
	const auto titleLabel = Ui::CreateChild<Ui::FlatLabel>(&_widget, "Wallets", st::walletTokensListTitle);

	_widget.sizeValue() | rpl::start_with_next(
		[=](QSize size)
		{
			const auto width = std::min(st::walletRowWidthMax, size.width());
			const auto left = (size.width() - width) / 2;
			titleLabel->move(
				left + st::walletTokensListPadding.left(),
				st::walletTokensListPadding.top());
		}, _widget.lifetime());

	titleLabel->show();

	_widget.paintRequest(
	) | rpl::start_with_next(
		[=](QRect clip)
		{
			Painter(&_widget).fillRect(clip, st::walletTopBg);
		}, lifetime());

	// content
	const auto layoutWidget = Ui::CreateChild<Ui::FixedHeightWidget>(&_widget, 0);
	layoutWidget->setContentsMargins(st::walletTokensListPadding);
	auto *layout = new QVBoxLayout{layoutWidget};
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(st::walletTokensListRowSpacing);

	_widget.sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto use = std::min(size.width(), st::walletRowWidthMax);
		const auto x = (size.width() - use) / 2;
		layoutWidget->setGeometry(QRect(x, st::walletTokensListRowsTopOffset, use, layoutWidget->height()));
	}, lifetime());

	layoutWidget->heightValue(
	) | rpl::start_with_next([this](int height) {
		_height = st::walletTokensListRowsTopOffset + height;
	}, lifetime());

	std::move(
		state
	) | rpl::start_with_next(
		[this, layoutWidget, layout](TokensListState &&state) {
			if (!mergeListChanged(std::move(state.tokens))) {
				return;
			}

			refreshItems();

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

void TokensList::refreshItems() {
	auto addedFront = std::vector<std::unique_ptr<TokensListRow>>();
	auto addedBack = std::vector<std::unique_ptr<TokensListRow>>();
	for (const auto &item : _listData) {
		if (!_rows.empty() && item.name == _rows.front()->name()) {
			break;
		}
		addedFront.push_back(makeRow(item));
	}
	if (!_rows.empty()) {
		const auto from = ranges::find(
			_listData,
			_rows.back()->name(),
			&TokenItem::name);
		if (from != end(_listData)) {
			addedBack = ranges::make_subrange(
				from + 1,
				end(_listData)
			) | ranges::view::transform(
				[=](const TokenItem &data)
				{
					return makeRow(data);
				}) | ranges::to_vector;
		}
	}
	if (addedFront.empty() && addedBack.empty()) {
		return;
	}
	else if (!addedFront.empty()) {
		if (addedFront.size() < _listData.size()) {
			addedFront.insert(
				end(addedFront),
				std::make_move_iterator(begin(_rows)),
				std::make_move_iterator(end(_rows)));
		}
		_rows = std::move(addedFront);
	}

	if (!addedBack.empty()) {
		_rows.insert(
			end(_rows),
			std::make_move_iterator(begin(addedBack)),
			std::make_move_iterator(end(addedBack)));
	}
}

bool TokensList::mergeListChanged(std::vector<TokenItem> &&data) {
	const auto i = _listData.empty()
		? data.cend()
		: ranges::find(std::as_const(data), _listData.front());
	if (i == data.cend()) {
		_listData = data | ranges::to_vector;
		return true;
	}
	else if (i != data.cbegin()) {
		_listData.insert(begin(_listData), data.cbegin(), i);
		return true;
	}
	return false;
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
			return TokensListState{
				.tokens = {
					TokenItem{
						.icon = Ui::TokenIconKind::Ton,
						.name = "TON",
						.address = "0:A921453472366B7FEEEC15323A96B5DCF17197C88DC0D4578DFA52900B8A33CB",
						.balance = 247'781653888,
					},
					TokenItem{
						.icon = Ui::TokenIconKind::Pepe,
						.name = "PEPE",
						.address = "0:A921453472366B7FEEEC15323A96B5DCF17197C88DC0D4578DFA52900B8A33CB",
						.balance = 9999'123123234,
					},
					TokenItem{
						.icon = Ui::TokenIconKind::Pepe,
						.name = "PEPE",
						.address = "0:A921453472366B7FEEEC15323A96B5DCF17197C88DC0D4578DFA52900B8A33CB",
						.balance = 9999'123123234,
					},
					TokenItem{
						.icon = Ui::TokenIconKind::Pepe,
						.name = "PEPE",
						.address = "0:A921453472366B7FEEEC15323A96B5DCF17197C88DC0D4578DFA52900B8A33CB",
						.balance = 9999'123123234,
					},
					TokenItem{
						.icon = Ui::TokenIconKind::Pepe,
						.name = "PEPE",
						.address = "0:A921453472366B7FEEEC15323A96B5DCF17197C88DC0D4578DFA52900B8A33CB",
						.balance = 9999'123123234,
					},
					TokenItem{
						.icon = Ui::TokenIconKind::Pepe,
						.name = "PEPE",
						.address = "0:A921453472366B7FEEEC15323A96B5DCF17197C88DC0D4578DFA52900B8A33CB",
						.balance = 9999'123123234,
					},
					TokenItem{
						.icon = Ui::TokenIconKind::Pepe,
						.name = "PEPE",
						.address = "0:A921453472366B7FEEEC15323A96B5DCF17197C88DC0D4578DFA52900B8A33CB",
						.balance = 9999'123123234,
					}
				}
			};
		});
}

bool operator==(const TokenItem &a, const TokenItem &b) {
	return a.name == b.name;
}

bool operator!=(const TokenItem &a, const TokenItem &b) {
	return a.name != b.name;
}

} // namespace Wallet
