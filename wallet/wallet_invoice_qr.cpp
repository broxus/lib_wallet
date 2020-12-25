// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_invoice_qr.h"

#include "wallet/wallet_common.h"
#include "wallet/wallet_phrases.h"
#include "ui/widgets/buttons.h"
#include "ui/inline_token_icon.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"

namespace Wallet {

void InvoiceQrBox(
		not_null<Ui::GenericBox*> box,
		const QString &link,
        Ton::TokenKind token,
		const Fn<void(QImage, QString)> &share) {

	box->setTitle(ph::lng_wallet_invoice_qr_title());
	box->setStyle(st::walletBox);

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	const auto container = box->addRow(
		object_ptr<Ui::BoxContentDivider>(box, 1),
		st::walletInvoiceQrMargin);

    auto currentToken = container->lifetime().make_state<Ton::TokenKind>();

    const auto button = Ui::CreateChild<Ui::AbstractButton>(container);

    auto qr = button->lifetime().make_state<QImage>();
	*qr = Ui::TokenQr(
		token,
		link,
		st::walletInvoiceQrPixel,
		st::boxWidth - st::boxRowPadding.left() - st::boxRowPadding.right());

	const int size = qr->width() / style::DevicePixelRatio();
	const auto height = st::walletInvoiceQrSkip * 2 + size;

	container->setFixedHeight(height);

	button->resize(size, size);

    button->setClickedCallback([=] {
        share(Ui::TokenQrForShare(*currentToken, link), QString());
    });

    button->paintRequest(
	) | rpl::start_with_next([=] {
        const auto size = qr->width() / style::DevicePixelRatio();
		QPainter(button).drawImage(QRect(0, 0, size, size), *qr);
	}, button->lifetime());

	container->widthValue(
	) | rpl::start_with_next([=](int width) {
        const auto size = qr->width() / style::DevicePixelRatio();
		button->move((width - size) / 2, st::walletInvoiceQrSkip);
	}, button->lifetime());


	const auto prepared = ParseInvoice(link);

	AddBoxSubtitle(box, ph::lng_wallet_invoice_qr_amount());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			FormatAmount(prepared.amount, prepared.token).full,
			st::walletLabel),
		st::walletInvoiceQrValuePadding);

	if (!prepared.comment.isEmpty()) {
		AddBoxSubtitle(box, ph::lng_wallet_invoice_qr_comment());

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				prepared.comment,
				st::walletLabel),
			st::walletInvoiceQrValuePadding);
	}

	box->addButton(
		ph::lng_wallet_invoice_qr_share(),
		[=] { share(Ui::TokenQrForShare(*currentToken, link), QString()); },
		st::walletBottomButton
	)->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

} // namespace Wallet
