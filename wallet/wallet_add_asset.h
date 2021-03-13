#pragma once

#include "wallet_add_asset.h"

#include "ui/layers/generic_box.h"

namespace Ton {
struct WalletState;
struct FtabiKey;
struct AvailableKey;
struct MultisigInitialInfo;
enum class KeyType;
enum class MultisigVersion;
}  // namespace Ton

namespace Wallet {

struct NewAsset;
struct MultisigDeployInvoice;

enum class AddAssetField {
  Address,
};

void AddAssetBox(not_null<Ui::GenericBox *> box, const Fn<void(NewAsset)> &done);

void SelectMultisigKeyBox(not_null<Ui::GenericBox *> box, const std::vector<QByteArray> &custodians,
                          const std::vector<Ton::AvailableKey> &availableKeys, int defaultIndex, bool allowNewKeys,
                          const Fn<void()> &addNewKey, const Fn<void(QByteArray)> &done);

void SelectMultisigVersionBox(not_null<Ui::GenericBox *> box, const Fn<void(Ton::MultisigVersion)> &done);

void PredeployMultisigBox(not_null<Ui::GenericBox *> box, const Ton::MultisigInitialInfo &info,
                          const Fn<void(QImage, QString)> &share, const Fn<void()> &done);

void DeployMultisigBox(not_null<Ui::GenericBox *> box, const Ton::MultisigInitialInfo &info,
                       const Fn<void(MultisigDeployInvoice)> &done);

}  // namespace Wallet
