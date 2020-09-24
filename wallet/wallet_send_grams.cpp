// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_send_grams.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/inline_token_icon.h"
#include "base/algorithm.h"
#include "base/qt_signal_producer.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

#include <iostream>

namespace Wallet {
namespace {

struct FixedAddress {
	PreparedInvoice invoice;
	int position = 0;
};

[[nodiscard]] QString AmountSeparator() {
	return FormatAmount(1, Ton::TokenKind::DefaultToken).separator;
}

[[nodiscard]] FixedAddress FixAddressInput(
		const QString &text,
		int position) {
	auto result = FixedAddress{ ParseInvoice(text), position };
	if (result.invoice.address != text) {
		const auto removed = std::max(
			int(text.size()) - int(result.invoice.address.size()),
			0);
		result.position = std::max(position - removed, 0);
	}
	return result;
}

} // namespace

void SendGramsBox(
		not_null<Ui::GenericBox*> box,
		const std::variant<PreparedInvoice, QString> &invoice,
        rpl::producer<Ton::WalletState> state,
        rpl::producer<std::optional<Ton::TokenKind>> selectedToken,
		const Fn<void(PreparedInvoice, Fn<void(InvoiceField)> error)> &done) {

    const auto prepared = box->lifetime().make_state<PreparedInvoice>(v::match(invoice, [](const PreparedInvoice &invoice) {
    	return invoice;
    }, [](const QString &link) {
		return ParseInvoice(link);
    }));
    const auto currentToken = box->lifetime().make_state<Ton::TokenKind>(prepared->token);

    const auto token = rpl::duplicate(selectedToken) | rpl::map([=](std::optional<Ton::TokenKind> token) {
        if (prepared->address.isEmpty()) {
            return token.value_or(Ton::TokenKind::DefaultToken);
        } else {
            return prepared->token;
        }
    });
    rpl::duplicate(token) | rpl::start_with_next([=](Ton::TokenKind value) {
        *currentToken = value;
	}, box->lifetime());


    auto unlockedBalance = rpl::duplicate(state) | rpl::map([=](const Ton::WalletState &state) {
        if (!*currentToken) {
            return state.account.fullBalance - state.account.lockedBalance;
        } else {
            const auto it = state.tokenStates.find(*currentToken);
            if (it != state.tokenStates.end()) {
                return it->second.fullBalance;
            } else {
                return int64{};
            }
        }
    });

	const auto funds = std::make_shared<int64>();

	const auto replaceTickerTag = [=] {
		return rpl::map([=](QString &&text) {
			return text.replace("{ticker}", Ton::toString(*currentToken));
		});
	};

	const auto replaceGramsTag = [] {
		return rpl::map([=](QString &&text, const QString &grams) {
			return text.replace("{grams}", grams);
		});
	};

	const auto titleText = rpl::duplicate(ph::lng_wallet_send_title()) | replaceTickerTag();

	box->setTitle(titleText);
	box->setStyle(st::walletBox);

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	AddBoxSubtitle(box, ph::lng_wallet_send_recipient());
	const auto address = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::walletSendInput,
		Ui::InputField::Mode::NoNewlines,
		ph::lng_wallet_send_address(),
		prepared->address));
	address->rawTextEdit()->setWordWrapMode(QTextOption::WrapAnywhere);

	const auto subtitle = AddBoxSubtitle(box, ph::lng_wallet_send_amount());

	const auto amount = box->addRow(
		object_ptr<Ui::InputField>::fromRaw(CreateAmountInput(
			box,
			rpl::single("0" + AmountSeparator() + "0"),
			prepared->amount,
			token)),
		st::walletSendAmountPadding);

	auto balanceText = rpl::combine(
		ph::lng_wallet_send_balance(),
		rpl::duplicate(unlockedBalance)
	) | rpl::map([=](QString &&phrase, int64 value) {
		return phrase.replace(
			"{amount}",
			FormatAmount(std::max(value, 0LL), *currentToken, FormatFlag::Rounded).full);
	});

	const auto diamondLabel = Ui::CreateInlineTokenIcon(
        token,
		subtitle->parentWidget(),
		0,
		0,
		st::walletSendBalanceLabel.style.font);
	const auto balanceLabel = Ui::CreateChild<Ui::FlatLabel>(
		subtitle->parentWidget(),
		std::move(balanceText),
		st::walletSendBalanceLabel);
	rpl::combine(
		subtitle->geometryValue(),
		balanceLabel->widthValue()
	) | rpl::start_with_next([=](QRect rect, int innerWidth) {
		const auto diamondTop = rect.top()
			+ st::walletSubsectionTitle.style.font->ascent
			- st::walletDiamondAscent;
		const auto diamondRight = st::boxRowPadding.right();
		diamondLabel->moveToRight(diamondRight, diamondTop);
		const auto labelTop = rect.top()
			+ st::walletSubsectionTitle.style.font->ascent
			- st::walletSendBalanceLabel.style.font->ascent;
		const auto labelRight = diamondRight
			+ st::walletDiamondSize
			+ st::walletSendBalanceLabel.style.font->spacew;
		balanceLabel->moveToRight(labelRight, labelTop);
	}, balanceLabel->lifetime());

	const auto comment = box->addRow(
		object_ptr<Ui::InputField>::fromRaw(CreateCommentInput(
			box,
			ph::lng_wallet_send_comment(),
			prepared->comment)),
		st::walletSendCommentPadding);

	rpl::duplicate(
		token
	) | rpl::start_with_next([=](const std::optional<Ton::TokenKind> &selectedToken) {
		const auto showComment = (selectedToken.has_value() && !*selectedToken);
		comment->setMinHeight(showComment ? st::walletInput.heightMin : 0);
		comment->setMaxHeight(showComment ? st::walletInput.heightMax : 0);
	}, comment->lifetime());

	const auto checkFunds = [=](const QString &amount, Ton::TokenKind token) {
		if (const auto value = ParseAmountString(amount, Ton::countDecimals(token))) {
			const auto insufficient = (*value > std::max(*funds, 0LL));
			balanceLabel->setTextColorOverride(insufficient
				? std::make_optional(st::boxTextFgError->c)
				: std::nullopt);
		}
	};
	rpl::combine(
		std::move(unlockedBalance),
		rpl::duplicate(token)
	) | rpl::start_with_next([=](int64 value, Ton::TokenKind token) {
		*funds = value;
		checkFunds(amount->getLastText(), token);
	}, amount->lifetime());

	Ui::Connect(amount, &Ui::InputField::changed, [=] {
		Ui::PostponeCall(amount, [=] {
			checkFunds(amount->getLastText(), *currentToken);
		});
	});
	Ui::Connect(address, &Ui::InputField::changed, [=] {
		Ui::PostponeCall(address, [=] {
			const auto position = address->textCursor().position();
			const auto now = address->getLastText();
			const auto fixed = FixAddressInput(now, position);
			if (fixed.invoice.address != now) {
				address->setText(fixed.invoice.address);
				address->setFocusFast();
				address->setCursorPosition(fixed.position);
			}
			if (fixed.invoice.amount > 0) {
				amount->setText(FormatAmount(
					fixed.invoice.amount,
					*currentToken,
					FormatFlag::Simple).full);
			}
			if (!fixed.invoice.comment.isEmpty()) {
				comment->setText(fixed.invoice.comment);
			}

			const auto colonPosition = fixed.invoice.address.indexOf(':');
			const auto isRaw = colonPosition > 0;
			const auto hexPrefixPosition = fixed.invoice.address.indexOf("0x");
			const auto isEtheriumAddress = hexPrefixPosition == 0;

			bool shouldShiftFocus = false;
			if (isRaw && ((fixed.invoice.address.size() - colonPosition - 1) == kRawAddressLength)) {
				shouldShiftFocus = true;
			} else if (isEtheriumAddress && (fixed.invoice.address.size() - 2) == kEtheriumAddressLength) {
				shouldShiftFocus = true;
			} else if (!(isRaw || isEtheriumAddress) && fixed.invoice.address.size() == kEncodedAddressLength) {
				shouldShiftFocus = true;
			}

			if (shouldShiftFocus && address->hasFocus()) {
				if (amount->getLastText().isEmpty()) {
					amount->setFocus();
				} else {
					comment->setFocus();
				}
			}
		});
	});

	box->setFocusCallback([=] {
		if (prepared->address.isEmpty()
			|| address->getLastText() != prepared->address) {
			address->setFocusFast();
		} else {
			amount->setFocusFast();
		}
	});

	const auto showError = crl::guard(box, [=](InvoiceField field) {
		switch (field) {
		case InvoiceField::Address: address->showError(); return;
		case InvoiceField::Amount: amount->showError(); return;
		case InvoiceField::Comment: comment->showError(); return;
		}
		Unexpected("Field value in SendGramsBox error callback.");
	});
	const auto submit = [=] {
		auto collected = PreparedInvoice();
		const auto parsed = ParseAmountString(amount->getLastText(), Ton::countDecimals(*currentToken));
		if (!parsed) {
			amount->showError();
			return;
		}
		collected.amount = *parsed;
		collected.address = address->getLastText();
		collected.comment = comment->getLastText();
		collected.swapBack = collected.address.startsWith("0x");
		collected.token = *currentToken;
		done(collected, showError);
	};

	Ui::Connect(address, &Ui::InputField::submitted, [=] {
		const auto text = address->getLastText();
		const auto colonPosition = text.indexOf(':');
		const auto isRaw = colonPosition > 0;
		const auto hexPrefixPosition = text.indexOf("0x");
		const auto isEtheriumAddress = hexPrefixPosition == 0;

		bool showAddressError = false;
		if (isRaw && ((text.size() - colonPosition - 1) != kRawAddressLength)) {
			showAddressError = true;
		} else if (isEtheriumAddress && (text.size() - 2) != kEtheriumAddressLength) {
			showAddressError = true;
		} else if (!(isRaw || isEtheriumAddress) && (text.size() != kEncodedAddressLength)) {
			showAddressError = true;
		}

		if (showAddressError) {
			address->showError();
		} else {
			amount->setFocus();
		}
	});
	Ui::Connect(amount, &Ui::InputField::submitted, [=] {
		if (ParseAmountString(amount->getLastText(), Ton::countDecimals(*currentToken)).value_or(0) <= 0) {
			amount->showError();
		} else {
			comment->setFocus();
		}
	});
	Ui::Connect(comment, &Ui::InputField::submitted, submit);

	auto text = rpl::single(
		rpl::empty_value()
	) | rpl::then(base::qt_signal_producer(
		amount,
		&Ui::InputField::changed
	)) | rpl::map([=]() -> rpl::producer<QString> {
		const auto text = amount->getLastText();
		const auto value = ParseAmountString(text, Ton::countDecimals(*currentToken)).value_or(0);
		if (value > 0) {
            return rpl::combine(
                    ph::lng_wallet_send_button_amount(),
                    ph::lng_wallet_grams_count(FormatAmount(value, *currentToken).full, *currentToken)()
            ) | replaceGramsTag();
        } else {
            return rpl::duplicate(ph::lng_wallet_send_button()) | replaceTickerTag();
        }
	}) | rpl::flatten_latest();

	box->addButton(
		std::move(text),
		submit,
		st::walletBottomButton
	)->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

} // namespace Wallet
