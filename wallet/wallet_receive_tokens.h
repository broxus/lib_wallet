// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/layers/generic_box.h"

namespace Ton {
enum class TokenKind;
} // namespace Ton

namespace Wallet {

void ReceiveTokensBox(
	not_null<Ui::GenericBox*> box,
	const QString &packedAddress,
	const QString &rawAddress,
	Ton::TokenKind token,
	const Fn<void()> &createInvoice,
	const Fn<void(QImage, QString)> &share,
	const Fn<void()> &swap);

} // namespace Wallet
