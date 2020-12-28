#include "wallet_depool_info.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/inline_token_icon.h"
#include "base/algorithm.h"
#include "base/qt_signal_producer.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

namespace Wallet {

namespace {

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

} // namespace

DePoolInfo::DePoolInfo(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<DePoolInfoState> state)
: _widget(parent) {
	setupControls(std::move(state));
}

void DePoolInfo::setGeometry(QRect geometry) {
	_widget.setGeometry(geometry);
}

auto DePoolInfo::geometry() const -> const QRect & {
	return _widget.geometry();
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

not_null<Ui::VerticalLayout*> OrdinaryStakeItem(
		not_null<Ui::RpWidget*> parent,
		Ui::VerticalLayout* layout,
		int64 id,
		int64 amount) {
	const auto wrapper = layout->add(
		object_ptr<Ui::VerticalLayout>(parent.get()));

	wrapper->add(
		object_ptr<Ui::FlatLabel>(
			parent.get(),
			ReplaceAmount(
				ph::lng_wallet_depool_info_id(),
				rpl::single(id)),
			st::dePoolInfoEmpty),
		st::dePoolInfoEmptyPadding);

	wrapper->adjustSize();

	return wrapper;
}

void DePoolInfo::setupControls(rpl::producer<DePoolInfoState> &&state) {
	const auto layout = _widget.lifetime().make_state<Ui::VerticalLayout>(&_widget);

	_widget.sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto padding = st::walletRowPadding;
		const auto use = std::min(size.width(), st::walletRowWidthMax);
		const auto avail = use - padding.left() - padding.right();
		const auto x = (size.width() - use) / 2 + padding.left();

		layout->setGeometry(x, padding.top(), avail, layout->height());
	}, lifetime());

	layout->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Ignored);

	const auto stakesTitle = layout->add(
		object_ptr<Ui::FlatLabel>(
			&_widget,
			ph::lng_wallet_depool_info_stakes_title(),
			st::dePoolInfoTitle),
		st::dePoolInfoTitlePadding);

	EmptySection(&_widget, layout);

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
