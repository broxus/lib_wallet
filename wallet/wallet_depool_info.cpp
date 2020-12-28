#include "wallet_depool_info.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/address_label.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/inline_token_icon.h"
#include "base/algorithm.h"
#include "base/unixtime.h"
#include "base/qt_signal_producer.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

#include <QDateTime>

namespace Wallet {

namespace {

[[nodiscard]] const style::TextStyle &AddressStyle() {
	const static auto result = Ui::ComputeAddressStyle(st::defaultTextStyle);
	return result;
}

rpl::producer<QString> ReplaceAmount(rpl::producer<QString>&& text, rpl::producer<int64>&& amount) {
	return rpl::combine(
		std::forward<rpl::producer<QString>>(text),
		std::forward<rpl::producer<int64>>(amount)
	) | rpl::map([](QString &&item, int64 value) {
		return item.replace(
			"{amount}",
			FormatAmount(value, Ton::TokenKind::DefaultToken).full);
	});
}

struct StakeLayout {
	Ui::Text::String id;
	Ui::Text::String amountGrams;
	Ui::Text::String amountNano;
};

StakeLayout PrepareOrdinaryStakeLayout(int64 id, int64 amount) {
	auto result = StakeLayout{};
	result.id.setText(st::walletRowGramsStyle, QString{"#%1: "}.arg(id));

	const auto formattedAmount = FormatAmount(
		amount,
		Ton::TokenKind::DefaultToken,
		FormatFlag::Signed | FormatFlag::Rounded);

	result.amountGrams.setText(st::walletRowGramsStyle, formattedAmount.gramsString);
	result.amountNano.setText(st::walletRowNanoStyle, formattedAmount.separator + formattedAmount.nanoString);

	return result;
}

struct InvestParamsLayout {
	bool first{};
	Ui::Text::String id;

	Ui::Text::String remainingAmountGrams;
	Ui::Text::String remainingAmountNano;

	Ui::Text::String owner;
	int ownerWidth = 0;
	int ownerHeight = 0;

	Ui::Text::String withdrawalValueGrams;
	Ui::Text::String withdrawalValueNano;

	Ui::Text::String withdrawalPeriod;

	int32 lastWithdrawal;
	Ui::Text::String lastWithdrawalTime;
	QDateTime lastWithdrawalTimeRaw;
};

void RefreshTimeTexts(
		InvestParamsLayout &layout) {
	layout.lastWithdrawalTimeRaw = base::unixtime::parse(layout.lastWithdrawal);
	layout.lastWithdrawalTime.setText(
		st::defaultTextStyle,
		layout.lastWithdrawalTimeRaw.toString(Qt::DefaultLocaleLongDate));
}

InvestParamsLayout PrepareInvestParamsLayout(bool first, int64 id, const Ton::InvestParams &investParams) {
	auto result = InvestParamsLayout{};
	result.first = first;
	result.id.setText(st::walletRowGramsStyle, QString{"#%1: "}.arg(id));

	const auto remainingAmount = FormatAmount(
		investParams.remainingAmount,
		Ton::TokenKind::DefaultToken,
		FormatFlag::Signed | FormatFlag::Rounded);
	result.remainingAmountGrams.setText(st::walletRowGramsStyle, remainingAmount.gramsString);
	result.remainingAmountNano.setText(st::walletRowNanoStyle, remainingAmount.separator + remainingAmount.nanoString);

	const auto addressPartWidth = [&](int from, int length = -1) {
		return AddressStyle().font->width(investParams.owner.mid(from, length));
	};

	result.owner = Ui::Text::String(
		AddressStyle(),
		investParams.owner,
		_defaultOptions,
		st::walletAddressWidthMin);
	result.ownerWidth = (AddressStyle().font->spacew / 2) + std::max(
		addressPartWidth(0, investParams.owner.size() / 2),
		addressPartWidth(investParams.owner.size() / 2));
	result.ownerHeight = AddressStyle().font->height * 2;

	result.withdrawalPeriod.setText(
		st::defaultTextStyle,
		QString{"%1 sec"}.arg(investParams.withdrawalPeriod));

	const auto withdrawalValue = FormatAmount(
		investParams.withdrawalValue,
		Ton::TokenKind::DefaultToken,
		FormatFlag::Signed | FormatFlag::Rounded);
	result.withdrawalValueGrams.setText(st::walletRowGramsStyle, withdrawalValue.gramsString);
	result.withdrawalValueNano.setText(st::walletRowNanoStyle, withdrawalValue.separator + withdrawalValue.nanoString);

	result.lastWithdrawal = investParams.lastWithdrawalTime;
	RefreshTimeTexts(result);

	return result;
}

} // namespace

class StakeRow final {
public:
	explicit StakeRow(int64 id, int64 amount)
		: _layout{PrepareOrdinaryStakeLayout(id, amount)} {
	}

	void setTop(int top) { _top = top; }
	[[nodiscard]] auto top() const { return _top; }
	[[nodiscard]] auto bottom() const { return _top + _height; }
	[[nodiscard]] auto height() const { return _height; }

	void update(int64 id, int64 amount) {
		_layout = PrepareOrdinaryStakeLayout(id, amount);
	}

	void resizeToWidth(int width) {
		if (_width == width) {
			return;
		}
		_width = width;
		const auto padding = st::walletRowPadding;
		_height = padding.top() + _layout.amountGrams.minHeight() + padding.bottom();
	}

	void paint(Painter &p, int x, int y) const {
		const auto padding = st::walletRowPadding;
		const auto avail = _width - padding.left() - padding.right();
		x += padding.left();
		y += padding.top();

		p.setPen(st::boxTextFg);
		_layout.id.draw(p, x, y, avail);

		const auto amountOffset = std::max(st::dePoolInfoIdOffset, _layout.id.maxWidth());
		x += amountOffset;

		p.setPen(st::boxTextFgGood);
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
		Ui::PaintInlineTokenIcon(Ton::TokenKind::DefaultToken, p, diamondLeft, diamondTop, st::normalFont);
	}

private:
	int _top{};
	int _width{};
	int _height{};
	StakeLayout _layout;
};

class InvestParamsRow final {
public:
	explicit InvestParamsRow(bool first, int64 id, const Ton::InvestParams &investParams)
		: _layout{PrepareInvestParamsLayout(first, id, investParams)} {
	}

	void setTop(int top) { _top = top; }
	[[nodiscard]] auto top() const { return _top; }
	[[nodiscard]] auto bottom() const { return _top + _height; }
	[[nodiscard]] auto height() const { return _height; }

	void update(bool first, int64 id, const Ton::InvestParams &investParams) {
		_layout = PrepareInvestParamsLayout(first, id, investParams);
	}

	void resizeToWidth(int width) {
		if (_width == width) {
			return;
		}
		_width = width;
		const auto padding = st::walletRowPadding;
		_height = padding.top() + _layout.remainingAmountGrams.minHeight() + padding.bottom();
	}

	void paint(Painter &p, int x, int y) const {
		const auto padding = st::walletRowPadding;
		const auto avail = _width - padding.left() - padding.right();
		x += padding.left();

		if (!_layout.first) {
			p.fillRect(x, y, avail, st::lineWidth, st::shadowFg);
		}
		y += padding.top();

		_layout.id.draw(p, x, y, avail);

		p.setPen(st::boxTextFg);

		const auto amountOffset = std::max(st::dePoolInfoIdOffset, _layout.id.maxWidth());
		x += amountOffset;

		p.setPen(st::boxTextFgGood);
		_layout.remainingAmountGrams.draw(p, x, y, avail);

		const auto nanoTop = y
							 + st::walletRowGramsStyle.font->ascent
							 - st::walletRowNanoStyle.font->ascent;
		const auto nanoLeft = x + _layout.remainingAmountGrams.maxWidth();
		_layout.remainingAmountNano.draw(p, nanoLeft, nanoTop, avail);

		const auto diamondTop = y
								+ st::walletRowGramsStyle.font->ascent
								- st::normalFont->ascent;
		const auto diamondLeft = nanoLeft
								 + _layout.remainingAmountNano.maxWidth()
								 + st::normalFont->spacew;
		Ui::PaintInlineTokenIcon(Ton::TokenKind::DefaultToken, p, diamondLeft, diamondTop, st::normalFont);
	}

private:
	int _top{};
	int _width{};
	int _height{};
	InvestParamsLayout _layout;
};

DePoolInfo::DePoolInfo(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<DePoolInfoState> state)
: _widget(parent) {
	setupControls(std::move(state));
}

DePoolInfo::~DePoolInfo() = default;

void DePoolInfo::setGeometry(QRect geometry) {
	_widget.setGeometry(geometry);
}

auto DePoolInfo::geometry() const -> const QRect & {
	return _widget.geometry();
}

auto DePoolInfo::heightValue() -> rpl::producer<int> {
	return _height.value();
}

void DePoolInfo::setVisible(bool visible) {
	_widget.setVisible(visible);
}

rpl::lifetime &DePoolInfo::lifetime() {
	return _widget.lifetime();
}

not_null<Ui::VerticalLayout*> EmptySection(
		not_null<Ui::RpWidget*> parent,
		Ui::VerticalLayout* layout) {
	const auto wrapper = layout->add(
		object_ptr<Ui::VerticalLayout>(parent.get()));

	wrapper->add(
		object_ptr<Ui::FlatLabel>(
			parent.get(),
			ph::lng_wallet_depool_info_empty(),
			st::dePoolInfoEmpty),
		st::dePoolInfoEmptyPadding);

	wrapper->adjustSize();

	return wrapper;
}

void DePoolInfo::setupControls(rpl::producer<DePoolInfoState> &&state) {
	const auto layout = lifetime().make_state<Ui::VerticalLayout>(&_widget);

	const auto resizeToWidth = [this](Ui::RpWidget* widget, int width) {
		int y = 0;
		for (auto &row : _stakeRows) {
			row->resizeToWidth(width);
			row->setTop(y);
			y += row->height();
		}
		widget->setGeometry(0, widget->geometry().top(), width, y);
	};

	layout->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Ignored);

	const auto stakesTitle = layout->add(
		object_ptr<Ui::FlatLabel>(
			&_widget,
			ph::lng_wallet_depool_info_stakes_title(),
			st::dePoolInfoTitle),
		st::dePoolInfoTitlePadding);

	const auto stakesWrapper = layout->add(
		object_ptr<Ui::RpWidget>(&_widget));
	stakesWrapper->show();

	const auto stakesEmptyLabel = EmptySection(&_widget, layout);

	const auto vestingsTitle = layout->add(
		object_ptr<Ui::FlatLabel>(
			&_widget,
			ph::lng_wallet_depool_info_vestings_title(),
			st::dePoolInfoTitle),
		st::dePoolInfoTitlePadding);

	EmptySection(&_widget, layout);

	const auto locksTitle = layout->add(
		object_ptr<Ui::FlatLabel>(
			&_widget,
			ph::lng_wallet_depool_info_locks_title(),
			st::dePoolInfoTitle),
		st::dePoolInfoTitlePadding);

	EmptySection(&_widget, layout);
	EmptySection(&_widget, layout);
	EmptySection(&_widget, layout);
	EmptySection(&_widget, layout);
	EmptySection(&_widget, layout);

	_widget.sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto padding = st::walletRowPadding;
		const auto use = std::min(size.width(), st::walletRowWidthMax);
		const auto x = (size.width() - use) / 2;

		layout->setGeometry(x, padding.top(), use, layout->height());
		resizeToWidth(stakesWrapper, use);

		stakesEmptyLabel->setMaximumHeight(
			stakesWrapper->geometry().height() == 0
				? QWIDGETSIZE_MAX
				: 0);
		layout->adjustSize();

		_height = layout->height();
		_widget.update();
	}, lifetime());

	stakesWrapper->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = Painter(stakesWrapper);

		const auto from = ranges::upper_bound(
			_stakeRows,
			clip.top(),
			ranges::less(),
			&StakeRow::bottom);
		const auto till = ranges::lower_bound(
			_stakeRows,
			clip.top() + clip.height() + 1,
			ranges::less(),
			&StakeRow::bottom);
		if (from == till || from == _stakeRows.end()) {
			return;
		}
		for (const auto &row : ranges::make_subrange(from, till)) {
			row->paint(p, 0, row->top());
		}
	}, lifetime());

	rpl::duplicate(
		state
	) | rpl::start_with_next([=](const DePoolInfoState &state) {
		size_t stakeCount = 0;
		for (const auto& [id, amount] : state.participantState.stakes) {
			if (stakeCount + 1 > _stakeRows.size()) {
				auto row = std::make_unique<StakeRow>(id, amount);
				_stakeRows.emplace_back(std::move(row));
			} else {
				_stakeRows[stakeCount]->update(id, amount);
			}
			++stakeCount;
		}
		for (auto i = stakeCount; i < _stakeRows.size(); ++i) {
			_stakeRows.pop_back();
		}

		layout->adjustSize();
		_widget.update();
	}, lifetime());
}

rpl::producer<DePoolInfoState> MakeDePoolInfoState(
		rpl::producer<Ton::WalletViewerState> state,
		rpl::producer<QString> selectedDePool) {
	return rpl::combine(
		std::move(state),
		std::move(selectedDePool)
	) | rpl::map([](const Ton::WalletViewerState &state, const QString &address) {
		const auto it = state.wallet.dePoolParticipantStates.find(address);
		if (it != state.wallet.dePoolParticipantStates.end()) {
			return DePoolInfoState {
				.address = address,
				.participantState = it->second
			};
		} else {
			return DePoolInfoState {
				.address = address,
			};
		}
	});
}

} // namespace Wallet
