#pragma once

#include "ui/layers/generic_box.h"

namespace Ton {
struct FtabiKey;
enum class KeyType;
}  // namespace Ton

namespace Wallet {

enum class KeystoreAction {
  Export,
  ChangePassword,
  Delete,
};

using WordsList = std::vector<QString>;

using OnKeystoreAction = Fn<void(Ton::KeyType, const QByteArray &, KeystoreAction)>;

struct NewFtabiKey {
  QString name;
  bool generate;
};

void KeystoreBox(not_null<Ui::GenericBox *> box, const QByteArray &mainPublicKey,
                 const std::vector<Ton::FtabiKey> &ftabiKeys, const Fn<void(QString)> &share,
                 const OnKeystoreAction &onAction, const Fn<void()> &createFtabiKey);

void NewFtabiKeyBox(not_null<Ui::GenericBox *> box, const Fn<void()> &cancel, const Fn<void(NewFtabiKey)> &done);

void ImportFtabiKeyBox(not_null<Ui::GenericBox *> box, const Fn<void()> &cancel, const Fn<void(WordsList)> &done);

void GeneratedFtabiKeyBox(not_null<Ui::GenericBox *> box, const WordsList &words, const Fn<void()> &done);

void ExportedFtabiKeyBox(not_null<Ui::GenericBox *> box, const WordsList &words);

void NewFtabiKeyPasswordBox(not_null<Ui::GenericBox *> box,
                            const Fn<void(const QByteArray &, const Fn<void(QString)> &)> &done);

}  // namespace Wallet
