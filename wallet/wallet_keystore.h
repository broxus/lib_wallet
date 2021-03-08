#pragma once

#include "ui/layers/generic_box.h"

namespace Ton {
struct FtabiKey;
}  // namespace Ton

namespace Wallet {

void KeystoreBox(not_null<Ui::GenericBox *> box, const QByteArray &mainPublicKey,
                 const std::vector<Ton::FtabiKey> &ftabiKeys, const Fn<void(QString)> &share,
                 const Fn<void()> &createFtabiKey);

}  // namespace Wallet
