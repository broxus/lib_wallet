// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/layers/generic_box.h"

#include "ton/ton_state.h"

namespace Wallet {

void CreateInvoiceBox(not_null<Ui::GenericBox *> box, const QString &address, bool testnet, const Ton::Symbol &symbol,
                      const Fn<void(QString)> &generateQr, const Fn<void(QImage, QString)> &share);

}  // namespace Wallet
