// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/layers/generic_box.h"

namespace Ton {
enum class Currency;
struct WalletState;
}  // namespace Ton

namespace Wallet {

struct TonTransferInvoice;
struct TokenTransferInvoice;

enum class InvoiceField {
  Address,
  Amount,
  Comment,
};

template <typename T>
void SendGramsBox(not_null<Ui::GenericBox *> box, const T &invoice, rpl::producer<Ton::WalletState> state,
                  const Fn<void(const T &, Fn<void(InvoiceField)> error)> &done);

}  // namespace Wallet
