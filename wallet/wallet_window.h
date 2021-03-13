// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <mutex>

#include "ton/ton_state.h"
#include "base/weak_ptr.h"
#include "base/object_ptr.h"

#include "wallet_common.h"

#include <QtCore/QPointer>

namespace Ton {
class Wallet;
class AccountViewer;
struct TransactionCheckResult;
struct PendingTransaction;
struct Transaction;
struct WalletState;
struct Error;
struct Settings;
enum class ConfigUpgrade;
}  // namespace Ton

namespace Ui {
class Window;
class LayerManager;
class GenericBox;
class RpWidget;
class FlatButton;
}  // namespace Ui

namespace Wallet {
namespace Create {
class Manager;
}  // namespace Create

class Info;
struct StakeInvoice;
enum class InvoiceField;
class UpdateInfo;
enum class InfoTransition;
using PreparedInvoiceOrLink = std::variant<PreparedInvoice, QString>;

class Window final : public base::has_weak_ptr {
 public:
  Window(not_null<Ton::Wallet *> wallet, UpdateInfo *updateInfo = nullptr);
  ~Window();

  void showAndActivate();
  [[nodiscard]] not_null<Ui::RpWidget *> widget() const;
  bool handleLinkOpen(const QString &link);
  void showConfigUpgrade(Ton::ConfigUpgrade upgrade);

 private:
  struct DecryptPasswordState {
    int generation = 0;
    bool success = false;
    QPointer<Ui::GenericBox> box;
    Fn<void(QString)> showError;
  };

  void init();
  void updatePalette();
  void showSimpleError(rpl::producer<QString> title, rpl::producer<QString> text, rpl::producer<QString> button);
  void showGenericError(const Ton::Error &error, const QString &additional = QString());
  void showSendingError(const Ton::Error &error);
  void showToast(const QString &text);
  void startWallet();

  void showCreate();
  void createImportKey(const std::vector<QString> &words);
  void createKey(std::shared_ptr<bool> guard);
  void createShowIncorrectWords();
  void createShowIncorrectImport();
  void createShowImportFail();
  void createSavePasscode(const QByteArray &passcode, const std::shared_ptr<bool> &guard);
  void createSaveKey(const QByteArray &passcode, const QString &address, const std::shared_ptr<bool> &guard);

  void decryptEverything(const QByteArray &publicKey);
  void askDecryptPassword(const Ton::DecryptPasswordNeeded &data);
  void doneDecryptPassword(const Ton::DecryptPasswordGood &data);

  void openInExplorer(const QString &transactionHash);

  void showAccount(const QByteArray &publicKey, bool justCreated = false);
  void setupUpdateWithInfo();
  void setupRefreshEach();
  void sendMoney(const PreparedInvoiceOrLink &symbol);
  void sendStake(const StakeInvoice &invoice);
  void dePoolWithdraw(const WithdrawalInvoice &invoice);
  void dePoolCancelWithdrawal(const CancelWithdrawalInvoice &invoice);
  void deployTokenWallet(const DeployTokenWalletInvoice &invoice);
  void collectTokens(const QString &eventContractAddress);

  void confirmTransaction(PreparedInvoice invoice, const Fn<void(InvoiceField)> &showInvoiceError,
                          const std::shared_ptr<bool> &guard);
  void showSendConfirmation(const PreparedInvoice &invoice, const Ton::TransactionCheckResult &checkResult,
                            const Fn<void(InvoiceField)> &showInvoiceError);
  void askSendPassword(const PreparedInvoice &invoice, const Fn<void(InvoiceField)> &showInvoiceError);
  void showSendingTransaction(const Ton::PendingTransaction &transaction, const PreparedInvoice &invoice,
                              rpl::producer<> confirmed);
  void showSendingDone(std::optional<Ton::Transaction> result, const PreparedInvoice &invoice);

  void addAsset();
  void refreshNow();
  void receiveTokens(RecipientWalletType type, const QString &address, const Ton::Symbol &symbol);
  void createInvoice(const Ton::Symbol &selectedToken);
  void showInvoiceQr(const QString &link);
  void changePassword();
  void askExportPassword();
  void showExported(const std::vector<QString> &words);
  void showSettings();
  void checkConfigFromContent(QByteArray bytes, Fn<void(QByteArray)> good);
  void saveSettings(const Ton::Settings &settings);
  void saveSettingsWithLoaded(const Ton::Settings &settings);
  void saveSettingsSure(const Ton::Settings &settings, const Fn<void()> &done);
  void showSwitchTestNetworkWarning(const Ton::Settings &settings);
  void showBlockchainNameWarning(const Ton::Settings &settings);
  void showSettingsWithLogoutWarning(const Ton::Settings &settings, rpl::producer<QString> text);

  using OnFtabiKeyCreated = Fn<void(QByteArray)>;
  void showKeystore();
  void exportFtabiKey(const QByteArray &publicKey);
  void showExportedFtabiKey(const std::vector<QString> &words);
  void addFtabiKey(const Fn<void()> &cancel, const OnFtabiKeyCreated &done);
  void importFtabiKey(const QString &name, const Fn<void()> &cancel, const OnFtabiKeyCreated &done);
  void showNewFtabiKey(const std::vector<QString> &words, const OnFtabiKeyCreated &done);
  void askNewFtabiKeyPassword(const OnFtabiKeyCreated &done);
  void askFtabiKeyChangePassword(const QByteArray &publicKey);

  void importMultisig(const QString &address);
  void showMultisigError();
  void selectMultisigKey(const std::vector<QByteArray> &custodians, int defaultIndex, bool allowNewKeys,
                         const Fn<void(QByteArray)> &done);
  void addNewMultisig();

  std::vector<QByteArray> getAllPublicKeys() const;
  std::vector<Ton::AvailableKey> getAvailableKeys(const std::vector<QByteArray> &custodians);

  [[nodiscard]] Fn<void(QImage, QString)> shareCallback(const QString &linkCopied, const QString &textCopied,
                                                        const QString &qr);
  [[nodiscard]] Fn<void(QImage, QString)> shareAddressCallback();
  [[nodiscard]] Fn<void(QString)> sharePubKeyCallback();

  void logoutWithConfirmation();
  void logout();
  void back();

  // Before _layers, because box destructor can set this pointer.
  std::unique_ptr<DecryptPasswordState> _decryptPasswordState;

  const not_null<Ton::Wallet *> _wallet;
  const std::unique_ptr<Ui::Window> _window;
  const std::unique_ptr<Ui::LayerManager> _layers;
  UpdateInfo *const _updateInfo = nullptr;

  std::unique_ptr<Create::Manager> _createManager;
  rpl::event_stream<QString> _createSyncing;
  bool _importing = false;
  bool _testnet = false;

  QString _packedAddress;
  QString _rawAddress;
  std::unique_ptr<Ton::AccountViewer> _viewer;
  rpl::variable<Ton::WalletState> _state;
  rpl::variable<std::optional<SelectedAsset>> _selectedAsset;
  rpl::variable<bool> _syncing;
  std::unique_ptr<Info> _info;
  object_ptr<Ui::FlatButton> _updateButton = {nullptr};
  rpl::event_stream<rpl::producer<int>> _updateButtonHeight;

  rpl::event_stream<not_null<std::vector<Ton::Transaction> *>> _collectEncryptedRequests;
  rpl::event_stream<not_null<const std::vector<Ton::Transaction> *>> _decrypted;
  rpl::event_stream<InfoTransition> _infoTransitions;
  rpl::event_stream<NotificationsHistoryUpdate> _notificationHistoryUpdates;
  rpl::event_stream<not_null<std::map<QString, QString> *>> _updateTokenOwners;

  QPointer<Ui::GenericBox> _sendBox;
  QPointer<Ui::GenericBox> _sendConfirmBox;
  QPointer<Ui::GenericBox> _simpleErrorBox;
  QPointer<Ui::GenericBox> _settingsBox;
  QPointer<Ui::GenericBox> _saveConfirmBox;

  QPointer<Ui::GenericBox> _keystoreBox;
  QPointer<Ui::GenericBox> _keySelectionBox;

  std::shared_ptr<bool> _multisigConfirmationGuard;
};

}  // namespace Wallet
