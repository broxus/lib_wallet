// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/layers/generic_box.h"

namespace Ton {
struct Transaction;
enum class TokenKind;
} // namespace Ton

namespace Wallet {

void SendingTransactionBox(
	not_null<Ui::GenericBox*> box,
	Ton::TokenKind token,
	rpl::producer<> confirmed);

template<typename T>
void SendingDoneBox(
	not_null<Ui::GenericBox*> box,
	const Ton::Transaction &result,
	const T &invoice,
	const Fn<void()> &onClose);

} // namespace Wallet
