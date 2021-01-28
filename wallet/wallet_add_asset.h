#pragma once

#include "wallet_add_asset.h"

#include "ui/layers/generic_box.h"

namespace Ton {
struct WalletState;
}  // namespace Ton

namespace Wallet {

struct CustomAsset;

enum class AddAssetField {
  Address,
};

void AddAssetBox(not_null<Ui::GenericBox *> box, const Fn<void(CustomAsset, Fn<void(AddAssetField)> error)> &done);

}  // namespace Wallet
