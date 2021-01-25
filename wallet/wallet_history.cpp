// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_history.h"

#include "wallet/wallet_common.h"
#include "wallet/wallet_phrases.h"
#include "base/unixtime.h"
#include "base/flags.h"
#include "ui/address_label.h"
#include "ui/inline_token_icon.h"
#include "ui/painter.h"
#include "ui/text/text.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/animations.h"
#include "styles/style_wallet.h"
#include "styles/palette.h"
#include "ton/ton_wallet.h"

#include <iostream>
#include <QtCore/QDateTime>

namespace Wallet {
namespace {

constexpr auto kPreloadScreens = 3;
constexpr auto kCommentLinesMax = 3;

enum class Flag : uchar {
	Incoming = 0x01,
	Pending = 0x02,
	Encrypted = 0x04,
	Service = 0x08,
	Initialization = 0x10,
	SwapBack = 0x20,
	DePoolReward = 0x40,
	DePoolStake = 0x80,
};
inline constexpr bool is_flag_type(Flag) { return true; };
using Flags = base::flags<Flag>;

struct TransactionLayout {
	TimeId serverTime = 0;
	QDateTime dateTime;
	Ui::Text::String date;
	Ui::Text::String time;
	Ui::Text::String amountGrams;
	Ui::Text::String amountNano;
	Ui::Text::String address;
	Ui::Text::String comment;
	Ui::Text::String fees;
	Ton::Symbol token = Ton::Symbol::DefaultToken;
	int addressWidth = 0;
	int addressHeight = 0;
	Flags flags = Flags();
};

[[nodiscard]] const style::TextStyle &addressStyle() {
	const static auto result = Ui::ComputeAddressStyle(st::defaultTextStyle);
	return result;
}

void refreshTimeTexts(
		TransactionLayout &layout,
		bool forceDateText = false) {
	layout.dateTime = base::unixtime::parse(layout.serverTime);
	layout.time.setText(
		st::defaultTextStyle,
		ph::lng_wallet_short_time(layout.dateTime.time())(ph::now));
	if (layout.date.isEmpty() && !forceDateText) {
		return;
	}
	if (layout.flags & Flag::Pending) {
		layout.date.setText(
			st::semiboldTextStyle,
			ph::lng_wallet_row_pending_date(ph::now));
	} else {
		layout.date.setText(
			st::semiboldTextStyle,
			ph::lng_wallet_short_date(layout.dateTime.date())(ph::now));
	}
}

[[nodiscard]] TransactionLayout prepareLayout(
		const Ton::Transaction &data,
		const Fn<void()> &decrypt,
		bool isInitTransaction) {
	const auto service = IsServiceTransaction(data);
	const auto encrypted = IsEncryptedMessage(data) && decrypt;
	const auto amount = FormatAmount(
		service ? (-data.fee) : CalculateValue(data),
		Ton::Symbol::DefaultToken,
		FormatFlag::Signed | FormatFlag::Rounded);
	const auto incoming = !data.incoming.source.isEmpty();
	const auto pending = (data.id.lt == 0);
	const auto address = ExtractAddress(data);
	const auto addressPartWidth = [&](int from, int length = -1) {
		return addressStyle().font->width(address.mid(from, length));
	};

	auto result = TransactionLayout();
	result.serverTime = data.time;
	result.amountGrams.setText(st::walletRowGramsStyle, amount.gramsString);
	result.amountNano.setText(
		st::walletRowNanoStyle,
		amount.separator + amount.nanoString);
	result.address = Ui::Text::String(
		addressStyle(),
		service ? QString() : address,
		_defaultOptions,
		st::walletAddressWidthMin);
	result.addressWidth = (addressStyle().font->spacew / 2) + std::max(
		addressPartWidth(0, address.size() / 2),
		addressPartWidth(address.size() / 2));
	result.addressHeight = addressStyle().font->height * 2;
	result.comment = Ui::Text::String(st::walletAddressWidthMin);
	result.comment.setText(
		st::defaultTextStyle,
		(encrypted ? QString() : ExtractMessage(data)),
		_textPlainOptions);
	if (data.fee) {
		const auto fee = FormatAmount(data.fee, Ton::Symbol::DefaultToken).full;
		result.fees.setText(
			st::defaultTextStyle,
			ph::lng_wallet_row_fees(ph::now).replace("{amount}", fee));
	}
	result.flags = Flag(0)
		| (service ? Flag::Service : Flag(0))
		| (isInitTransaction ? Flag::Initialization : Flag(0))
		| (encrypted ? Flag::Encrypted : Flag(0))
		| (incoming ? Flag::Incoming : Flag(0))
		| (pending ? Flag::Pending : Flag(0));

	refreshTimeTexts(result);
	return result;
}

[[nodiscard]] TransactionLayout prepareLayout(
		const Ton::Transaction &transaction,
		const Ton::TokenTransaction &tokenTransaction) {
	const auto [token, address, value, flags] = v::match(tokenTransaction, [](const Ton::TokenTransfer &transfer) {
		return std::make_tuple(transfer.token, transfer.dest, transfer.value, Flag(0));
	}, [](const Ton::TokenSwapBack &tokenSwapBack) {
		return std::make_tuple(tokenSwapBack.token, tokenSwapBack.dest, tokenSwapBack.value, Flag::SwapBack);
	});

	const auto amount = FormatAmount(-value, token, FormatFlag::Signed | FormatFlag::Rounded);
	const auto addressPartWidth = [address = std::ref(address)](int from, int length = -1) {
		return addressStyle().font->width(address.get().mid(from, length));
	};

	auto result = TransactionLayout();
	result.serverTime = transaction.time;
	result.amountGrams.setText(st::walletRowGramsStyle, amount.gramsString);
	result.amountNano.setText(
		st::walletRowNanoStyle,
		amount.separator + amount.nanoString);
	result.address = Ui::Text::String(
		addressStyle(),
		address,
		_defaultOptions,
		st::walletAddressWidthMin);
	result.addressWidth = (addressStyle().font->spacew / 2) + std::max(
		addressPartWidth(0, address.size() / 2),
		addressPartWidth(address.size() / 2));
	result.addressHeight = addressStyle().font->height * 2;
	result.comment = Ui::Text::String(st::walletAddressWidthMin);
	result.comment.setText(st::defaultTextStyle, {}, _textPlainOptions);

	const auto fee = FormatAmount(-CalculateValue(transaction), Ton::Symbol::DefaultToken).full;
	result.fees.setText(
		st::defaultTextStyle,
		ph::lng_wallet_row_fees(ph::now).replace("{amount}", fee));
	result.token = token;

	result.flags = flags;

	refreshTimeTexts(result);
	return result;
}

[[nodiscard]] TransactionLayout prepareLayout(
	const Ton::Transaction &data,
	const Ton::DePoolTransaction &dePoolTransaction) {
	const auto [value, fee, flags] = v::match(
		dePoolTransaction,
		[&](const Ton::DePoolOrdinaryStakeTransaction &dePoolOrdinaryStakeTransaction) {
			return std::make_tuple(
				-dePoolOrdinaryStakeTransaction.stake,
				-CalculateValue(data) - dePoolOrdinaryStakeTransaction.stake + data.otherFee,
				Flag::DePoolStake);
		},
		[&](const Ton::DePoolOnRoundCompleteTransaction &dePoolOnRoundCompleteTransaction) {
			return std::make_tuple(
				dePoolOnRoundCompleteTransaction.reward,
				data.otherFee,
				Flag::DePoolReward);
		});

	const auto token = Ton::Symbol::DefaultToken;

	const auto amount = FormatAmount(value, token, FormatFlag::Signed | FormatFlag::Rounded);

	const auto incoming = !data.incoming.source.isEmpty();
	const auto pending = (data.id.lt == 0);
	const auto address = ExtractAddress(data);
	const auto addressPartWidth = [&](int from, int length = -1) {
		return addressStyle().font->width(address.mid(from, length));
	};

	auto result = TransactionLayout();
	result.serverTime = data.time;
	result.amountGrams.setText(st::walletRowGramsStyle, amount.gramsString);
	result.amountNano.setText(
		st::walletRowNanoStyle,
		amount.separator + amount.nanoString);
	result.address = Ui::Text::String(
		addressStyle(),
		address,
		_defaultOptions,
		st::walletAddressWidthMin);
	result.addressWidth = (addressStyle().font->spacew / 2) + std::max(
		addressPartWidth(0, address.size() / 2),
		addressPartWidth(address.size() / 2));
	result.addressHeight = addressStyle().font->height * 2;
	result.comment = Ui::Text::String(st::walletAddressWidthMin);
	result.comment.setText(st::defaultTextStyle, {}, _textPlainOptions);

	result.fees.setText(
		st::defaultTextStyle,
		ph::lng_wallet_row_fees(ph::now).replace("{amount}", FormatAmount(fee, Ton::Symbol::DefaultToken).full));
	result.token = token;

	result.flags = Flag(0)
		| (incoming ? Flag::Incoming : Flag(0))
		| (pending ? Flag::Pending : Flag(0))
		| flags;

	refreshTimeTexts(result);
	return result;
}

} // namespace

class HistoryRow final {
public:
	explicit HistoryRow(
		const Ton::Transaction &transaction,
		Fn<void()> decrypt = nullptr,
		bool isInitTransaction = false);
	HistoryRow(const HistoryRow &) = delete;
	HistoryRow &operator=(const HistoryRow &) = delete;

	[[nodiscard]] Ton::TransactionId id() const;
	[[nodiscard]] const Ton::Transaction &transaction() const;

	void refreshDate();
	[[nodiscard]] QDateTime date() const;
	void setShowDate(bool show, Fn<void()> repaintDate);
	void setDecryptionFailed();
	bool showDate() const;

	[[nodiscard]] int top() const;
	void setTop(int top);

	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] int bottom() const;

	void setVisible(bool visible);
	[[nodiscard]] bool isVisible() const;
	void clearAdditionalData();
	void attachTokenTransaction(std::optional<Ton::TokenTransaction> &&tokenTransaction);
	void attachDePoolTransaction(std::optional<Ton::DePoolTransaction> &&dePoolTransaction);

	void paint(Painter &p, int x, int y);
	void paintDate(Painter &p, int x, int y);
	[[nodiscard]] bool isUnderCursor(QPoint point) const;
	[[nodiscard]] ClickHandlerPtr handlerUnderCursor(QPoint point) const;

private:
	[[nodiscard]] QRect computeInnerRect() const;

	Ton::Transaction _transaction;
	std::optional<Ton::TokenTransaction> _tokenTransaction;
	std::optional<Ton::DePoolTransaction> _dePoolTransaction;
	Fn<void()> _decrypt;
	bool _isInitTransaction;
	TransactionLayout _layout;
	int _top = 0;
	int _width = 0;
	int _height = 0;
	int _commentHeight = 0;

	Ui::Animations::Simple _dateShadowShown;
	Fn<void()> _repaintDate;
	bool _dateHasShadow = false;
	bool _decryptionFailed = false;

};

HistoryRow::HistoryRow(
	const Ton::Transaction &transaction,
	Fn<void()> decrypt,
	bool isInitTransaction)
: _transaction(transaction)
, _decrypt(decrypt)
, _isInitTransaction(isInitTransaction)
, _layout(prepareLayout(transaction, _decrypt, _isInitTransaction)) {
}

Ton::TransactionId HistoryRow::id() const {
	return _transaction.id;
}

const Ton::Transaction &HistoryRow::transaction() const {
	return _transaction;
}

void HistoryRow::refreshDate() {
	refreshTimeTexts(_layout);
}

QDateTime HistoryRow::date() const {
	return _layout.dateTime;
}

void HistoryRow::setShowDate(bool show, Fn<void()> repaintDate) {
	_width = 0;
	if (!show) {
		_layout.date.clear();
	} else {
		_repaintDate = std::move(repaintDate);
		refreshTimeTexts(_layout, true);
	}
}

void HistoryRow::setDecryptionFailed() {
	_width = 0;
	_decryptionFailed = true;
	_layout.comment.setText(
		st::defaultTextStyle,
		ph::lng_wallet_decrypt_failed(ph::now),
		_textPlainOptions);
}

bool HistoryRow::showDate() const {
	return !_layout.date.isEmpty();
}

int HistoryRow::top() const {
	return _top;
}

void HistoryRow::setTop(int top) {
	_top = top;
}

void HistoryRow::resizeToWidth(int width) {
	if (_width == width) {
		return;
	}
	_width = width;
	if (!isVisible()) {
		return;
	}

	const auto padding = st::walletRowPadding;
	const auto use = std::min(_width, st::walletRowWidthMax);
	const auto avail = use - padding.left() - padding.right();
	_height = 0;
	if (!_layout.date.isEmpty()) {
		_height += st::walletRowDateSkip;
	}
	_height += padding.top() + _layout.amountGrams.minHeight();
	if (!_layout.address.isEmpty()) {
		_height += st::walletRowAddressTop + _layout.addressHeight;
	}
	if (!_layout.comment.isEmpty()) {
		_commentHeight = std::min(
			_layout.comment.countHeight(avail),
			st::defaultTextStyle.font->height * kCommentLinesMax);
		_height += st::walletRowCommentTop + _commentHeight;
	}
	if (!_layout.fees.isEmpty()) {
		_height += st::walletRowFeesTop + _layout.fees.minHeight();
	}
	_height += padding.bottom();
}

int HistoryRow::height() const {
	return _height;
}

int HistoryRow::bottom() const {
	return _top + _height;
}

void HistoryRow::setVisible(bool visible) {
	if (visible) {
		_height = 1;
		resizeToWidth(_width);
	} else {
		_height = 0;
	}
}

bool HistoryRow::isVisible() const {
	return _height > 0;
}

void HistoryRow::clearAdditionalData() {
	_tokenTransaction.reset();
	_dePoolTransaction.reset();
	_layout = prepareLayout(_transaction, _decrypt, _isInitTransaction);
}

void HistoryRow::attachTokenTransaction(std::optional<Ton::TokenTransaction> &&tokenTransaction) {
	if (_tokenTransaction.has_value() == tokenTransaction.has_value()) {
		return;
	}
	_tokenTransaction = std::move(tokenTransaction);
	_dePoolTransaction.reset();
	if (_tokenTransaction.has_value()) {
		_layout = prepareLayout(_transaction, *_tokenTransaction);
	} else {
		_layout = prepareLayout(_transaction, {}, false);
	}
}

void HistoryRow::attachDePoolTransaction(std::optional<Ton::DePoolTransaction> &&dePoolTransaction) {
	if (_dePoolTransaction.has_value() == dePoolTransaction.has_value()) {
		return;
	}
	_dePoolTransaction = std::move(dePoolTransaction);
	_tokenTransaction.reset();
	if (_dePoolTransaction.has_value()) {
		_layout = prepareLayout(_transaction, *_dePoolTransaction);
	} else {
		_layout = prepareLayout(_transaction, {}, false);
	}
}

void HistoryRow::paint(Painter &p, int x, int y) {
	if (!isVisible()) {
		return;
	}

	const auto padding = st::walletRowPadding;
	const auto use = std::min(_width, st::walletRowWidthMax);
	const auto avail = use - padding.left() - padding.right();
	x += (_width - use) / 2 + padding.left();

	if (!_layout.date.isEmpty()) {
		y += st::walletRowDateSkip;
	} else {
		const auto shadowLeft = (use < _width)
			? (x - st::walletRowShadowAdd)
			: x;
		const auto shadowWidth = (use < _width)
			? (avail + 2 * st::walletRowShadowAdd)
			: _width - padding.left() - padding.right();
		p.fillRect(shadowLeft, y, shadowWidth, st::lineWidth, st::shadowFg);
	}
	y += padding.top();

	if (_layout.flags & Flag::Service) {
		const auto labelLeft = x;
		const auto labelTop = y
			+ st::walletRowGramsStyle.font->ascent
			- st::normalFont->ascent;
		p.setPen(st::windowFg);
		p.setFont(st::normalFont);
		p.drawText(
			labelLeft,
			labelTop + st::normalFont->ascent,
			((_layout.flags & Flag::Initialization)
				? ph::lng_wallet_row_init(ph::now)
				: ph::lng_wallet_row_service(ph::now)));
	} else {
		const auto incoming = (_layout.flags & Flag::Incoming);
		const auto swapBack = (_layout.flags & Flag::SwapBack);

		const auto reward = (_layout.flags & Flag::DePoolReward);
		const auto stake = (_layout.flags & Flag::DePoolStake);

		p.setPen(incoming ? st::boxTextFgGood : st::boxTextFgError);
		_layout.amountGrams.draw(p, x, y, avail);

		const auto nanoTop = y
			+ st::walletRowGramsStyle.font->ascent
			- st::walletRowNanoStyle.font->ascent;
		const auto nanoLeft = x + _layout.amountGrams.maxWidth();
		_layout.amountNano.draw(p, nanoLeft, nanoTop, avail);

		const auto diamondTop = y
			+ st::walletRowGramsStyle.font->ascent
			- st::normalFont->ascent;
		const auto diamondLeft = nanoLeft
			+ _layout.amountNano.maxWidth()
			+ st::normalFont->spacew;
		Ui::PaintInlineTokenIcon(_layout.token, p, diamondLeft, diamondTop, st::normalFont);

		const auto labelTop = diamondTop;
		const auto labelLeft = diamondLeft
			+ st::walletDiamondSize
			+ st::normalFont->spacew;
		p.setPen(st::windowFg);
		p.setFont(st::normalFont);
		p.drawText(
			labelLeft,
			labelTop + st::normalFont->ascent,
			(incoming
				? reward
					? ph::lng_wallet_row_reward_from(ph::now)
					: ph::lng_wallet_row_from(ph::now)
				: swapBack
					? ph::lng_wallet_row_swap_back_to(ph::now)
					: stake
						? ph::lng_wallet_row_ordinary_stake_to(ph::now)
						: ph::lng_wallet_row_to(ph::now)));

		const auto timeTop = labelTop;
		const auto timeLeft = x + avail - _layout.time.maxWidth();
		p.setPen(st::windowSubTextFg);
		_layout.time.draw(p, timeLeft, timeTop, avail);
		if (_layout.flags & Flag::Encrypted) {
			const auto iconLeft = x
				+ avail
				- st::walletCommentIconLeft
				- st::walletCommentIcon.width();
			const auto iconTop = labelTop + st::walletCommentIconTop;
			st::walletCommentIcon.paint(p, iconLeft, iconTop, avail);
		}
		if (_layout.flags & Flag::Pending) {
			st::walletRowPending.paint(
				p,
				(timeLeft
					- st::walletRowPendingPosition.x()
					- st::walletRowPending.width()),
				timeTop + st::walletRowPendingPosition.y(),
				avail);
		}
	}
	y += _layout.amountGrams.minHeight();

	if (!_layout.address.isEmpty()) {
		p.setPen(st::windowFg);
		y += st::walletRowAddressTop;
		_layout.address.drawElided(
			p,
			x,
			y,
			_layout.addressWidth,
			2,
			style::al_topleft,
			0,
			-1,
			0,
			true);
		y += _layout.addressHeight;
	}
	if (!_layout.comment.isEmpty()) {
		y += st::walletRowCommentTop;
		if (_decryptionFailed) {
			p.setPen(st::boxTextFgError);
		}
		_layout.comment.drawElided(p, x, y, avail, kCommentLinesMax);
		y += _commentHeight;
	}
	if (!_layout.fees.isEmpty()) {
		p.setPen(st::windowSubTextFg);
		y += st::walletRowFeesTop;
		_layout.fees.draw(p, x, y, avail);
	}
}

void HistoryRow::paintDate(Painter &p, int x, int y) {
	if (!isVisible()) {
		return;
	}

	Expects(!_layout.date.isEmpty());
	Expects(_repaintDate != nullptr);

	const auto hasShadow = (y != top());
	if (_dateHasShadow != hasShadow) {
		_dateHasShadow = hasShadow;
		_dateShadowShown.start(
			_repaintDate,
			hasShadow ? 0. : 1.,
			hasShadow ? 1. : 0.,
			st::widgetFadeDuration);
	}
	const auto line = st::lineWidth;
	const auto noShadowHeight = st::walletRowDateHeight - line;

	if (_dateHasShadow || _dateShadowShown.animating()) {
		p.setOpacity(_dateShadowShown.value(_dateHasShadow ? 1. : 0.));
		p.fillRect(x, y + noShadowHeight, _width, line, st::shadowFg);
	}

	const auto padding = st::walletRowPadding;
	const auto use = std::min(_width, st::walletRowWidthMax);
	x += (_width - use) / 2;

	p.setOpacity(0.9);
	p.fillRect(x, y, use, noShadowHeight, st::windowBg);

	const auto avail = use - padding.left() - padding.right();
	x += padding.left();
	p.setOpacity(1.);
	p.setPen(st::windowFg);
	_layout.date.draw(p, x, y + st::walletRowDateTop, avail);
}

QRect HistoryRow::computeInnerRect() const {
	const auto padding = st::walletRowPadding;
	const auto use = std::min(_width, st::walletRowWidthMax);
	const auto avail = use - padding.left() - padding.right();
	const auto left = (use < _width)
		? ((_width - use) / 2 + padding.left() - st::walletRowShadowAdd)
		: 0;
	const auto width = (use < _width)
		? (avail + 2 * st::walletRowShadowAdd)
		: _width;
	auto y = top();
	if (!_layout.date.isEmpty()) {
		y += st::walletRowDateSkip;
	}
	return QRect(left, y, width, bottom() - y);
}

bool HistoryRow::isUnderCursor(QPoint point) const {
	return isVisible() && computeInnerRect().contains(point);
}

ClickHandlerPtr HistoryRow::handlerUnderCursor(QPoint point) const {
	return nullptr;
}

History::History(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<HistoryState> state,
	rpl::producer<Ton::LoadedSlice> loaded,
	rpl::producer<not_null<std::vector<Ton::Transaction>*>> collectEncrypted,
	rpl::producer<not_null<const std::vector<Ton::Transaction>*>> updateDecrypted,
	rpl::producer<std::optional<SelectedAsset>> selectedAsset)
: _widget(parent)
, _selectedAsset(SelectedToken { .token = Ton::Symbol::Ton }) {
	setupContent(std::move(state), std::move(loaded), std::move(selectedAsset));

	base::unixtime::updates(
	) | rpl::start_with_next([=] {
		for (const auto &row : ranges::views::concat(_pendingRows, _rows)) {
			row->refreshDate();
		}
		refreshShowDates();
		_widget.update();
	}, _widget.lifetime());

	std::move(
		collectEncrypted
	) | rpl::start_with_next([=](
			not_null<std::vector<Ton::Transaction>*> list) {
		auto &&encrypted = ranges::views::all(
			_listData
		) | ranges::views::filter(IsEncryptedMessage);
		list->insert(list->end(), encrypted.begin(), encrypted.end());
	}, _widget.lifetime());

	std::move(
		updateDecrypted
	) | rpl::start_with_next([=](
			not_null<const std::vector<Ton::Transaction>*> list) {
		auto changed = false;
		for (auto i = 0, count = int(_listData.size()); i != count; ++i) {
			if (IsEncryptedMessage(_listData[i])) {
				if (takeDecrypted(i, *list)) {
					changed = true;
				}
			}
		}
		if (changed) {
			refreshShowDates();
			_widget.update();
		}
	}, _widget.lifetime());
}

History::~History() = default;

void History::updateGeometry(QPoint position, int width) {
	_widget.move(position);
	resizeToWidth(width);
}

void History::resizeToWidth(int width) {
	if (!width) {
		return;
	}

	auto top = (_pendingRows.empty() && _rows.empty())
		? 0
		: st::walletRowsSkip;
	int height = 0;
	for (const auto &row : ranges::views::concat(_pendingRows, _rows)) {
		row->setTop(top + height);
		row->resizeToWidth(width);
		height += row->height();
	}
	_widget.resize(width, (height > 0 ? top * 2 : 0) + height);

	checkPreload();
}

rpl::producer<int> History::heightValue() const {
	return _widget.heightValue();
}

void History::setVisible(bool visible) {
	_widget.setVisible(visible);
}

void History::setVisibleTopBottom(int top, int bottom) {
	_visibleTop = top - _widget.y();
	_visibleBottom = bottom - _widget.y();
	if (_visibleBottom <= _visibleTop || !_previousId.lt || _rows.empty()) {
		return;
	}
	checkPreload();
}

rpl::producer<Ton::TransactionId> History::preloadRequests() const {
	return _preloadRequests.events();
}

rpl::producer<Ton::Transaction> History::viewRequests() const {
	return _viewRequests.events();
}

rpl::producer<Ton::Transaction> History::decryptRequests() const {
	return _decryptRequests.events();
}

rpl::lifetime &History::lifetime() {
	return _widget.lifetime();
}

void History::setupContent(
		rpl::producer<HistoryState> &&state,
		rpl::producer<Ton::LoadedSlice> &&loaded,
		rpl::producer<std::optional<SelectedAsset>> &&selectedAsset) {
	std::move(
		state
	) | rpl::start_with_next([=](HistoryState &&state) {
		mergeState(std::move(state));
	}, lifetime());

	std::move(
		loaded
	) | rpl::filter([=](const Ton::LoadedSlice &slice) {
		return (slice.after == _previousId);
	}) | rpl::start_with_next([=](Ton::LoadedSlice &&slice) {
		const auto loadedLast = (_previousId.lt != 0)
			&& (slice.data.previousId.lt == 0);
		_previousId = slice.data.previousId;
		_listData.insert(
			end(_listData),
			slice.data.list.begin(),
			slice.data.list.end());
		if (loadedLast) {
			computeInitTransactionId();
		}
		refreshRows();
	}, lifetime());

	_widget.paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = Painter(&_widget);
		paint(p, clip);
	}, lifetime());

	_widget.setAttribute(Qt::WA_MouseTracking);
	_widget.events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::Leave: selectRow(-1, nullptr); return;
		case QEvent::Enter:
		case QEvent::MouseMove: selectRowByMouse(); return;
		case QEvent::MouseButtonPress: pressRow(); return;
		case QEvent::MouseButtonRelease: releaseRow(); return;
		}
	}, lifetime());

	rpl::combine(
		std::move(selectedAsset)
	) | rpl::start_with_next([=](const std::optional<SelectedAsset> &asset) {
		auto newAsset = asset.value_or(SelectedToken::defaultToken());
		_selectedAsset = std::move(newAsset);
		refreshShowDates();
		_widget.update();
		_widget.repaint();
	}, _widget.lifetime());
}

void History::selectRow(int selected, ClickHandlerPtr handler) {
	Expects(selected >= 0 || !handler);

	if (_selected != selected) {
		const auto was = (_selected >= 0 && _selected < int(_rows.size()))
			? _rows[_selected].get()
			: nullptr;
		if (was) repaintRow(was);
		_selected = selected;
		_widget.setCursor((_selected >= 0)
			? style::cur_pointer
			: style::cur_default);
	}
	if (ClickHandler::getActive() != handler) {
		const auto now = (_selected >= 0 && _selected < int(_rows.size()))
			? _rows[_selected].get()
			: nullptr;
		if (now) repaintRow(now);
		ClickHandler::setActive(handler);
	}
}

void History::selectRowByMouse() {
	const auto point = _widget.mapFromGlobal(QCursor::pos());
	const auto from = ranges::upper_bound(
		_rows,
		point.y(),
		ranges::less(),
		&HistoryRow::bottom);
	const auto till = ranges::lower_bound(
		_rows,
		point.y(),
		ranges::less(),
		&HistoryRow::top);

	if (from != _rows.end() && from != till && (*from)->isUnderCursor(point)) {
		selectRow(from - begin(_rows), (*from)->handlerUnderCursor(point));
	} else {
		selectRow(-1, nullptr);
	}
}

void History::pressRow() {
	_pressed = _selected;
	ClickHandler::pressed();
}

void History::releaseRow() {
	Expects(_selected < int(_rows.size()));

	const auto handler = ClickHandler::unpressed();
	if (std::exchange(_pressed, -1) != _selected || _selected < 0) {
		if (handler) handler->onClick(ClickContext());
		return;
	}
	if (handler) {
		handler->onClick(ClickContext());
	} else {
		const auto i = ranges::find(
			_listData,
			_rows[_selected]->id(),
			&Ton::Transaction::id);
		Assert(i != end(_listData));
		_viewRequests.fire_copy(*i);
	}
}

void History::decryptById(const Ton::TransactionId &id) {
	const auto i = ranges::find(_listData, id, &Ton::Transaction::id);
	Assert(i != end(_listData));
	_decryptRequests.fire_copy(*i);
}

void History::paint(Painter &p, QRect clip) {
	if (_pendingRows.empty() && _rows.empty()) {
		return;
	}
	const auto paintRows = [&](
			const std::vector<std::unique_ptr<HistoryRow>> &rows) {
		const auto from = ranges::upper_bound(
			rows,
			clip.top(),
			ranges::less(),
			&HistoryRow::bottom);
		const auto till = ranges::lower_bound(
			rows,
			clip.top() + clip.height(),
			ranges::less(),
			&HistoryRow::top);
		if (from == till || from == rows.end()) {
			return;
		}
		for (const auto &row : ranges::make_subrange(from, till)) {
			row->paint(p, 0, row->top());
		}
		auto lastDateTop = rows.back()->bottom();
		const auto dates = ranges::make_subrange(begin(rows), till);
		for (const auto &row : dates | ranges::views::reverse) {
			if (!row->showDate()) {
				continue;
			}
			const auto top = std::max(
				std::min(_visibleTop, lastDateTop - st::walletRowDateHeight),
				row->top());
			row->paintDate(p, 0, top);
			if (row->top() <= _visibleTop) {
				break;
			}
			lastDateTop = top;
		}
	};
	paintRows(_pendingRows);
	paintRows(_rows);
}

void History::mergeState(HistoryState &&state) {
	if (mergePendingChanged(std::move(state.pendingTransactions))) {
		refreshPending();
	}
	if (mergeListChanged(std::move(state.lastTransactions))) {
		refreshRows();
	} else if (_tokenContractAddress.current() != state.tokenContractAddress) {
		_tokenContractAddress = state.tokenContractAddress;
		refreshShowDates();
		_widget.update();
	}
}

bool History::mergePendingChanged(
		std::vector<Ton::PendingTransaction> &&list) {
	if (_pendingData == list) {
		return false;
	}
	_pendingData = std::move(list);
	return true;
}

bool History::mergeListChanged(Ton::TransactionsSlice &&data) {
	const auto i = _listData.empty()
		? data.list.cend()
		: ranges::find(std::as_const(data.list), _listData.front());
	if (i == data.list.cend()) {
		_listData = data.list | ranges::to_vector;
		_previousId = std::move(data.previousId);
		if (!_previousId.lt) {
			computeInitTransactionId();
		}
		return true;
	} else if (i != data.list.cbegin()) {
		_listData.insert(begin(_listData), data.list.cbegin(), i);
		return true;
	}
	return false;
}

void History::setRowShowDate(
		const std::unique_ptr<HistoryRow> &row,
		bool show) {
	const auto raw = row.get();
	row->setShowDate(show, [=] { repaintShadow(raw); });
}

bool History::takeDecrypted(
		int index,
		const std::vector<Ton::Transaction> &decrypted) {
	Expects(index >= 0 && index < _listData.size());
	Expects(index >= 0 && index < _rows.size());
	Expects(_rows[index]->id() == _listData[index].id);

	const auto i = ranges::find(
		decrypted,
		_listData[index].id,
		&Ton::Transaction::id);
	if (i == end(decrypted)) {
		return false;
	}
	if (IsEncryptedMessage(*i)) {
		_rows[index]->setDecryptionFailed();
	} else {
		_listData[index] = *i;
		_rows[index] = makeRow(*i);
	}
	return true;
}

std::unique_ptr<HistoryRow> History::makeRow(const Ton::Transaction &data) {
	const auto id = data.id;
	if (id.lt == 0) {
		// pending
		return std::make_unique<HistoryRow>(data);
	}

	const auto isInitTransaction = (_initTransactionId == id);
	return std::make_unique<HistoryRow>(
		data,
		[=] { decryptById(id); },
		isInitTransaction);
}

void History::computeInitTransactionId() {
	const auto was = _initTransactionId;
	auto found = static_cast<Ton::Transaction*>(nullptr);
	for (auto &row : ranges::views::reverse(_listData)) {
		if (IsServiceTransaction(row)) {
			found = &row;
			break;
		} else if (row.incoming.source.isEmpty()) {
			break;
		}
	}
	const auto now = found ? found->id : Ton::TransactionId();
	if (was == now) {
		return;
	}

	_initTransactionId = now;
	const auto wasItem = ranges::find(_listData, was, &Ton::Transaction::id);
	if (wasItem != end(_listData)) {
		wasItem->initializing = false;
		const auto wasRow = ranges::find(_rows, was, &HistoryRow::id);
		if (wasRow != end(_rows)) {
			*wasRow = makeRow(*wasItem);
		}
	}
	if (found) {
		found->initializing = true;
		const auto nowRow = ranges::find(_rows, now, &HistoryRow::id);
		if (nowRow != end(_rows)) {
			*nowRow = makeRow(*found);
		}
	}
}

void History::refreshShowDates() {
	const auto selectedAsset = _selectedAsset.current();

	const auto targetAddress = v::match(selectedAsset, [this](const SelectedToken&) {
		return _tokenContractAddress.current().isEmpty()
			? QString{}
			: Ton::Wallet::ConvertIntoRaw(_tokenContractAddress.current());
	}, [](const SelectedDePool &selectedDePool) {
		return Ton::Wallet::ConvertIntoRaw(selectedDePool.address);
	});

	auto filterTransaction = [selectedAsset, targetAddress](decltype(_rows.front())& row) {
		const auto &transaction = row->transaction();

		v::match(selectedAsset, [&](const SelectedToken &selectedToken) {
			if (selectedToken.token == Ton::Symbol::DefaultToken) {
				row->setVisible(true);
				row->clearAdditionalData();
				return;
			}

			for (const auto& out : transaction.outgoing) {
				if (Ton::Wallet::ConvertIntoRaw(out.destination) != targetAddress) {
					continue;
				}

				auto tokenTransaction = Ton::Wallet::ParseTokenTransaction(out.message);
				if (tokenTransaction.has_value() && !Ton::CheckTokenTransaction(selectedToken.token, *tokenTransaction)) {
					break;
				}

				row->setVisible(true);
				row->attachTokenTransaction(std::move(tokenTransaction));
				return;
			}

			row->setVisible(false);
		}, [&](const SelectedDePool &selectedDePool) {
			const auto incoming = !transaction.incoming.source.isEmpty();

			if (incoming && Ton::Wallet::ConvertIntoRaw(transaction.incoming.source) == targetAddress) {
				auto parsedTransaction = Ton::Wallet::ParseDePoolTransaction(
					transaction.incoming.message, incoming);
				if (parsedTransaction.has_value()) {
					row->attachDePoolTransaction(std::move(parsedTransaction));
					row->setVisible(true);
					return;
				}
			} else if (!incoming) {
				for (const auto &out : transaction.outgoing) {
					if (Ton::Wallet::ConvertIntoRaw(out.destination) != targetAddress) {
						continue;
					}

					auto stakeTransaction = Ton::Wallet::ParseDePoolTransaction(out.message, incoming);
					if (!stakeTransaction.has_value()) {
						break;
					}

					row->attachDePoolTransaction(std::move(stakeTransaction));
					row->setVisible(true);
					return;
				}
			}

			row->setVisible(false);
		});
	};

	auto previous = QDate();
	for (auto &row : _rows) {
		filterTransaction(row);

		const auto current = row->date().date();
		setRowShowDate(row, row->isVisible() && current != previous);
		if (row->isVisible()) {
			previous = current;
		}
	}
	resizeToWidth(_widget.width());
}

void History::refreshPending() {
	_pendingRows = ranges::views::all(
		_pendingData
	) | ranges::views::transform([&](const Ton::PendingTransaction &data) {
		return makeRow(data.fake);
	}) | ranges::to_vector;

	if (!_pendingRows.empty()) {
		const auto& pendingRow = _pendingRows.front();
		if (pendingRow->isVisible()) {
			setRowShowDate(pendingRow);
		}
	}
	resizeToWidth(_widget.width());
}

void History::refreshRows() {
	auto addedFront = std::vector<std::unique_ptr<HistoryRow>>();
	auto addedBack = std::vector<std::unique_ptr<HistoryRow>>();
	for (const auto &element : _listData) {
		if (!_rows.empty() && element.id == _rows.front()->id()) {
			break;
		}
		addedFront.push_back(makeRow(element));
	}
	if (!_rows.empty()) {
		const auto from = ranges::find(
			_listData,
			_rows.back()->id(),
			&Ton::Transaction::id);
		if (from != end(_listData)) {
			addedBack = ranges::make_subrange(
				from + 1,
				end(_listData)
			) | ranges::views::transform([=](const Ton::Transaction &data) {
				return makeRow(data);
			}) | ranges::to_vector;
		}
	}
	if (addedFront.empty() && addedBack.empty()) {
		return;
	} else if (!addedFront.empty()) {
		if (addedFront.size() < _listData.size()) {
			addedFront.insert(
				end(addedFront),
				std::make_move_iterator(begin(_rows)),
				std::make_move_iterator(end(_rows)));
		}
		_rows = std::move(addedFront);
	}
	_rows.insert(
		end(_rows),
		std::make_move_iterator(begin(addedBack)),
		std::make_move_iterator(end(addedBack)));

	refreshShowDates();
}

void History::repaintRow(not_null<HistoryRow*> row) {
	_widget.update(0, row->top(), _widget.width(), row->height());
}

void History::repaintShadow(not_null<HistoryRow*> row) {
	const auto min = std::min(row->top(), _visibleTop);
	const auto delta = std::max(row->top(), _visibleTop) - min;
	_widget.update(0, min, _widget.width(), delta + st::walletRowDateHeight);
}

void History::checkPreload() const {
	const auto visibleHeight = (_visibleBottom - _visibleTop);
	const auto preloadHeight = kPreloadScreens * visibleHeight;
	if (_visibleBottom + preloadHeight >= _widget.height() && _previousId.lt != 0) {
		_preloadRequests.fire_copy(_previousId);
	}
}

rpl::producer<HistoryState> MakeHistoryState(
		rpl::producer<QString> tokenContractAddress,
		rpl::producer<Ton::WalletViewerState> state) {
	return rpl::combine(
		std::move(tokenContractAddress),
		std::move(state)
	) | rpl::map([](QString &&tokenContractAddress, Ton::WalletViewerState &&state) {
		return HistoryState{
			.tokenContractAddress = tokenContractAddress,
			.lastTransactions = std::move(state.wallet.lastTransactions),
			.pendingTransactions = std::move(state.wallet.pendingTransactions)
		};
	});
}

} // namespace Wallet
