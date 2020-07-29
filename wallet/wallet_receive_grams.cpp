// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_receive_grams.h"

#include "wallet/wallet_phrases.h"
#include "ui/address_label.h"
#include "ui/inline_diamond.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "qr/qr_generate.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"

namespace Wallet {

void ReceiveGramsBox(
		not_null<Ui::GenericBox*> box,
		const QString &packedAddress,
		const QString &rawAddress,
		const QString &link,
		bool testnet,
		Fn<void()> createInvoice,
		Fn<void(QImage, QString)> share) 
{
	const auto showAsPackedOn = box->lifetime().make_state<rpl::variable<bool>>(true);

	box->setTitle(ph::lng_wallet_receive_title());
	box->setStyle(st::walletBox);

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	// box->addRow(
	//	object_ptr<Ui::FlatLabel>(
	//		box,
	//		(testnet
	//			? ph::lng_wallet_receive_description_test()
	//			: ph::lng_wallet_receive_description()),
	//		st::walletLabel),
	//	st::walletReceiveLabelPadding);

	// Qr code image
	const auto qr = Ui::DiamondQr(link, st::walletReceiveQrPixel);
	const auto size = qr.width() / style::DevicePixelRatio();
	const auto container = box->addRow(object_ptr<Ui::AbstractButton>(box));

	container->resize(size, size);

	container->paintRequest() | rpl::start_with_next([=] {
		QPainter(container).drawImage(
			QRect((container->width() - size) / 2, 0, size, size),
			qr);
	}, container->lifetime());

	container->setClickedCallback([=] {
		share(Ui::DiamondQrForShare(link), QString());
	});

	// Address label
	const auto packedAddressLabel = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
			box,
			object_ptr<Ui::RpWidget>::fromRaw(Ui::CreateAddressLabel(
				box,
				packedAddress,
				st::walletReceiveAddressLabel,
				[=] { share(QImage(), packedAddress); })),
			st::walletReceiveAddressPadding
	))->setDuration(0);

	const auto rawAddressLabel = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
			box,
			object_ptr<Ui::RpWidget>::fromRaw(Ui::CreateAddressLabel(
				box,
				rawAddress,
				st::walletReceiveAddressLabel,
				[=] { share(QImage(), rawAddress); })),
			st::walletReceiveAddressPadding
	));

	// Address present group
	const auto showAsPacked = box->addRow(
		object_ptr<Ui::SettingsButton>(
			box,
			ph::lng_wallet_receive_show_as_packed(),
			st::defaultSettingsButton),
		QMargins()
	)->toggleOn(showAsPackedOn->value());

	showAsPacked->toggledValue() | rpl::start_with_next([=](bool toggled) {
		if (toggled) {
			packedAddressLabel->show(anim::type::instant);
			rawAddressLabel->hide(anim::type::instant);
		}
		else {
			packedAddressLabel->hide(anim::type::instant);
			rawAddressLabel->show(anim::type::instant);
		}
	}, showAsPacked->lifetime());

	box->addRow(
		object_ptr<Ui::BoxContentDivider>(box),
		st::walletSettingsDividerMargin);

	// Create link button
	const auto createLinkWrap = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::boxLinkButton.font->height),
		st::walletReceiveLinkPadding);

	const auto createLink = Ui::CreateChild<Ui::LinkButton>(
		createLinkWrap,
		ph::lng_wallet_receive_create_invoice(ph::now),
		st::boxLinkButton);

	createLinkWrap->widthValue() | rpl::start_with_next([=](int width) {
		createLink->move((width - createLink->width()) / 2, 0);
	}, createLink->lifetime());

	createLink->setClickedCallback([=] {
		box->closeBox();
		createInvoice();
	});

	// Submit button
	box->addButton(
		ph::lng_wallet_receive_share(),
		[=] { share(QImage(), link); },
		st::walletBottomButton
	)->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

} // namespace Wallet
