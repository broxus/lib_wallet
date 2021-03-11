#pragma once

#include "wallet_add_asset.h"

#include "ui/layers/generic_box.h"

namespace Ton {
struct WalletState;
struct MultisigInfo;
struct FtabiKey;
struct AvailableKey;
enum class KeyType;
}  // namespace Ton

namespace Wallet {

struct NewAsset;

enum class AddAssetField {
  Address,
};

void AddAssetBox(not_null<Ui::GenericBox *> box, const Fn<void(NewAsset)> &done);

void SelectMultisigKeyBox(not_null<Ui::GenericBox *> box, const Ton::MultisigInfo &info,
                          const std::vector<Ton::AvailableKey> &availableKeys, int defaultIndex,
                          const Fn<void()> &addNewKey, const Fn<void(QByteArray)> &done);

}  // namespace Wallet
