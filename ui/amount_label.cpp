// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/amount_label.h"

#include "wallet/wallet_common.h"
#include "ui/lottie_widget.h"
#include "styles/style_wallet.h"
#include "inline_token_icon.h"

#include <iostream>

namespace Ui {
namespace {

using Wallet::FormattedAmount;

rpl::producer<QString> LargeText(rpl::producer<FormattedAmount> amount) {
	return std::move(
		amount
	) | rpl::map([](const FormattedAmount &amount) {
		return amount.gramsString;
	});
}

rpl::producer<QString> SmallText(rpl::producer<FormattedAmount> amount) {
	return std::move(
		amount
	) | rpl::map([](const FormattedAmount &amount) {
		return amount.separator + amount.nanoString;
	});
}

rpl::producer<Ton::TokenKind> Token(rpl::producer<FormattedAmount> amount) {
	return std::move(
		amount
	) | rpl::map([](const FormattedAmount &amount) {
		return amount.token;
	});
}

} // namespace

AmountLabel::AmountLabel(
	not_null<QWidget*> parent,
	rpl::producer<FormattedAmount> amount,
	const style::WalletAmountLabel &st)
: _st(st)
, _large(parent, LargeText(rpl::duplicate(amount)), st.large)
, _small(parent, SmallText(rpl::duplicate(amount)), st.small)
, _token(Token(amount))
, _diamond(!st.diamond
	? nullptr
	: std::make_unique<LottieAnimation>(
		parent,
		LottieFromResource("diamond")))
,_tokenIcon(!st.diamond
            ? nullptr
            : std::make_unique<Ui::FixedHeightWidget>(
            parent)) {

    const auto currentToken = _tokenIcon->lifetime().make_state<Ton::TokenKind>(Ton::TokenKind::DefaultToken);

    _diamond->start();

    if (_diamond) {
        rpl::duplicate(
			_token
		) | rpl::start_with_next([=](Ton::TokenKind token) {

            *currentToken = token;

		    if (!!token) {
		        _diamond->setVisible(false);
                _tokenIcon->setVisible(true);
            } else {

            _diamond->start();

            _diamond->setVisible(true);
            _tokenIcon->setVisible(false);
            }
        }, _large.lifetime());
    }
    _large.show();
    _small.show();

	_tokenIcon->paintRequest() | rpl::start_with_next([=](QRect clip){
        QPainter p(_tokenIcon.get());
        p.drawImage(0,0, Ui::InlineTokenIcon(*currentToken, _st.tokenIcon));
	}, _tokenIcon->lifetime());
}

AmountLabel::~AmountLabel() = default;

rpl::producer<int> AmountLabel::widthValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_large.widthValue(),
		_small.widthValue(),
		rpl::duplicate(_token)
	) | rpl::map([this](int largeWidth, int smallWidth, Ton::TokenKind token) {
		return largeWidth + smallWidth + (_diamond ? (_st.diamond + _st.diamondPosition.x()) : 0);
	});
}

int AmountLabel::height() const {
	return _large.height();
}

void AmountLabel::move(int x, int y) {
	_large.move(x, y);
	x += _large.width();
	_small.move(
		x,
		y + _st.large.style.font->ascent - _st.small.style.font->ascent);
	x += _small.width();
	if (_diamond) {
		const auto size = QSize(_st.diamond, _st.diamond);
		_diamond->setGeometry(
			{ QPoint(x, y) + _st.diamondPosition, size });
	}
    if (_tokenIcon) {
        const auto size = QSize(_st.tokenIcon, _st.tokenIcon);
        _tokenIcon->setGeometry(
            { QPoint(x, y) + _st.tokenIconPosition, size });
    }
}

rpl::lifetime &AmountLabel::lifetime() {
	return _lifetime;
}

} // namespace Ui
