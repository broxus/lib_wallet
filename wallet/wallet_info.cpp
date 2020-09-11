// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_info.h"

#include "wallet/wallet_top_bar.h"
#include "wallet/wallet_common.h"
#include "wallet/wallet_cover.h"
#include "wallet/wallet_empty_history.h"
#include "wallet/wallet_history.h"
#include "wallet/wallet_tokens_list.h"
#include "ui/rp_widget.h"
#include "ui/lottie_widget.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/text/text_utilities.h"
#include "styles/style_wallet.h"
#include <ui/wrap/slide_wrap.h>

#include <QtCore/QDateTime>
#include <iostream>

namespace Wallet {

Info::Info(not_null<QWidget*> parent, Data data)
: _widget(std::make_unique<Ui::RpWidget>(parent))
, _scroll(
	Ui::CreateChild<Ui::ScrollArea>(_widget.get(), st::walletScrollArea))
, _inner(_scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(_scroll.get()))) {
	setupControls(std::move(data));
	_widget->show();
}

Info::~Info() = default;

void Info::setGeometry(QRect geometry) {
	_widget->setGeometry(geometry);
}

rpl::producer<Action> Info::actionRequests() const {
	return _actionRequests.events();
}

rpl::producer<Ton::TransactionId> Info::preloadRequests() const {
	return _preloadRequests.events();
}

rpl::producer<Ton::Transaction> Info::viewRequests() const {
	return _viewRequests.events();
}

rpl::producer<Ton::Transaction> Info::decryptRequests() const {
	return _decryptRequests.events();
}

void Info::setupControls(Data &&data) {
	const auto &state = data.state;
	const auto topBar = _widget->lifetime().make_state<TopBar>(
		_widget.get(),
		MakeTopBarState(
			rpl::duplicate(state),
			rpl::duplicate(data.updates),
			_widget->lifetime()));
	topBar->actionRequests(
	) | rpl::start_to_stream(_actionRequests, topBar->lifetime());

	// ton data stream
	auto loaded = std::move(
		data.loaded
	) | rpl::filter([](const Ton::Result<Ton::LoadedSlice> &value) {
		return value.has_value();
	}) | rpl::map([](Ton::Result<Ton::LoadedSlice> &&value) {
		return std::move(*value);
	});

	const auto selectedToken = _widget->lifetime().make_state<rpl::variable<std::optional<int>>>(std::nullopt);

	std::move(
		data.transitionEvents
	) | rpl::start_with_next([=](InfoTransition transition) {
		switch (transition) {
			case InfoTransition::Back:
				*selectedToken = std::nullopt;
				return;
			default:
				return;
		}
	}, lifetime());

	// create wrappers
	const auto tokensListWrapper = Ui::CreateChild<Ui::FixedHeightWidget>(_inner.get(), _widget->height());
	const auto tonHistoryWrapper = Ui::CreateChild<Ui::FixedHeightWidget>(_inner.get(), _widget->height());

	// create tokens list page
	const auto tokensList = _widget->lifetime().make_state<TokensList>(
		tokensListWrapper,
		MakeTokensListState(rpl::duplicate(state)));

	tokensList->openRequests(
	) | rpl::start_with_next([=](TokenItem token) {
		*selectedToken = 1;
	}, tokensList->lifetime());

	// create ton history page

	// top cover
	const auto cover = _widget->lifetime().make_state<Cover>(
		tonHistoryWrapper,
		MakeCoverState(
			rpl::duplicate(state),
			data.justCreated,
			data.useTestNetwork));

	// register top cover events
	rpl::merge(
		cover->sendRequests() | rpl::map([] { return Action::Send; }),
		cover->receiveRequests() | rpl::map([] { return Action::Receive; })
	) | rpl::start_to_stream(_actionRequests, cover->lifetime());

	// create transactions lists
	const auto history = _widget->lifetime().make_state<History>(
		tonHistoryWrapper,
		MakeHistoryState(rpl::duplicate(state)),
		std::move(loaded),
		std::move(data.collectEncrypted),
		std::move(data.updateDecrypted));

	const auto emptyHistory = _widget->lifetime().make_state<EmptyHistory>(
		tonHistoryWrapper,
		MakeEmptyHistoryState(rpl::duplicate(state), data.justCreated),
		data.share);

	// register layout relations

	// set scroll height to full page
	_widget->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_scroll->setGeometry(QRect(
			QPoint(),
			size
		).marginsRemoved({ 0, st::walletTopBarHeight, 0, 0 }));
	}, _scroll->lifetime());

	// set wrappers size same as scroll height
	rpl::combine(
		_scroll->sizeValue(),
		tokensList->heightValue(),
		history->heightValue(),
		selectedToken->value()
	) | rpl::start_with_next([=](QSize size, int tokensListHeight, int historyHeight, std::optional<int> token) {
		if (token.has_value()) {
			const auto innerHeight = std::max(
				size.height(),
				cover->height() + historyHeight);
			_inner->setGeometry({0, 0, size.width(), innerHeight});

			cover->setGeometry(QRect(0, 0, size.width(), st::walletCoverHeight));
			const auto contentGeometry = QRect(
				0,
				st::walletCoverHeight,
				size.width(),
				size.height() - st::walletCoverHeight);
			emptyHistory->setGeometry(contentGeometry);
			emptyHistory->setVisible(historyHeight == 0);

			tonHistoryWrapper->setGeometry(QRect(0, 0, size.width(), innerHeight));
			history->updateGeometry({0, st::walletCoverHeight}, size.width());
		} else {
			const auto innerHeight = std::max(size.height(), tokensListHeight);
			_inner->setGeometry(QRect(0, 0, size.width(), innerHeight));

			tokensListWrapper->setGeometry(QRect(0, 0, size.width(), innerHeight));
			tokensList->setGeometry(QRect(0, 0, size.width(), innerHeight));
		}
	}, _scroll->lifetime());

	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		selectedToken->value()
	) | rpl::start_with_next([=](int scrollTop, int scrollHeight, std::optional<int> token) {
		if (token.has_value()) {
			history->setVisibleTopBottom(scrollTop, scrollTop + scrollHeight);
		}
	}, history->lifetime());

	history->preloadRequests(
	) | rpl::start_to_stream(_preloadRequests, history->lifetime());

	history->viewRequests(
	) | rpl::start_to_stream(_viewRequests, history->lifetime());

	history->decryptRequests(
	) | rpl::start_to_stream(_decryptRequests, history->lifetime());

	// initialize default layouts
	selectedToken->value(
	) | rpl::start_with_next([=](std::optional<int> token) {
		tokensListWrapper->setVisible(!token.has_value());
		tonHistoryWrapper->setVisible(token.has_value());
	}, lifetime());
}

rpl::lifetime &Info::lifetime() {
	return _widget->lifetime();
}

} // namespace Wallet
