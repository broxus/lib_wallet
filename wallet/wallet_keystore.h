#pragma once

#include "ui/layers/generic_box.h"

namespace Ton {
struct FtabiKey;
struct LedgerKey;
struct TonLedgerKey;
enum class KeyType;
}  // namespace Ton

namespace Wallet {

enum class KeystoreAction {
  Export,
  ChangePassword,
  Delete,
};

using WordsList = std::vector<QString>;
using LedgerKeysList = std::vector<Ton::TonLedgerKey>;

using OnKeystoreAction = Fn<void(Ton::KeyType, const QByteArray &, KeystoreAction)>;

enum class NewFtabiAction : int {
  Generate = 0,
  Import,
  ImportLedger,
};

struct NewFtabiKey {
  QString name;
  NewFtabiAction action;
};

void KeystoreBox(not_null<Ui::GenericBox *> box, const QByteArray &mainPublicKey,
                 const std::vector<Ton::FtabiKey> &ftabiKeys, const std::vector<Ton::LedgerKey> &ledgerKeys, const Fn<void(QString)> &share,
                 const OnKeystoreAction &onAction, const Fn<void()> &createFtabiKey);

void NewFtabiKeyBox(not_null<Ui::GenericBox *> box, const Fn<void()> &cancel, const Fn<void(NewFtabiKey)> &done);

void ImportFtabiKeyBox(not_null<Ui::GenericBox *> box, const Fn<void()> &cancel, const Fn<void(WordsList)> &done);

void GeneratedFtabiKeyBox(not_null<Ui::GenericBox *> box, const WordsList &words, const Fn<void()> &done);

void ImportLedgerKeyBox(not_null<Ui::GenericBox *> box, const Fn<void()> &cancel, const LedgerKeysList &ledgerKeys, const Fn<void(const LedgerKeysList& ledgerKeys)> &done);

void ExportedFtabiKeyBox(not_null<Ui::GenericBox *> box, const WordsList &words);

void NewFtabiKeyPasswordBox(not_null<Ui::GenericBox *> box,
                            const Fn<void(const QByteArray &, const Fn<void(QString)> &)> &done);

}  // namespace Wallet
