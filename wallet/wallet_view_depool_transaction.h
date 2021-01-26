#pragma once

#include "ui/layers/generic_box.h"

namespace Ton {
struct Transaction;
}

namespace Wallet {

void ViewDePoolTransactionBox(not_null<Ui::GenericBox *> box, const Ton::Transaction &data,
                              const Fn<void(QImage, QString)> &share);

}  // namespace Wallet
