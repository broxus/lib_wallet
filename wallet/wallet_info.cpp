// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_info.h"

#include "wallet/wallet_phrases.h"
#include "ui/rp_widget.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/text/text_utilities.h"
#include "styles/style_wallet.h"

#include <QtCore/QDateTime>

namespace Wallet {
namespace {

constexpr auto kOneGram = 1'000'000'000;
constexpr auto kNanoDigits = 9;

QString AmountToString(int64 amount) {
	amount = std::abs(amount);
	const auto grams = amount / kOneGram;
	auto nanos = amount % kOneGram;
	auto zeros = 0;
	while (zeros + 1 < kNanoDigits && nanos % 10 == 0) {
		nanos /= 10;
		++zeros;
	}
	return QString("%1.%2"
	).arg(grams
	).arg(nanos, kNanoDigits - zeros, 10, QChar('0'));
}

QString DateToString(const QDateTime &timestamp) {
	return QString("%1.%2.%3 %4:%5:%6"
	).arg(timestamp.date().day(), 2, 10, QChar('0')
	).arg(timestamp.date().month(), 2, 10, QChar('0')
	).arg(timestamp.date().year()
	).arg(timestamp.time().hour(), 2, 10, QChar('0')
	).arg(timestamp.time().minute(), 2, 10, QChar('0')
	).arg(timestamp.time().second(), 2, 10, QChar('0'));
}

QString PrintAddress(const Ton::WalletState &state) {
	return state.address;
}

QString PrintData(const Ton::WalletState &state) {
	auto result = "Balance: " + AmountToString(
		std::max(state.account.balance, 0LL));
	for (const auto &transaction : state.lastTransactions.list) {
		result += "\n\n";
		const auto value = transaction.incoming.value
			- ranges::accumulate(
				transaction.outgoing,
				int64(0),
				ranges::plus(),
				&Ton::Message::value);
		if (value) {
			result += (value > 0 ? '+' : '-') + AmountToString(value);
		} else {
			result += AmountToString(0);
		}
		if (value > 0 && !transaction.incoming.source.isEmpty()) {
			result += " from " + transaction.incoming.source;
		} else if (value < 0
			&& !transaction.outgoing.empty()
			&& !transaction.outgoing.front().destination.isEmpty()) {
			result += " to " + transaction.outgoing.front().destination;
		}
		result += "\n" + DateToString(
			QDateTime::fromTime_t(transaction.time));
		if (transaction.fee) {
			result += " (fee: " + AmountToString(transaction.fee) + ")";
		}
		if (!transaction.incoming.message.isEmpty()) {
			result += "\nComment: " + transaction.incoming.message;
		} else if (!transaction.outgoing.empty()
			&& !transaction.outgoing.front().message.isEmpty()) {
			result += "\nComment: " + transaction.outgoing.front().message;
		}
	}
	return result;
}

} // namespace

Info::Info(not_null<QWidget*> parent, Data data)
: _widget(std::make_unique<Ui::RpWidget>(parent))
, _scroll(Ui::CreateChild<Ui::ScrollArea>(_widget.get()))
, _inner(_scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(_scroll.get()))) {
	setupControls(std::move(data));
	_widget->show();
}

void Info::setGeometry(QRect geometry) {
	_widget->setGeometry(geometry);
	_scroll->setGeometry({ QPoint(), geometry.size() });
	_inner->resizeToWidth(geometry.width());
}

rpl::producer<Info::Action> Info::actionRequests() const {
	return _actionRequests.events();
}

void Info::setupControls(Data &&data) {
	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		_inner.get(),
		rpl::duplicate(data.state) | rpl::map(PrintAddress));
	title->setSelectable(true);
	const auto description = Ui::CreateChild<Ui::FlatLabel>(
		_inner.get(),
		std::move(data.state) | rpl::map(PrintData));
	description->setSelectable(true);

	const auto refresh = Ui::CreateChild<Ui::RoundButton>(
		_inner.get(),
		rpl::single(QString("Refresh")),
		st::walletActionButton);
	const auto send = Ui::CreateChild<Ui::RoundButton>(
		_inner.get(),
		rpl::single(QString("Send")),
		st::walletActionButton);
	const auto change = Ui::CreateChild<Ui::LinkButton>(
		_inner.get(),
		"Change password");
	const auto logout = Ui::CreateChild<Ui::LinkButton>(
		_inner.get(),
		"Logout");

	rpl::combine(
		_inner->widthValue(),
		description->sizeValue()
	) | rpl::start_with_next([=](int width, QSize descriptionSize) {
		title->move((width - title->width()) / 2, width / 10);
		description->move((width - descriptionSize.width()) / 2, width / 5);
		const auto bottom = description->y() + descriptionSize.height();
		const auto buttons = refresh->width() + send->width() + (width / 10);
		refresh->move((width - buttons) / 2, bottom + width / 10);
		send->move(
			(width - buttons) / 2 + refresh->width() + width / 10,
			bottom + width / 10);
		change->move((width - change->width()) / 2, send->y() + send->height() + width / 50);
		logout->move((width - logout->width()) / 2, change->y() + change->height() + width / 50);
		_inner->resize(width, refresh->y() + refresh->height() + width / 10);
	}, _inner->lifetime());

	refresh->setClickedCallback([=] {
		_actionRequests.fire(Action::Refresh);
	});
	send->setClickedCallback([=] {
		_actionRequests.fire(Action::Send);
	});
	change->setClickedCallback([=] {
		_actionRequests.fire(Action::ChangePassword);
	});
	logout->setClickedCallback([=] {
		_actionRequests.fire(Action::LogOut);
	});
}

rpl::lifetime &Info::lifetime() {
	return _widget->lifetime();
}

} // namespace Wallet
