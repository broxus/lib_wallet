// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_window.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "wallet/wallet_info.h"
#include "wallet/wallet_add_asset.h"
#include "wallet/wallet_view_transaction.h"
#include "wallet/wallet_view_depool_transaction.h"
#include "wallet/wallet_receive_tokens.h"
#include "wallet/wallet_create_invoice.h"
#include "wallet/wallet_invoice_qr.h"
#include "wallet/wallet_keystore.h"
#include "wallet/wallet_send_grams.h"
#include "wallet/wallet_send_stake.h"
#include "wallet/wallet_depool_withdraw.h"
#include "wallet/wallet_depool_cancel_withdrawal.h"
#include "wallet/wallet_deploy_token_wallet.h"
#include "wallet/wallet_collect_tokens.h"
#include "wallet/wallet_enter_passcode.h"
#include "wallet/wallet_change_passcode.h"
#include "wallet/wallet_confirm_transaction.h"
#include "wallet/wallet_sending_transaction.h"
#include "wallet/wallet_delete.h"
#include "wallet/wallet_export.h"
#include "wallet/wallet_update_info.h"
#include "wallet/wallet_settings.h"
#include "wallet/create/wallet_create_manager.h"
#include "ton/ton_wallet.h"
#include "ton/ton_account_viewer.h"
#include "base/platform/base_platform_process.h"
#include "base/qt_signal_producer.h"
#include "base/last_user_input.h"
#include "base/algorithm.h"
#include "ui/widgets/window.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/buttons.h"
#include "ui/layers/layer_manager.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/palette.h"

#include <QtCore/QMimeData>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include <QtGui/QtEvents>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>

namespace Wallet {
namespace {

constexpr auto kRefreshEachDelay = 10 * crl::time(1000);
constexpr auto kRefreshInactiveDelay = 60 * crl::time(1000);
constexpr auto kRefreshWhileSendingDelay = 3 * crl::time(1000);

[[nodiscard]] bool ValidateTransferLink(const QString &link) {
  return QRegularExpression(
             QString(R"(^((freeton:\/\/)?(transfer|stake)\/)?[A-Za-z0-9_\-]{%1}\/?($|\?))").arg(kEncodedAddressLength),
             QRegularExpression::CaseInsensitiveOption)
      .match(link.trimmed())
      .hasMatch();
}

}  // namespace

Window::Window(not_null<Ton::Wallet *> wallet, UpdateInfo *updateInfo)
    : _wallet(wallet)
    , _window(std::make_unique<Ui::Window>())
    , _layers(std::make_unique<Ui::LayerManager>(_window->body()))
    , _updateInfo(updateInfo)
    , _selectedAsset(std::nullopt) {
  init();
  const auto keys = _wallet->publicKeys();
  if (keys.empty()) {
    showCreate();
  } else {
    showAccount(keys[0]);
  }
}

Window::~Window() = default;

void Window::init() {
  QApplication::setStartDragDistance(32);

  _window->setTitle(QString());
  _window->setGeometry(style::centerrect(qApp->primaryScreen()->geometry(), QRect(QPoint(), st::walletWindowSize)));
  _window->setMinimumSize(st::walletWindowSize);

  _layers->setHideByBackgroundClick(true);

  updatePalette();
  style::PaletteChanged() | rpl::start_with_next([=] { updatePalette(); }, _window->lifetime());

  startWallet();
}

void Window::startWallet() {
  const auto &was = _wallet->settings().net();

  if (was.useCustomConfig) {
    return;
  }
  const auto loaded = [=](Ton::Result<QByteArray> result) {
    auto copy = _wallet->settings();
    if (result && !copy.net().useCustomConfig && copy.net().configUrl == was.configUrl &&
        *result != copy.net().config) {
      copy.net().config = *result;
      saveSettingsSure(copy, [=] {
        if (_viewer) {
          refreshNow();
        }
      });
    }
    if (!_viewer) {
      _wallet->sync();
    }
  };
  _wallet->loadWebResource(was.configUrl, std::move(loaded));
}

void Window::updatePalette() {
  auto palette = _window->palette();
  palette.setColor(QPalette::Window, st::windowBg->c);
  _window->setPalette(palette);
  Ui::ForceFullRepaint(_window.get());
}

void Window::showCreate() {
  _layers->hideAll();
  _info = nullptr;
  _viewer = nullptr;
  _updateButton.destroy();

  _window->setTitleStyle(st::defaultWindowTitle);
  _importing = false;
  _createManager = std::make_unique<Create::Manager>(_window->body(), _updateInfo);
  _layers->raise();

  _window->body()->sizeValue()  //
      | rpl::start_with_next(
            [=](QSize size) {
              _createManager->setGeometry({QPoint(), size});
            },
            _createManager->lifetime());

  const auto creating = std::make_shared<bool>();
  _createManager->actionRequests()  //
      | rpl::start_with_next(
            [=](Create::Manager::Action action) {
              switch (action) {
                case Create::Manager::Action::NewKey:
                  if (!_importing) {
                    _createManager->showIntro();
                  }
                  return;
                case Create::Manager::Action::CreateKey:
                  createKey(creating);
                  return;
                case Create::Manager::Action::ShowCheckIncorrect:
                  createShowIncorrectWords();
                  return;
                case Create::Manager::Action::ShowImportFail:
                  createShowImportFail();
                  return;
                case Create::Manager::Action::ShowAccount:
                  showAccount(_createManager->publicKey(), !_importing);
                  return;
              }
              Unexpected("Action in Create::Manager::actionRequests().");
            },
            _createManager->lifetime());

  _createManager->importRequests()  //
      | rpl::start_with_next([=](const std::vector<QString> &words) { createImportKey(words); },
                             _createManager->lifetime());

  const auto saving = std::make_shared<bool>();
  _createManager->passcodeChosen()  //
      | rpl::start_with_next([=](const QByteArray &passcode) { createSavePasscode(passcode, saving); },
                             _createManager->lifetime());
}

void Window::createImportKey(const std::vector<QString> &words) {
  if (std::exchange(_importing, true)) {
    return;
  }
  _wallet->importKey(words, crl::guard(this, [=](Ton::Result<> result) {
                       if (result) {
                         _createSyncing = rpl::event_stream<QString>();
                         _createManager->showPasscode(_createSyncing.events());
                       } else if (IsIncorrectMnemonicError(result.error())) {
                         _importing = false;
                         createShowIncorrectImport();
                       } else {
                         _importing = false;
                         showGenericError(result.error());
                       }
                     }));
}

void Window::createKey(std::shared_ptr<bool> guard) {
  if (std::exchange(*guard, true)) {
    return;
  }
  const auto done = [=](Ton::Result<std::vector<QString>> result) {
    Expects(result.has_value());

    *guard = false;
    _createManager->showCreated(std::move(*result));
  };
  _wallet->createKey(crl::guard(this, done));
}

void Window::createShowIncorrectWords() {
  _layers->showBox(Box([=](not_null<Ui::GenericBox *> box) {
    box->setTitle(ph::lng_wallet_check_incorrect_title());
    box->addRow(object_ptr<Ui::FlatLabel>(box, ph::lng_wallet_check_incorrect_text(), st::walletLabel));
    box->addButton(ph::lng_wallet_check_incorrect_retry(), [=] {
      box->closeBox();
      _createManager->setFocus();
    });
    box->addButton(ph::lng_wallet_check_incorrect_view(), [=] {
      box->closeBox();
      _createManager->showWords(Create::Direction::Backward);
    });
  }));
}

void Window::createShowIncorrectImport() {
  showSimpleError(ph::lng_wallet_import_incorrect_title(), ph::lng_wallet_import_incorrect_text(),
                  ph::lng_wallet_import_incorrect_retry());
}

void Window::createShowImportFail() {
  _layers->showBox(Box([=](not_null<Ui::GenericBox *> box) {
    box->setTitle(ph::lng_wallet_too_bad_title());
    box->addRow(object_ptr<Ui::FlatLabel>(box, ph::lng_wallet_too_bad_description(), st::walletLabel));
    box->addButton(ph::lng_wallet_too_bad_enter_words(), [=] {
      box->closeBox();
      _createManager->setFocus();
    });
    box->addButton(ph::lng_wallet_cancel(), [=] {
      box->closeBox();
      _createManager->showIntro();
    });
  }));
}

void Window::showSimpleError(rpl::producer<QString> title, rpl::producer<QString> text, rpl::producer<QString> button) {
  if (_simpleErrorBox) {
    _simpleErrorBox->closeBox();
  }
  auto box = Box([&](not_null<Ui::GenericBox *> box) mutable {
    box->setTitle(std::move(title));
    box->addRow(object_ptr<Ui::FlatLabel>(box, std::move(text), st::walletLabel));
    box->addButton(std::move(button), [=] {
      box->closeBox();
      if (_createManager) {
        _createManager->setFocus();
      }
    });
  });
  _simpleErrorBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::showGenericError(const Ton::Error &error, const QString &additional) {
  const auto title = [&] {
    switch (error.type) {
      case Ton::Error::Type::IO:
        return "Disk Error";
      case Ton::Error::Type::TonLib:
        return "Library Error";
      case Ton::Error::Type::WrongPassword:
        return "Encryption Error";
      case Ton::Error::Type::Web:
        return "Request Error";
    }
    Unexpected("Error type in Window::showGenericError.");
  }();
  showSimpleError(rpl::single(QString(title)), rpl::single((error.details + "\n\n" + additional).trimmed()),
                  ph::lng_wallet_ok());
}

void Window::showSendingError(const Ton::Error &error) {
  const auto additional =
      ""
      "Possible error, please wait. If your transaction disappears "
      "from the \"Pending\" list and does not appear "
      "in the list of recent transactions, try again.";
  showGenericError(error, additional);
  if (_sendBox) {
    _sendBox->closeBox();
  }
}

void Window::showKeyNotFound() {
  showSimpleError(ph::lng_wallet_key_not_found_title(), ph::lng_wallet_key_not_found_text(), ph::lng_wallet_ok());
}

void Window::createSavePasscode(const QByteArray &passcode, const std::shared_ptr<bool> &guard) {
  if (std::exchange(*guard, true)) {
    return;
  }
  if (!_importing) {
    return createSaveKey(passcode, QString(), guard);
  }

  rpl::single(Ton::Update{Ton::SyncState()})  //
      | rpl::then(_wallet->updates())         //
      | rpl::map([](const Ton::Update &update) {
          return v::match(
              update.data,
              [&](const Ton::SyncState &data) {
                if (!data.valid() || data.current == data.to || data.current == data.from) {
                  return ph::lng_wallet_sync();
                } else {
                  const auto percent = QString::number((100 * (data.current - data.from) / (data.to - data.from)));
                  return ph::lng_wallet_sync_percent() |
                         rpl::map([=](QString &&text) { return text.replace("{percent}", percent); }) |
                         rpl::type_erased();
                }
              },
              [&](auto &&) { return ph::lng_wallet_sync(); });
        })                     //
      | rpl::flatten_latest()  //
      | rpl::start_to_stream(_createSyncing, _createManager->lifetime());

  const auto done = [=](Ton::Result<QString> result) {
    if (!result) {
      *guard = false;
      showGenericError(result.error());
      return;
    }
    createSaveKey(passcode, *result, guard);
  };
  _wallet->queryWalletAddress(crl::guard(this, done));
}

void Window::createSaveKey(const QByteArray &passcode, const QString &address, const std::shared_ptr<bool> &guard) {
  const auto done = [=](Ton::Result<QByteArray> result) {
    *guard = false;
    if (!result) {
      showGenericError(result.error());
      return;
    }
    _createManager->showReady(*result);
  };
  _wallet->saveOriginalKey(passcode, address, crl::guard(this, done));
}

void Window::showAccount(const QByteArray &publicKey, bool justCreated) {
  _layers->hideAll();
  _importing = false;
  _createManager = nullptr;

  _packedAddress = _wallet->getUsedAddress(publicKey);
  _rawAddress = Ton::Wallet::ConvertIntoRaw(_packedAddress);
  _viewer = _wallet->createAccountViewer(publicKey, _packedAddress);
  _state = _viewer->state() | rpl::map([](Ton::WalletViewerState &&state) { return std::move(state.wallet); });
  _syncing = false;
  _syncing = _wallet->updates()  //
             | rpl::map([](const Ton::Update &update) {
                 return v::match(
                     update.data, [&](const Ton::SyncState &data) { return data.valid() && (data.current != data.to); },
                     [&](auto &&) { return false; });
               });

  _window->setTitleStyle(st::walletWindowTitle);
  Info::Data data{
      .state = _viewer->state(),
      .loaded = _viewer->loaded(),
      .updates = _wallet->updates(),
      .collectEncrypted = _collectEncryptedRequests.events(),
      .updateDecrypted = _decrypted.events(),
      .updateWalletOwners = _updateTokenOwners.events(),
      .updateNotifications = _notificationHistoryUpdates.events(),
      .transitionEvents = _infoTransitions.events(),
      .share = shareAddressCallback(),
      .openGate = [this] { _wallet->openGate(_rawAddress); },
      .justCreated = justCreated,
      .useTestNetwork = _wallet->settings().useTestNetwork,
  };
  _info = std::make_unique<Info>(_window->body(), data);

  _info->selectedAsset()  //
      | rpl::start_with_next([=](std::optional<SelectedAsset> &&selectedAsset) { _selectedAsset = selectedAsset; },
                             _info->lifetime());

  _layers->raise();

  setupRefreshEach();

  _viewer->loaded()                                                                                                //
      | rpl::filter([](const Ton::Result<std::pair<HistoryPageKey, Ton::LoadedSlice>> &value) { return !value; })  //
      | rpl::map([](Ton::Result<std::pair<HistoryPageKey, Ton::LoadedSlice>> &&value) {
          return std::move(value.error());
        })  //
      | rpl::start_with_next([=](const Ton::Error &error) { showGenericError(error); }, _info->lifetime());

  setupUpdateWithInfo();

  _info->actionRequests()  //
      | rpl::start_with_next(
            [=](Action action) {
              switch (action) {
                case Action::Refresh:
                  refreshNow();
                  return;
                case Action::Export:
                  askExportPassword();
                  return;
                case Action::Send:
                  v::match(
                      _selectedAsset.current().value_or(SelectedToken::defaultToken()),
                      [&](const SelectedToken &selectedToken) {
                        if (selectedToken.symbol.isTon()) {
                          sendMoney(TonTransferInvoice{});
                        } else {
                          sendTokens(TokenTransferInvoice{.token = selectedToken.symbol});
                        }
                      },
                      [&](const SelectedDePool &selectedDePool) {
                        sendStake(StakeInvoice{
                            .stake = 0,
                            .dePool = selectedDePool.address,
                        });
                      },
                      [&](const SelectedMultisig &selectedMultisig) {
                        auto state = _state.current();
                        const auto it = state.multisigStates.find(selectedMultisig.address);
                        if (it == state.multisigStates.end()) {
                          return;
                        }

                        sendMoney(MultisigSubmitTransactionInvoice{
                            .multisigAddress = it->first,
                        });
                      });
                  return;
                case Action::Receive:
                  v::match(
                      _selectedAsset.current().value_or(SelectedToken::defaultToken()),
                      [&](const SelectedToken &selectedToken) {
                        receiveTokens(RecipientWalletType::Main, _rawAddress, selectedToken.symbol);
                      },
                      [&](const SelectedDePool &selectedDePool) {
                        auto state = _state.current();
                        const auto it = state.dePoolParticipantStates.find(selectedDePool.address);
                        if (it != state.dePoolParticipantStates.end() &&
                            (it->second.withdrawValue > 0 || !it->second.reinvest)) {
                          dePoolCancelWithdrawal(CancelWithdrawalInvoice{.dePool = selectedDePool.address});
                        } else {
                          dePoolWithdraw(WithdrawalInvoice{.amount = 0, .dePool = selectedDePool.address});
                        }
                      },
                      [&](const SelectedMultisig &selectedMultisig) {
                        receiveTokens(RecipientWalletType::Multisig, selectedMultisig.address, Ton::Symbol::ton());
                      });
                  return;
                case Action::ChangePassword:
                  return changePassword();
                case Action::ShowSettings:
                  return showSettings();
                case Action::ShowKeystore:
                  return showKeystore();
                case Action::AddAsset:
                  return addAsset();
                case Action::Deploy:
                  v::match(
                      _selectedAsset.current().value_or(SelectedToken::defaultToken()),
                      [&](const SelectedToken &selectedToken) {
                        auto state = _state.current();
                        const auto it = state.tokenStates.find(selectedToken.symbol);
                        if (it != state.tokenStates.end()) {
                          deployTokenWallet(
                              DeployTokenWalletInvoice{.version = it->second.version,
                                                       .rootContractAddress = it->first.rootContractAddress(),
                                                       .walletContractAddress = it->second.walletContractAddress,
                                                       .owned = true});
                        }
                      },
                      [&](const SelectedMultisig &selectedMultisig) { deployMultisig(selectedMultisig.address); },
                      [](auto &&) {});
                  return;
                case Action::Upgrade:
                  v::match(
                      _selectedAsset.current().value_or(SelectedToken::defaultToken()),
                      [&](const SelectedToken &selectedToken) {
                        if (!selectedToken.symbol.isToken()) {
                          return;
                        }
                        auto state = _state.current();
                        const auto it = state.tokenStates.find(selectedToken.symbol);
                        if (it != state.tokenStates.end()) {
                          if (_tokenUpgradeGuard && *_tokenUpgradeGuard) {
                            return;
                          }

                          if (!_tokenUpgradeGuard) {
                            _tokenUpgradeGuard = std::make_shared<bool>(false);
                          }
                          confirmTransaction(
                              UpgradeTokenWalletInvoice{
                                  .rootContractAddress = selectedToken.symbol.rootContractAddress(),
                                  .walletContractAddress = it->second.walletContractAddress,
                                  .callbackAddress = it->second.proxyAddress,
                                  .oldVersion = it->second.version,
                                  .amount = it->second.balance,
                              },
                              [=](InvoiceField) {}, _tokenUpgradeGuard);
                        }
                      },
                      [](auto &&) {});
                  return;
                case Action::LogOut:
                  return logoutWithConfirmation();
                case Action::Back:
                  return back();
              }
              Unexpected("Action in Info::actionRequests().");
            },
            _info->lifetime());

  _info->removeAssetRequests()  //
      | rpl::start_with_next(
            [=](const CustomAsset &asset) {
              switch (asset.type) {
                case CustomAssetType::DePool:
                  return _wallet->removeDePool(getMainPublicKey(), asset.address);
                case CustomAssetType::Token:
                  return _wallet->removeToken(getMainPublicKey(), asset.symbol);
                case CustomAssetType::Multisig:
                  return _wallet->removeMultisig(getMainPublicKey(), asset.address);
                default:
                  return;
              }
            },
            _info->lifetime());

  _info->assetsReorderRequests()  //
      | rpl::start_with_next(
            [this](const std::pair<int, int> &indices) {
              _wallet->reorderAssets(getMainPublicKey(), indices.first, indices.second);
            },
            _info->lifetime());

  _info->preloadRequests()  //
      | rpl::start_with_next(
            [=](const std::pair<HistoryPageKey, Ton::TransactionId> &id) {
              const auto [symbol, account] = id.first;

              if (symbol.isTon() && account.isEmpty()) {
                _viewer->preloadSlice(id.second);
              } else if (symbol.isTon()) {
                _viewer->preloadAccountSlice(account, id.second);
              } else {
                const auto state = _state.current();
                const auto it = state.tokenStates.find(symbol);
                if (it != end(state.tokenStates)) {
                  _viewer->preloadTokenSlice(symbol, it->second.walletContractAddress, id.second);
                }
              }
            },
            _info->lifetime());

  _info->ownerResolutionRequests()  //
      | rpl::start_with_next(       //
            crl::guard(             //
                this,
                [=](std::pair<const Ton::Symbol *, const QSet<QString> *> event) {
                  const auto &[symbol, wallets] = event;
                  _wallet->getWalletOwners(
                      symbol->rootContractAddress(), *wallets,
                      crl::guard(this, [=](std::map<QString, QString> &&result) { _updateTokenOwners.fire(&result); }));
                }),
            _info->lifetime());

  _info->dePoolDetailsRequests() |
      rpl::start_with_next(
          [=](not_null<const QString *> dePoolAddress) {
            const auto state = _state.current();
            for (const auto &item : state.dePoolParticipantStates) {
              if (item.first == *dePoolAddress) {
                return;
              }
            }
            _wallet->addDePool(  //
                getMainPublicKey(), *dePoolAddress, true, crl::guard(this, [this](const Ton::Result<> &result) {
                  if (result.has_value()) {
                    showToast(ph::lng_wallet_add_depool_succeeded(ph::now));
                  } else {
                    std::cout << "Failed to add depool: " << result.error().details.toStdString() << std::endl;
                  }
                }));
          },
          _info->lifetime());

  _info->tokenDetailsRequests() |
      rpl::start_with_next(
          [=](not_null<const Ton::Transaction *> transaction) {
            auto addToken = [&](const QString &rootTokenContract) {
              const auto state = _state.current();
              for (const auto &item : state.tokenStates) {
                if (rootTokenContract == item.first.rootContractAddress()) {
                  return;
                }
              }
              _wallet->addToken(  //
                  getMainPublicKey(), rootTokenContract, true, crl::guard(this, [this](const Ton::Result<> &result) {
                    if (result.has_value()) {
                      showToast(ph::lng_wallet_add_token_succeeded(ph::now));
                    } else {
                      std::cout << "Failed to add token: " << result.error().details.toStdString() << std::endl;
                    }
                  }));
            };

            auto gotDetails = [=, transaction = *transaction](auto details) mutable {
              if (details.has_value() && !details->rootTokenContract.isEmpty()) {
                const auto &rootTokenContract = details->rootTokenContract;

                const auto state = _state.current();
                for (const auto &item : state.tokenStates) {
                  if (item.first.rootContractAddress() == rootTokenContract) {
                    return _notificationHistoryUpdates.fire(AddNotification{
                        .symbol = item.first,
                        .transaction = std::move(transaction),
                    });
                  }
                }

                _wallet->getRootTokenContractDetails(
                    rootTokenContract,
                    crl::guard(this, [=](const Ton::Result<Ton::RootTokenContractDetails> &details) mutable {
                      const auto symbol = Ton::Symbol::tip3(details->symbol, details->decimals, rootTokenContract);

                      _notificationHistoryUpdates.fire(AddNotification{
                          .symbol = symbol,
                          .transaction = std::move(transaction),
                      });

                      addToken(rootTokenContract);
                    }));
              }
            };
            v::match(
                transaction->additional,
                [&](const Ton::TokenWalletDeployed &event) { addToken(event.rootTokenContract); },
                [&](const Ton::EthEventStatusChanged &) {
                  _wallet->getEthEventDetails(transaction->incoming.source, crl::guard(this, gotDetails));
                },
                [&](const Ton::TonEventStatusChanged &) {
                  _wallet->getTonEventDetails(transaction->incoming.source, crl::guard(this, gotDetails));
                },
                [](auto &&) {});
          },
          _info->lifetime());

  _info->collectTokenRequests()  //
      | rpl::start_with_next(
            [=](not_null<const QString *> eventContractAddress) { collectTokens(*eventContractAddress); },
            _info->lifetime());

  _info->executeSwapBackRequests()  //
      | rpl::start_with_next(
            [=](not_null<const QString *> eventContractAddress) {
              _wallet->openGateExecuteSwapBack(*eventContractAddress);
            },
            _info->lifetime());

  _info->multisigConfirmRequests()  //
      | rpl::start_with_next(
            [=](const std::pair<QString, int64> &confirmation) {
              if (_multisigConfirmationGuard && *_multisigConfirmationGuard) {
                return;
              }

              if (!_multisigConfirmationGuard) {
                _multisigConfirmationGuard = std::make_shared<bool>(false);
              }

              const auto state = _state.current();
              const auto it = state.multisigStates.find(confirmation.first);
              if (it == state.multisigStates.end()) {
                return;
              }

              confirmTransaction(
                  MultisigConfirmTransactionInvoice{
                      .publicKey = it->second.publicKey,
                      .multisigAddress = confirmation.first,
                      .transactionId = confirmation.second,
                  },
                  [=](InvoiceField) {}, _multisigConfirmationGuard);
            },
            _info->lifetime());

  _info->viewRequests() |
      rpl::start_with_next(
          [=](Ton::Transaction &&data) {
            const auto selectedAsset = _selectedAsset.current().value_or(SelectedToken::defaultToken());

            v::match(
                selectedAsset,
                [&](const SelectedToken &selectedToken) {
                  const auto send = [=](const QString &address) {
                    if (selectedToken.symbol.isTon()) {
                      sendMoney(TonTransferInvoice{
                          .address = address,
                      });
                    } else {
                      sendTokens(TokenTransferInvoice{
                          .token = selectedToken.symbol,
                          .ownerAddress = address,
                          .address = address,
                      });
                    }
                  };

                  auto resolveOwner = crl::guard(this, [=](const QString &wallet, const Fn<void(QString &&)> &done) {
                    _wallet->getWalletOwner(selectedToken.symbol.rootContractAddress(), wallet,
                                            crl::guard(this, [=](Ton::Result<QString> result) {
                                              if (result.has_value()) {
                                                return done(std::move(result.value()));
                                              }
                                            }));
                  });

                  auto collect = crl::guard(this, [=](const QString &eventAddress) { collectTokens(eventAddress); });

                  auto execute = crl::guard(
                      this, [=](const QString &eventAddress) { _wallet->openGateExecuteSwapBack(eventAddress); });

                  _layers->showBox(Box(
                      ViewTransactionBox, std::move(data), selectedToken.symbol, _collectEncryptedRequests.events(),
                      _decrypted.events(), shareAddressCallback(),
                      [=](const QString &transactionHash) { openInExplorer(transactionHash); },
                      [=] { decryptEverything(publicKey); }, resolveOwner, send, collect, execute));
                },
                [&](const SelectedDePool &selectedDePool) {
                  _layers->showBox(Box(ViewDePoolTransactionBox, std::move(data), shareAddressCallback()));
                },
                [&](const SelectedMultisig &selectedMultisig) {
                  _layers->showBox(Box(
                      ViewTransactionBox, std::move(data), Ton::Symbol::ton(), _collectEncryptedRequests.events(),
                      _decrypted.events(), shareAddressCallback(),
                      [=](const QString &transactionHash) { openInExplorer(transactionHash); },
                      [=] { decryptEverything(publicKey); },
                      /*resolveOwner*/ [](const QString &, const Fn<void(QString &&)> &) {},
                      [=](const QString &address) {
                        sendMoney(TonTransferInvoice{
                            .address = address,
                        });
                      },
                      /*collect*/ [](const QString &) {}, /*execute*/ [](const QString &) {}));
                });
          },
          _info->lifetime());

  _info->decryptRequests() | rpl::start_with_next([=] { decryptEverything(publicKey); }, _info->lifetime());

  _wallet->updates()                                                                                           //
      | rpl::filter([](const Ton::Update &update) { return v::is<Ton::DecryptPasswordNeeded>(update.data); })  //
      | rpl::start_with_next(
            [=](const Ton::Update &update) { askDecryptPassword(v::get<Ton::DecryptPasswordNeeded>(update.data)); },
            _info->lifetime());

  _wallet->updates()                                                                                         //
      | rpl::filter([](const Ton::Update &update) { return v::is<Ton::DecryptPasswordGood>(update.data); })  //
      | rpl::start_with_next(
            [=](const Ton::Update &update) { doneDecryptPassword(v::get<Ton::DecryptPasswordGood>(update.data)); },
            _info->lifetime());
}

void Window::decryptEverything(const QByteArray &publicKey) {
  auto transactions = std::vector<Ton::Transaction>();
  _collectEncryptedRequests.fire(&transactions);
  if (transactions.empty()) {
    return;
  }
  const auto done = [=](const Ton::Result<std::vector<Ton::Transaction>> &result) {
    if (!result) {
      showGenericError(result.error());
      return;
    }
    _decrypted.fire(&result.value());
  };
  _wallet->decrypt(publicKey, std::move(transactions), crl::guard(this, done));
}

void Window::askDecryptPassword(const Ton::DecryptPasswordNeeded &data) {
  const auto key = data.publicKey;
  const auto generation = data.generation;
  const auto already = (_decryptPasswordState && _decryptPasswordState->box) ? _decryptPasswordState->generation : 0;
  if (already == generation) {
    return;
  } else if (!_decryptPasswordState) {
    _decryptPasswordState = std::make_unique<DecryptPasswordState>();
  }
  _decryptPasswordState->generation = generation;
  if (!_decryptPasswordState->box) {
    auto box = Box(EnterPasscodeBox, ph::lng_wallet_keystore_main_wallet_key(ph::now),
                   [=](const QByteArray &passcode, Fn<void(QString)> showError) {
                     _decryptPasswordState->showError = showError;
                     _wallet->updateViewersPassword(key, passcode);
                   });
    QObject::connect(box, &QObject::destroyed, [=] {
      if (!_decryptPasswordState->success) {
        _wallet->updateViewersPassword(key, QByteArray());
      }
      _decryptPasswordState = nullptr;
    });
    _decryptPasswordState->box = box.data();
    _layers->showBox(std::move(box));
  } else if (_decryptPasswordState->showError) {
    _decryptPasswordState->showError(ph::lng_wallet_passcode_incorrect(ph::now));
  }
}

void Window::doneDecryptPassword(const Ton::DecryptPasswordGood &data) {
  if (_decryptPasswordState && _decryptPasswordState->generation < data.generation) {
    _decryptPasswordState->success = true;
    _decryptPasswordState->box->closeBox();
  }
}

void Window::openInExplorer(const QString &transactionHash) {
  auto url = QUrl(kExplorerPath).resolved(transactionHash);
  QDesktopServices::openUrl(url);
}

void Window::setupUpdateWithInfo() {
  Expects(_info != nullptr);

  rpl::combine(_window->body()->sizeValue(), _updateButtonHeight.events() | rpl::flatten_latest())  //
      | rpl::start_with_next(
            [=](QSize size, int height) {
              _info->setGeometry({0, 0, size.width(), size.height() - height});
              if (height > 0) {
                _updateButton->setGeometry(0, size.height() - height, size.width(), height);
              }
            },
            _info->lifetime());

  if (!_updateInfo) {
    _updateButtonHeight.fire(rpl::single(0));
    return;
  }

  rpl::merge(rpl::single(rpl::empty_value()), _updateInfo->isLatest(), _updateInfo->failed(), _updateInfo->ready())  //
      | rpl::start_with_next(
            [=] {
              if (_updateInfo->state() == UpdateState::Ready) {
                if (_updateButton) {
                  return;
                }
                _updateButton.create(_window->body(), ph::lng_wallet_update(ph::now).toUpper(), st::walletUpdateButton);
                _updateButton->show();
                _updateButton->setClickedCallback([=] { _updateInfo->install(); });
                _updateButtonHeight.fire(_updateButton->heightValue());

                _layers->raise();
              } else {
                _updateButtonHeight.fire(rpl::single(0));
                if (!_updateButton) {
                  return;
                }
                _updateButton.destroy();
              }
            },
            _info->lifetime());
}

void Window::setupRefreshEach() {
  Expects(_viewer != nullptr);
  Expects(_info != nullptr);

  const auto basedOnActivity =
      _viewer->state()  //
      | rpl::map([] {
          return (base::SinceLastUserInput() > kRefreshEachDelay) ? kRefreshInactiveDelay : kRefreshEachDelay;
        });

  const auto basedOnWindowActive =
      rpl::single(rpl::empty_value()) |
      rpl::then(base::qt_signal_producer(_window->windowHandle(), &QWindow::activeChanged)) |
      rpl::map([=]() -> rpl::producer<crl::time> {
        if (!_window->isActiveWindow()) {
          return rpl::single(kRefreshInactiveDelay);
        }
        return rpl::duplicate(basedOnActivity);
      }) |
      rpl::flatten_latest();

  const auto basedOnPending =
      _viewer->state()                                                                                            //
      | rpl::map([=](const Ton::WalletViewerState &state) { return !state.wallet.pendingTransactions.empty(); })  //
      | rpl::distinct_until_changed() | rpl::map([=](bool hasPending) -> rpl::producer<crl::time> {
          if (hasPending) {
            return rpl::single(kRefreshWhileSendingDelay);
          }
          return rpl::duplicate(basedOnWindowActive);
        })  //
      | rpl::flatten_latest();

  rpl::duplicate(basedOnPending) | rpl::distinct_until_changed() |
      rpl::start_with_next([=](crl::time delay) { _viewer->setRefreshEach(delay); }, _info->lifetime());
}

void Window::showAndActivate() {
  _window->show();
  base::Platform::ActivateThisProcessWindow(_window->winId());
  _window->activateWindow();
  if (_createManager) {
    _createManager->setFocus();
  } else {
    _window->setFocus();
  }
}

not_null<Ui::RpWidget *> Window::widget() const {
  return _window.get();
}

bool Window::handleLinkOpen(const QString &link) {
  if (_viewer && ValidateTransferLink(link)) {
    sendMoney(link);
  }
  return true;
}

void Window::showConfigUpgrade(Ton::ConfigUpgrade upgrade) {
  if (upgrade == Ton::ConfigUpgrade::TestnetToTestnet2) {
    const auto message =
        "The TON test network has been reset.\n"
        "TON testnet2 is now operational.";
    showSimpleError(ph::lng_wallet_warning(), rpl::single(QString(message)), ph::lng_wallet_ok());
  } else if (upgrade == Ton::ConfigUpgrade::TestnetToMainnet) {
    const auto message =
        "The Gram Wallet has switched "
        "from the testing to the main network.\n\nIn case you want "
        "to perform more testing you can switch back "
        "to the Test Gram network in Settings "
        "and reconnect your wallet using 24 secret words.";
    showSimpleError(ph::lng_wallet_warning(), rpl::single(QString(message)), ph::lng_wallet_ok());
  }
}

void Window::sendMoney(const PreparedInvoiceOrLink &invoice) {
  if (_sendConfirmBox) {
    _sendConfirmBox->closeBox();
  }
  if (_sendBox) {
    _sendBox->closeBox();
  }
  if (!_state.current().pendingTransactions.empty()) {
    showSimpleError(ph::lng_wallet_warning(), ph::lng_wallet_wait_pending(), ph::lng_wallet_ok());
    return;
  } else if (_syncing.current()) {
    showSimpleError(ph::lng_wallet_warning(), ph::lng_wallet_wait_syncing(), ph::lng_wallet_ok());
    return;
  }

  const auto defaultToken = Ton::Symbol::ton();

  auto available = [=](const Ton::Symbol &symbol) -> int128 {
    const auto currentState = _state.current();
    const auto account = currentState.account;

    if (symbol.isTon()) {
      return account.fullBalance - account.lockedBalance;
    } else {
      auto it = currentState.tokenStates.find(symbol);
      if (it != currentState.tokenStates.end()) {
        return it->second.balance;
      } else {
        return int128{};
      }
    }
  };

  PreparedInvoice parsedInvoice = v::match(
      invoice, [](const PreparedInvoice &invoice) { return invoice; },
      [](const QString &link) { return ParseInvoice(link); });

  const auto checking = std::make_shared<bool>();
  auto box = v::match(
      parsedInvoice,
      [=](const TonTransferInvoice &tonTransferInvoice) {
        const auto send = [=](const TonTransferInvoice &finalInvoice, const Fn<void(InvoiceField)> &showError) {
          if (!Ton::Wallet::CheckAddress(finalInvoice.address)) {
            showError(InvoiceField::Address);
          } else if (finalInvoice.amount > available(defaultToken) || finalInvoice.amount <= 0) {
            showError(InvoiceField::Amount);
          } else {
            confirmTransaction(finalInvoice, showError, checking);
          }
        };

        return Box(SendGramsBox<TonTransferInvoice>, tonTransferInvoice, _state.value(), send);
      },
      [=](TokenTransferInvoice &tokenTransferInvoice) {
        const auto send = [=](const TokenTransferInvoice &finalInvoice, const Fn<void(InvoiceField)> &showError) {
          if (finalInvoice.transferType != Ton::TokenTransferType::SwapBack &&
              !Ton::Wallet::CheckAddress(finalInvoice.address)) {
            showError(InvoiceField::Address);
          } else if (finalInvoice.amount > available(finalInvoice.token) || finalInvoice.amount <= 0) {
            showError(InvoiceField::Amount);
          } else if (finalInvoice.transferType == Ton::TokenTransferType::SwapBack &&
                     !Ton::Wallet::CheckAddress(finalInvoice.callbackAddress)) {
            showError(InvoiceField::CallbackAddress);
          } else {
            confirmTransaction(finalInvoice, showError, checking);
          }
        };

        return Box(SendGramsBox<TokenTransferInvoice>, tokenTransferInvoice, _state.value(), send);
      },
      [=](MultisigSubmitTransactionInvoice &invoice) {
        const auto send = [=](const MultisigSubmitTransactionInvoice &invoice,
                              const Fn<void(InvoiceField)> &showError) {
          if (!Ton::Wallet::CheckAddress(invoice.address)) {
            return showError(InvoiceField::Address);
          }

          const auto state = _state.current();
          const auto it = state.multisigStates.find(invoice.multisigAddress);
          if (invoice.amount <= 0 || it == state.multisigStates.end() ||
              invoice.amount > (it->second.accountState.fullBalance - it->second.accountState.lockedBalance)) {
            return showError(InvoiceField::Amount);
          }

          confirmTransaction(invoice, showError, checking);
        };

        return Box(SendGramsBox<MultisigSubmitTransactionInvoice>, invoice, _state.value(), send);
      },
      [=](auto &&) -> object_ptr<Ui::GenericBox> { return nullptr; });

  if (box != nullptr) {
    _sendBox = box.data();
    _layers->showBox(std::move(box));
  }
}

void Window::sendTokens(TokenTransferInvoice &&invoice) {
  const auto state = _state.current();
  const auto it = state.tokenStates.find(invoice.token);
  if (it == state.tokenStates.end()) {
    return;
  }

  invoice.version = it->second.version;
  invoice.callbackAddress = it->second.proxyAddress;

  sendMoney(std::move(invoice));
}

void Window::sendStake(const StakeInvoice &invoice) {
  if (_sendBox) {
    _sendBox->closeBox();
  }

  const auto checking = std::make_shared<bool>();
  const auto send = [=](const StakeInvoice &invoice, const Fn<void(StakeInvoiceField)> &showError) {
    const auto currentState = _state.current();
    const auto account = currentState.account;

    int64_t available = account.fullBalance - account.lockedBalance;

    if (invoice.stake > available || invoice.stake <= 0) {
      showError(StakeInvoiceField::Amount);
    } else {
      confirmTransaction(
          invoice,
          [=](InvoiceField field) {
            if (field == InvoiceField::Amount) {
              showError(StakeInvoiceField::Amount);
            }
          },
          checking);
    }
  };

  auto box = Box(SendStakeBox, invoice, _state.value(), send);
  _sendBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::dePoolWithdraw(const WithdrawalInvoice &invoice) {
  if (_sendBox) {
    _sendBox->closeBox();
  }

  const auto checking = std::make_shared<bool>();
  const auto send = [this, checking](const WithdrawalInvoice &invoice, const Fn<void(DePoolWithdrawField)> &showError) {
    const auto currentState = _state.current();
    int64 total = 0;

    const auto it = currentState.dePoolParticipantStates.find(invoice.dePool);
    if (it != currentState.dePoolParticipantStates.end()) {
      total = it->second.total;
    }

    if (!invoice.all && (invoice.amount > total || invoice.amount <= 0)) {
      showError(DePoolWithdrawField::Amount);
    } else {
      confirmTransaction(
          invoice,
          [=](InvoiceField field) {
            if (field == InvoiceField::Amount) {
              showError(DePoolWithdrawField::Amount);
            }
          },
          checking);
    }
  };

  auto box = Box(DePoolWithdrawBox, invoice, _state.value(), send);
  _sendBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::dePoolCancelWithdrawal(const CancelWithdrawalInvoice &invoice) {
  if (_sendBox) {
    _sendBox->closeBox();
  }

  const auto checking = std::make_shared<bool>();
  const auto send = [this, checking](const CancelWithdrawalInvoice &invoice) {
    confirmTransaction(
        invoice, [=](InvoiceField) {}, checking);
  };

  auto box = Box(DePoolCancelWithdrawalBox, invoice, send);
  _sendBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::deployTokenWallet(const DeployTokenWalletInvoice &invoice) {
  if (_sendBox) {
    _sendBox->closeBox();
  }

  const auto checking = std::make_shared<bool>();
  const auto send = [this, checking](const DeployTokenWalletInvoice &invoice) {
    confirmTransaction(
        invoice, [=](InvoiceField) {}, checking);
  };

  auto box = Box(DeployTokenWalletBox, invoice, send);
  _sendBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::collectTokens(const QString &eventContractAddress) {
  if (_sendBox) {
    _sendBox->closeBox();
  }

  const auto checking = std::make_shared<bool>();
  const auto send = [this, checking](const CollectTokensInvoice &invoice) {
    confirmTransaction(
        invoice, [=](InvoiceField) {}, checking);
  };

  auto ethEventDetails = std::make_shared<rpl::event_stream<Ton::Result<Ton::EthEventDetails>>>();
  auto symbolEvents = std::make_shared<rpl::event_stream<Ton::Symbol>>();

  _wallet->getEthEventDetails(  //
      eventContractAddress, crl::guard(this, [=](Ton::Result<Ton::EthEventDetails> details) {
        if (details.has_value() && !details->rootTokenContract.isEmpty()) {
          const auto &rootTokenContract = details->rootTokenContract;
          const auto state = _state.current();
          auto found = false;
          for (const auto &item : state.tokenStates) {
            if (item.first.rootContractAddress() == rootTokenContract) {
              found = true;
              symbolEvents->fire_copy(item.first);
              break;
            }
          }
          if (!found) {
            _wallet->getRootTokenContractDetails(
                rootTokenContract, crl::guard(this, [=](Ton::Result<Ton::RootTokenContractDetails> details) {
                  if (details.has_value()) {
                    symbolEvents->fire(Ton::Symbol::tip3(details->symbol, details->decimals, rootTokenContract));
                  }
                }));
          }
        }
        ethEventDetails->fire(std::move(details));
      }));

  auto box = Box(CollectTokensBox, CollectTokensInvoice{.eventContractAddress = eventContractAddress},
                 ethEventDetails->events(), symbolEvents->events(), shareAddressCallback(), send);
  _sendBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::confirmTransaction(PreparedInvoice invoice, const Fn<void(InvoiceField)> &showInvoiceError,
                                const std::shared_ptr<bool> &guard) {
  if (*guard) {
    return;
  }

  const auto withoutBox = v::match(
      invoice,                                                         //
      [](const MultisigConfirmTransactionInvoice &) { return true; },  //
      [](const UpgradeTokenWalletInvoice &) { return true; },          //
      [](auto &&) { return false; });
  if (!withoutBox && !_sendBox) {
    return;
  }
  *guard = true;

  v::match(
      invoice,
      [&](TonTransferInvoice &tonTransferInvoice) {
        // stay same
      },
      [&](TokenTransferInvoice &tokenTransferInvoice) {
        const auto state = _state.current();
        const auto it = state.tokenStates.find(tokenTransferInvoice.token);
        if (it != state.tokenStates.end()) {
          tokenTransferInvoice.rootContractAddress = it->first.rootContractAddress();
          tokenTransferInvoice.walletContractAddress = it->second.walletContractAddress;
        }
        tokenTransferInvoice.realAmount = Ton::TokenTransactionToSend::realAmount;
      },
      [&](StakeInvoice &stakeInvoice) {
        stakeInvoice.realAmount = stakeInvoice.stake + Ton::StakeTransactionToSend::depoolFee;
      },
      [&](WithdrawalInvoice &withdrawalInvoice) {
        withdrawalInvoice.realAmount = Ton::WithdrawalTransactionToSend::depoolFee;
      },
      [&](CancelWithdrawalInvoice &cancelWithdrawalInvoice) {
        cancelWithdrawalInvoice.realAmount = Ton::CancelWithdrawalTransactionToSend::depoolFee;
      },
      [&](DeployTokenWalletInvoice &deployTokenWalletInvoice) {
        deployTokenWalletInvoice.realAmount = Ton::DeployTokenWalletTransactionToSend::realAmount;
      },
      [&](UpgradeTokenWalletInvoice &upgradeTokenWalletInvoice) {
        upgradeTokenWalletInvoice.realAmount = Ton::UpgradeTokenWalletTransactionToSend::realAmount;
      },
      [&](CollectTokensInvoice &collectTokensInvoice) {
        collectTokensInvoice.realAmount = Ton::CollectTokensTransactionToSend::realAmount;
      },
      [&](auto &&) {});

  auto handleCheckError = [=](Ton::Result<Ton::TransactionCheckResult> &&result) {
    if (const auto field = ErrorInvoiceField(result.error())) {
      return showInvoiceError(*field);
    }
    return showGenericError(result.error());
  };

  auto done = [=](Ton::Result<Ton::TransactionCheckResult> result, PreparedInvoice &&invoice) mutable {
    *guard = false;
    if (!result.has_value()) {
      return handleCheckError(std::move(result));
    }
    showSendConfirmation(invoice, *result, showInvoiceError);
  };

  auto doneUnchanged = [=](const Ton::Result<Ton::TransactionCheckResult> &result) mutable {
    done(result, std::move(invoice));
  };

  auto doneSelectMultisigKey = [=](Ton::Result<Ton::TransactionCheckResult> &&result) mutable {
    *guard = false;
    if (!result.has_value()) {
      return handleCheckError(std::move(result));
    }
    auto address = v::match(
        invoice, [](const MultisigSubmitTransactionInvoice &invoice) { return invoice.multisigAddress; },
        [](const MultisigConfirmTransactionInvoice &invoice) { return invoice.multisigAddress; },
        [](auto &&) { return QString{}; });
    if (address.isEmpty()) {
      return;
    }
    const auto states = _state.current().multisigStates;
    const auto it = states.find(address);
    if (it == states.end()) {
      return;
    }
    auto keySelectedGuard = std::make_shared<bool>(false);
    selectMultisigKey(  //
        it->second.custodians, 0, false, [=, invoice = std::move(invoice)](const QByteArray &publicKey) mutable {
          if (std::exchange(*keySelectedGuard, true)) {
            return;
          }
          if (_keySelectionBox) {
            _keySelectionBox->closeBox();
          }
          v::match(
              invoice, [&](MultisigSubmitTransactionInvoice &invoice) { invoice.publicKey = publicKey; },
              [&](MultisigConfirmTransactionInvoice &invoice) { invoice.publicKey = publicKey; }, [](auto &&) {});
          showSendConfirmation(invoice, *result, showInvoiceError);
        });
  };

  v::match(
      invoice,
      [&](const TonTransferInvoice &tonTransferInvoice) {
        _wallet->checkSendGrams(getMainPublicKey(), tonTransferInvoice.asTransaction(),
                                crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const TokenTransferInvoice &tokenTransferInvoice) {
        auto tokenHandler =
            [this, guard, showInvoiceError, done, invoice = tokenTransferInvoice](
                Ton::Result<std::pair<Ton::TransactionCheckResult, Ton::TokenTransferCheckResult>> result) mutable {
              if (!result.has_value()) {
                done(result.error(), std::move(invoice));
              } else {
                v::match(
                    result.value().second,
                    [&](const Ton::InvalidEthAddress &) {
                      *guard = false;
                      showInvoiceError(InvoiceField::Address);
                    },
                    [&](const Ton::TokenTransferUnchanged &) {
                      done(std::move(result.value().first), std::move(invoice));
                    },
                    [&](const Ton::DirectAccountNotFound &) {
                      *guard = false;
                      showToast(ph::lng_wallet_send_tokens_recipient_not_found(ph::now));
                      showInvoiceError(InvoiceField::Address);
                    },
                    [&](const Ton::DirectRecipient &directRecipient) {
                      invoice.transferType = Ton::TokenTransferType::Direct;
                      invoice.address = directRecipient.address;
                      showToast(ph::lng_wallet_send_tokens_recipient_changed(ph::now));
                      done(std::move(result.value().first), std::move(invoice));
                    });
              };
            };

        _wallet->checkSendTokens(getMainPublicKey(), tokenTransferInvoice.asTransaction(),
                                 crl::guard(_sendBox.data(), std::move(tokenHandler)));
      },
      [&](const StakeInvoice &stakeInvoice) {
        _wallet->checkSendStake(getMainPublicKey(), stakeInvoice.asTransaction(),
                                crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const WithdrawalInvoice &withdrawalInvoice) {
        _wallet->checkWithdraw(getMainPublicKey(), withdrawalInvoice.asTransaction(),
                               crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const CancelWithdrawalInvoice &cancelWithdrawalInvoice) {
        _wallet->checkCancelWithdraw(getMainPublicKey(), cancelWithdrawalInvoice.asTransaction(),
                                     crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const DeployTokenWalletInvoice &deployTokenWalletInvoice) {
        _wallet->checkDeployTokenWallet(getMainPublicKey(), deployTokenWalletInvoice.asTransaction(),
                                        crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const UpgradeTokenWalletInvoice &upgradeTokenWalletInvoice) {
        _wallet->checkUpgradeTokenWallet(getMainPublicKey(), upgradeTokenWalletInvoice.asTransaction(),
                                         crl::guard(this, doneUnchanged));
      },
      [&](const CollectTokensInvoice &collectTokensInvoice) {
        _wallet->checkCollectTokens(getMainPublicKey(), collectTokensInvoice.asTransaction(),
                                    crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const MultisigDeployInvoice &invoice) {
        _wallet->checkDeployMultisig(invoice.asTransaction(), crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const MultisigSubmitTransactionInvoice &invoice) {
        _wallet->checkSubmitTransaction(invoice.asTransaction(), crl::guard(_sendBox.data(), doneSelectMultisigKey));
      },
      [&](const MultisigConfirmTransactionInvoice &invoice) {
        _wallet->checkConfirmTransaction(invoice.asTransaction(), crl::guard(this, doneSelectMultisigKey));
      });
}

void Window::askSendPassword(const PreparedInvoice &invoice, const Fn<void(InvoiceField)> &showInvoiceError) {
  const auto mainPublicKey = getMainPublicKey();
  const auto sending = std::make_shared<bool>();
  const auto ready = [=](const QByteArray &passcode, const PreparedInvoice &invoice, Fn<void(QString)> showError) {
    if (*sending) {
      return;
    }
    const auto confirmations = std::make_shared<rpl::event_stream<>>();
    *sending = true;
    auto ready = [=, showError = std::move(showError)](Ton::Result<Ton::PendingTransaction> result) {
      if (!result && IsIncorrectPasswordError(result.error())) {
        *sending = false;
        showError(ph::lng_wallet_passcode_incorrect(ph::now));
        return;
      }
      if (_sendConfirmBox) {
        _sendConfirmBox->closeBox();
      }
      if (!result) {
        if (const auto field = ErrorInvoiceField(result.error())) {
          showInvoiceError(*field);
        } else {
          showGenericError(result.error());
        }
        return;
      }
      showSendingTransaction(*result, invoice, confirmations->events());

      v::match(
          invoice,                                           //
          [](const MultisigSubmitTransactionInvoice &) {},   //
          [](const MultisigConfirmTransactionInvoice &) {},  //
          [&](auto &&) {
            _wallet->updateViewersPassword(mainPublicKey, passcode);
            decryptEverything(mainPublicKey);
          });
    };
    const auto sent = [=](Ton::Result<> result) {
      if (!result) {
        showSendingError(result.error());
        return;
      }
      confirmations->fire({});
    };

    v::match(
        invoice,
        [&](const TonTransferInvoice &tonTransferInvoice) {
          _wallet->sendGrams(mainPublicKey, passcode, tonTransferInvoice.asTransaction(), crl::guard(this, ready),
                             crl::guard(this, sent));
        },
        [&](const TokenTransferInvoice &tokenTransferInvoice) {
          _wallet->sendTokens(mainPublicKey, passcode, tokenTransferInvoice.asTransaction(), crl::guard(this, ready),
                              crl::guard(this, sent));
        },
        [&](const StakeInvoice &stakeInvoice) {
          _wallet->sendStake(mainPublicKey, passcode, stakeInvoice.asTransaction(), crl::guard(this, ready),
                             crl::guard(this, sent));
        },
        [&](const WithdrawalInvoice &withdrawalInvoice) {
          _wallet->withdraw(mainPublicKey, passcode, withdrawalInvoice.asTransaction(), crl::guard(this, ready),
                            crl::guard(this, sent));
        },
        [&](const CancelWithdrawalInvoice &cancelWithdrawalInvoice) {
          _wallet->cancelWithdrawal(mainPublicKey, passcode, cancelWithdrawalInvoice.asTransaction(),
                                    crl::guard(this, ready), crl::guard(this, sent));
        },
        [&](const DeployTokenWalletInvoice &deployTokenWalletInvoice) {
          _wallet->deployTokenWallet(mainPublicKey, passcode, deployTokenWalletInvoice.asTransaction(),
                                     crl::guard(this, ready), crl::guard(this, sent));
        },
        [&](const UpgradeTokenWalletInvoice &upgradeTokenWalletInvoice) {
          _wallet->upgradeTokenWallet(mainPublicKey, passcode, upgradeTokenWalletInvoice.asTransaction(),
                                      crl::guard(this, ready), crl::guard(this, sent));
        },
        [&](const CollectTokensInvoice &collectTokensInvoice) {
          _wallet->collectTokens(mainPublicKey, passcode, collectTokensInvoice.asTransaction(), crl::guard(this, ready),
                                 crl::guard(this, sent));
        },
        [&](const MultisigDeployInvoice &invoice) {
          _wallet->deployMultisig(mainPublicKey, passcode, invoice.asTransaction(), crl::guard(this, ready),
                                  crl::guard(this, sent));
        },
        [&](const MultisigSubmitTransactionInvoice &invoice) {
          _wallet->submitTransaction(mainPublicKey, passcode, invoice.asTransaction(), crl::guard(this, ready),
                                     crl::guard(this, sent));
        },
        [&](const MultisigConfirmTransactionInvoice &invoice) {
          _wallet->confirmTransaction(mainPublicKey, passcode, invoice.asTransaction(), crl::guard(this, ready),
                                      crl::guard(this, sent));
        });
  };
  if (_sendConfirmBox) {
    _sendConfirmBox->closeBox();
  }

  auto passcodePublicKey = mainPublicKey;
  v::match(
      invoice, [&](const MultisigDeployInvoice &invoice) { passcodePublicKey = invoice.initialInfo.publicKey; },
      [&](const MultisigSubmitTransactionInvoice &invoice) { passcodePublicKey = invoice.publicKey; },
      [&](const MultisigConfirmTransactionInvoice &invoice) { passcodePublicKey = invoice.publicKey; }, [](auto &&) {});

  auto existingKeys = getExistingKeys();
  const auto it = existingKeys.find(passcodePublicKey);
  if (it == existingKeys.end()) {
    return showKeyNotFound();
  }

  auto box = Box(EnterPasscodeBox, it->second.name, [=](const QByteArray &passcode, Fn<void(QString)> showError) {
    ready(passcode, invoice, std::move(showError));
  });
  _sendConfirmBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::showSendConfirmation(const PreparedInvoice &invoice, const Ton::TransactionCheckResult &checkResult,
                                  const Fn<void(InvoiceField)> &showInvoiceError) {
  const auto currentState = _state.current();
  const auto account = currentState.account;
  const auto gramsAvailable = account.fullBalance - account.lockedBalance;

  //if (invoice.amount == available && account.lockedBalance == 0) {
  //    // Special case transaction where we transfer all that is left.
  //} else

  const auto sourceFees = checkResult.sourceFees.sum();

  const auto checkAmount = [&](int64_t realAmount) { return gramsAvailable > realAmount + sourceFees; };

  const auto checkAmountByState = [&](int64_t realAmount, const Ton::AccountState &accountState) {
    const auto gramsAvailable = accountState.fullBalance - accountState.lockedBalance;
    return gramsAvailable > realAmount + sourceFees;
  };

  auto box = v::match(
      invoice,
      [&](const TonTransferInvoice &tonTransferInvoice) -> object_ptr<Ui::GenericBox> {
        if (!checkAmount(tonTransferInvoice.amount)) {
          showInvoiceError(InvoiceField::Amount);
          return nullptr;
        }

        return Box(
            ConfirmTransactionBox<TonTransferInvoice>, tonTransferInvoice, sourceFees,
            [=, targetAddress = tonTransferInvoice.address] {
              if (targetAddress == _packedAddress) {
                _layers->showBox(Box([=](not_null<Ui::GenericBox *> box) {
                  box->setTitle(ph::lng_wallet_same_address_title());
                  box->addRow(object_ptr<Ui::FlatLabel>(box, ph::lng_wallet_same_address_text(), st::walletLabel));
                  box->addButton(ph::lng_wallet_same_address_proceed(), [=] {
                    box->closeBox();
                    askSendPassword(tonTransferInvoice, showInvoiceError);
                  });
                  box->addButton(ph::lng_wallet_cancel(), [=] {
                    box->closeBox();
                    if (_sendConfirmBox) {
                      _sendConfirmBox->closeBox();
                    }
                  });
                }));
              } else {
                askSendPassword(tonTransferInvoice, showInvoiceError);
              }
            });
      },
      [&](const TokenTransferInvoice &tokenTransferInvoice) -> object_ptr<Ui::GenericBox> {
        if (!checkAmount(tokenTransferInvoice.realAmount)) {
          showInvoiceError(InvoiceField::Amount);
          return nullptr;
        }

        return Box(ConfirmTransactionBox<TokenTransferInvoice>, tokenTransferInvoice,
                   tokenTransferInvoice.realAmount + sourceFees,
                   [=] { askSendPassword(tokenTransferInvoice, showInvoiceError); });
      },
      [&](const StakeInvoice &stakeInvoice) -> object_ptr<Ui::GenericBox> {
        if (!checkAmount(stakeInvoice.realAmount)) {
          showInvoiceError(InvoiceField::Amount);
          return nullptr;
        }

        return Box(ConfirmTransactionBox<StakeInvoice>, stakeInvoice,
                   stakeInvoice.realAmount - stakeInvoice.stake + sourceFees,
                   [=] { askSendPassword(stakeInvoice, showInvoiceError); });
      },
      [&](const WithdrawalInvoice &withdrawalInvoice) -> object_ptr<Ui::GenericBox> {
        if (!checkAmount(withdrawalInvoice.realAmount)) {
          showInvoiceError(InvoiceField::Amount);
          return nullptr;
        }

        return Box(ConfirmTransactionBox<WithdrawalInvoice>, withdrawalInvoice,
                   withdrawalInvoice.realAmount + sourceFees,
                   [=] { askSendPassword(withdrawalInvoice, showInvoiceError); });
      },
      [&](const CancelWithdrawalInvoice &cancelWithdrawalInvoice) -> object_ptr<Ui::GenericBox> {
        if (!checkAmount(cancelWithdrawalInvoice.realAmount)) {
          showInvoiceError(InvoiceField::Amount);
          return nullptr;
        }

        return Box(ConfirmTransactionBox<CancelWithdrawalInvoice>, cancelWithdrawalInvoice,
                   cancelWithdrawalInvoice.realAmount + sourceFees,
                   [=] { askSendPassword(cancelWithdrawalInvoice, showInvoiceError); });
      },
      [&](const DeployTokenWalletInvoice &deployTokenWalletInvoice) -> object_ptr<Ui::GenericBox> {
        if (!checkAmount(deployTokenWalletInvoice.realAmount)) {
          showInvoiceError(InvoiceField::Amount);
          return nullptr;
        }

        return Box(ConfirmTransactionBox<DeployTokenWalletInvoice>, deployTokenWalletInvoice,
                   deployTokenWalletInvoice.realAmount + sourceFees,
                   [=] { askSendPassword(deployTokenWalletInvoice, showInvoiceError); });
      },
      [&](const UpgradeTokenWalletInvoice &upgradeTokenWalletInvoice) -> object_ptr<Ui::GenericBox> {
        if (!checkAmount(upgradeTokenWalletInvoice.realAmount)) {
          showInvoiceError(InvoiceField::Amount);
          showSimpleError(ph::lng_wallet_send_failed_title(), ph::lng_wallet_send_failed_text(),
                          ph::lng_wallet_continue());
          return nullptr;
        }

        std::cout << "Show confirmation?" << std::endl;

        return Box(ConfirmTransactionBox<UpgradeTokenWalletInvoice>, upgradeTokenWalletInvoice,
                   upgradeTokenWalletInvoice.realAmount + sourceFees,
                   [=] { askSendPassword(upgradeTokenWalletInvoice, showInvoiceError); });
      },
      [&](const CollectTokensInvoice &collectTokensInvoice) -> object_ptr<Ui::GenericBox> {
        if (!checkAmount(collectTokensInvoice.realAmount)) {
          showInvoiceError(InvoiceField::Address);
          showSimpleError(ph::lng_wallet_send_failed_title(), ph::lng_wallet_send_failed_text(),
                          ph::lng_wallet_continue());
          return nullptr;
        }

        return Box(ConfirmTransactionBox<CollectTokensInvoice>, collectTokensInvoice,
                   collectTokensInvoice.realAmount + sourceFees,
                   [=] { askSendPassword(collectTokensInvoice, showInvoiceError); });
      },
      [&](const MultisigDeployInvoice &invoice) -> object_ptr<Ui::GenericBox> {
        return Box(ConfirmTransactionBox<MultisigDeployInvoice>, invoice, sourceFees,
                   [=] { askSendPassword(invoice, showInvoiceError); });
      },
      [&](const MultisigSubmitTransactionInvoice &invoice) -> object_ptr<Ui::GenericBox> {
        const auto it = currentState.multisigStates.find(invoice.multisigAddress);
        if (it == currentState.multisigStates.end() || !checkAmountByState(invoice.amount, it->second.accountState)) {
          showInvoiceError(InvoiceField::Address);
          showSimpleError(ph::lng_wallet_send_failed_title(), ph::lng_wallet_send_failed_text(),
                          ph::lng_wallet_continue());
          return nullptr;
        }

        return Box(ConfirmTransactionBox<MultisigSubmitTransactionInvoice>, invoice, sourceFees,
                   [=] { askSendPassword(invoice, showInvoiceError); });
      },
      [&](const MultisigConfirmTransactionInvoice &invoice) -> object_ptr<Ui::GenericBox> {
        const auto it = currentState.multisigStates.find(invoice.multisigAddress);
        if (it == currentState.multisigStates.end() || !checkAmountByState(0, it->second.accountState)) {
          showSimpleError(ph::lng_wallet_send_failed_title(), ph::lng_wallet_send_failed_text(),
                          ph::lng_wallet_continue());
          return nullptr;
        }

        return Box(ConfirmTransactionBox<MultisigConfirmTransactionInvoice>, invoice, sourceFees,
                   [=] { askSendPassword(invoice, showInvoiceError); });
      });

  if (box != nullptr) {
    _sendConfirmBox = box.data();
    _layers->showBox(std::move(box));
  }
}

void Window::showSendingTransaction(const Ton::PendingTransaction &transaction, const PreparedInvoice &invoice,
                                    rpl::producer<> confirmed) {
  if (_sendBox) {
    _sendBox->closeBox();
  }

  const auto token = v::match(
      invoice, [](const TokenTransferInvoice &tokenTransferInvoice) { return tokenTransferInvoice.token; },
      [](auto &&) { return Ton::Symbol::ton(); });

  auto box = Box(SendingTransactionBox, token, confirmed);

  _sendBox = box.data();

  const auto handleDefaultPending = [&] {
    _state.value()  //
        | rpl::filter([=](const Ton::WalletState &state) {
            return ranges::find(state.pendingTransactions, transaction) == end(state.pendingTransactions);
          })  //
        | rpl::map([=](const Ton::WalletState &state) {
            const auto i = ranges::find(state.lastTransactions.list, transaction.fake);
            return (i != end(state.lastTransactions.list)) ? std::make_optional(*i) : std::nullopt;
          })  //
        | rpl::start_with_next(
              [=](std::optional<Ton::Transaction> &&result) { showSendingDone(std::move(result), invoice); },
              _sendBox->lifetime());
  };

  auto justSent = std::make_shared<bool>(true);

  const auto handleMultisigPending = [&](const QString &address) {
    _state.value()  //
        | rpl::map([=](const Ton::WalletState &state) -> std::optional<Ton::MultisigState> {
            const auto it = state.multisigStates.find(address);
            if (it == end(state.multisigStates)) {
              return std::nullopt;
            }
            return ranges::find(it->second.pendingTransactions, transaction) == end(it->second.pendingTransactions)
                       ? std::make_optional(it->second)
                       : std::nullopt;
          })                      //
        | rpl::filter_optional()  //
        | rpl::map([=](const Ton::MultisigState &state) {
            const auto i = ranges::find(state.lastTransactions.list, transaction.fake);
            return (i != end(state.lastTransactions.list)) ? std::make_optional(*i) : std::nullopt;
          })  //
        | rpl::start_with_next(
              [=](std::optional<Ton::Transaction> &&result) {
                if (*justSent) {  // prevents the window from closing before transaction is really sent
                  return;
                }
                showSendingDone(std::move(result), invoice);
              },
              _sendBox->lifetime());
  };

  v::match(
      invoice, [&](const MultisigDeployInvoice &invoice) { handleMultisigPending(invoice.initialInfo.address); },
      [&](const MultisigSubmitTransactionInvoice &invoice) { handleMultisigPending(invoice.multisigAddress); },
      [&](const MultisigConfirmTransactionInvoice &invoice) { handleMultisigPending(invoice.multisigAddress); },
      [&](auto &&) { handleDefaultPending(); });

  _layers->showBox(std::move(box));
  *justSent = false;

  if (_sendConfirmBox) {
    _sendConfirmBox->closeBox();
  }
}

void Window::showSendingDone(std::optional<Ton::Transaction> result, const PreparedInvoice &invoice) {
  if (result) {
    const auto refresh = crl::guard(this, [this] { refreshNow(); });

    auto box = v::match(invoice, [&](const auto &invoice) {
      return Box(SendingDoneBox<std::decay_t<decltype(invoice)>>, *result, invoice, refresh);
    });

    if (box != nullptr) {
      _layers->showBox(std::move(box));
    }
  } else {
    showSimpleError(ph::lng_wallet_send_failed_title(), ph::lng_wallet_send_failed_text(), ph::lng_wallet_continue());
  }

  if (_sendBox) {
    _sendBox->closeBox();
  }
}

void Window::addAsset() {
  if (_sendBox) {
    _sendBox->closeBox();
  }

  const auto onNewDepool = [this](const Ton::Result<> &result) {
    if (result.has_value()) {
      refreshNow();
      showToast(ph::lng_wallet_add_depool_succeeded(ph::now));
    } else {
      showSimpleError(ph::lng_wallet_add_depool_failed_title(), ph::lng_wallet_add_depool_failed_text(),
                      ph::lng_wallet_continue());
    }
  };

  const auto onNewToken = [this](const Ton::Result<> &result) {
    if (result.has_value()) {
      refreshNow();
      showToast(ph::lng_wallet_add_token_succeeded(ph::now));
    } else {
      showSimpleError(ph::lng_wallet_add_token_failed_title(), ph::lng_wallet_add_token_failed_text(),
                      ph::lng_wallet_continue());
    }
  };

  const auto checking = std::make_shared<bool>();
  const auto send = [=](const NewAsset &newAsset) {
    if (*checking) {
      return;
    }
    *checking = true;

    switch (newAsset.type) {
      case CustomAssetType::DePool: {
        _wallet->addDePool(getMainPublicKey(), newAsset.address, false, crl::guard(this, onNewDepool));
        break;
      }
      case CustomAssetType::Token: {
        _wallet->addToken(getMainPublicKey(), newAsset.address, false, crl::guard(this, onNewToken));
        break;
      }
      case CustomAssetType::Multisig: {
        if (!newAsset.address.isEmpty()) {
          importMultisig(newAsset.address);
        } else {
          addNewMultisig();
        }
        break;
      }
      default:
        break;
    }

    _sendBox->closeBox();
  };

  auto box = Box(AddAssetBox, send);
  _sendBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::receiveTokens(RecipientWalletType type, const QString &address, const Ton::Symbol &symbol) {
  const auto rawAddress = Ton::Wallet::ConvertIntoRaw(address);

  _layers->showBox(Box(
      ReceiveTokensBox, type, rawAddress, symbol, [=] { createInvoice(symbol); }, shareAddressCallback(),
      [=] { _wallet->openGate(rawAddress, symbol); }));
}

void Window::createInvoice(const Ton::Symbol &selectedToken) {
  _layers->showBox(Box(
      CreateInvoiceBox, _packedAddress, _testnet, selectedToken, [=](const QString &link) { showInvoiceQr(link); },
      shareCallback(ph::lng_wallet_invoice_copied(ph::now), ph::lng_wallet_invoice_copied(ph::now),
                    ph::lng_wallet_receive_copied_qr(ph::now))));
}

void Window::showInvoiceQr(const QString &link) {
  _layers->showBox(Box(InvoiceQrBox, link,
                       shareCallback(ph::lng_wallet_invoice_copied(ph::now), ph::lng_wallet_invoice_copied(ph::now),
                                     ph::lng_wallet_receive_copied_qr(ph::now))));
}

Fn<void(QImage, QString)> Window::shareCallback(const QString &linkCopied, const QString &textCopied,
                                                const QString &qr) {
  return [=](const QImage &image, const QString &text) {
    if (!image.isNull()) {
      auto mime = std::make_unique<QMimeData>();
      if (!text.isEmpty()) {
        mime->setText(text);
      }
      mime->setImageData(image);
      QGuiApplication::clipboard()->setMimeData(mime.release());
      showToast(qr);
    } else {
      QGuiApplication::clipboard()->setText(text);
      showToast((text.indexOf("://") >= 0) ? linkCopied : textCopied);
    }
  };
}

Fn<void(QImage, QString)> Window::shareAddressCallback() {
  return shareCallback(ph::lng_wallet_receive_copied(ph::now), ph::lng_wallet_receive_address_copied(ph::now),
                       ph::lng_wallet_receive_copied_qr(ph::now));
}

Fn<void(QString)> Window::sharePubKeyCallback() {
  return [=](const QString &text) {
    QGuiApplication::clipboard()->setText(text);
    showToast(ph::lng_wallet_keystore_pubkey_copied(ph::now));
  };
}

void Window::showToast(const QString &text) {
  Ui::Toast::Show(_window.get(), text);
}

void Window::changePassword() {
  const auto saving = std::make_shared<bool>();
  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();
  auto box =
      Box(ChangePasscodeBox, [=](const QByteArray &old, const QByteArray &now, const Fn<void(QString)> &showError) {
        if (std::exchange(*saving, true)) {
          return;
        }
        const auto done = [=](Ton::Result<> result) {
          if (!result) {
            *saving = false;
            if (IsIncorrectPasswordError(result.error())) {
              showError(ph::lng_wallet_passcode_incorrect(ph::now));
            } else {
              showGenericError(result.error());
            }
            return;
          }
          if (*weakBox) {
            (*weakBox)->closeBox();
          }
          showToast(ph::lng_wallet_change_passcode_done(ph::now));
        };
        _wallet->changePassword(old, now, crl::guard(this, done));
      });
  *weakBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::showSettings() {
  const auto checkConfig = [=](QString path, Fn<void(QByteArray)> good) {
    checkConfigFromContent(
        [&] {
          auto file = QFile(path);
          file.open(QIODevice::ReadOnly);
          return file.readAll();
        }(),
        std::move(good));
  };
  auto box = Box(SettingsBox, _wallet->settings(), _updateInfo, checkConfig,
                 [=](const Ton::Settings &settings) { saveSettings(settings); });
  _settingsBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::checkConfigFromContent(QByteArray bytes, Fn<void(QByteArray)> good) {
  _wallet->checkConfig(bytes, [=](Ton::Result<> result) {
    if (result) {
      good(bytes);
    } else {
      showSimpleError(ph::lng_wallet_error(), ph::lng_wallet_bad_config(), ph::lng_wallet_ok());
    }
  });
}

void Window::saveSettings(const Ton::Settings &settings) {
  if (settings.net().useCustomConfig) {
    saveSettingsWithLoaded(settings);
    return;
  }
  const auto loaded = [=](Ton::Result<QByteArray> result) {
    if (!result) {
      if (result.error().type == Ton::Error::Type::Web) {
        using namespace rpl::mappers;
        showSimpleError(ph::lng_wallet_error(),
                        ph::lng_wallet_bad_config_url() | rpl::map(_1 + "\n\n" + result.error().details),
                        ph::lng_wallet_ok());
      } else {
        showGenericError(result.error());
      }
      return;
    }
    checkConfigFromContent(*result, [=](QByteArray config) {
      auto copy = settings;
      copy.net().config = std::move(config);
      saveSettingsWithLoaded(copy);
    });
  };
  _wallet->loadWebResource(settings.net().configUrl, loaded);
}

void Window::saveSettingsWithLoaded(const Ton::Settings &settings) {
  const auto useTestNetwork = settings.useTestNetwork;
  const auto &current = _wallet->settings();
  const auto change = (settings.useTestNetwork != current.useTestNetwork);
  if (change) {
    showSwitchTestNetworkWarning(settings);
    return;
  }
  const auto detach = (settings.net().blockchainName != current.net().blockchainName);
  if (detach) {
    showBlockchainNameWarning(settings);
    return;
  }
  saveSettingsSure(settings, [=] {
    if (_settingsBox) {
      _settingsBox->closeBox();
    }
    if (_viewer) {
      refreshNow();
    }
  });
}

void Window::saveSettingsSure(const Ton::Settings &settings, const Fn<void()> &done) {
  const auto showError = [=](const Ton::Error &error) {
    if (_saveConfirmBox) {
      _saveConfirmBox->closeBox();
    }
    showGenericError(error);
  };
  _wallet->updateSettings(settings, [=](Ton::Result<> result) {
    if (!result) {
      if (_wallet->publicKeys().empty()) {
        showCreate();
      }
      showError(result.error());
    } else {
      done();
    }
  });
}

void Window::refreshNow() {
  _viewer->refreshNow([=](Ton::Result<> result) {
    if (!result) {
      showGenericError(result.error());
    }
  });
}

void Window::showSwitchTestNetworkWarning(const Ton::Settings &settings) {
  showSettingsWithLogoutWarning(
      settings, (settings.useTestNetwork ? ph::lng_wallet_warning_to_testnet() : ph::lng_wallet_warning_to_mainnet()));
}

void Window::showBlockchainNameWarning(const Ton::Settings &settings) {
  showSettingsWithLogoutWarning(settings, ph::lng_wallet_warning_blockchain_name());
}

void Window::showSettingsWithLogoutWarning(const Ton::Settings &settings, rpl::producer<QString> text) {
  using namespace rpl::mappers;

  const auto saving = std::make_shared<bool>();
  auto box = Box([=](not_null<Ui::GenericBox *> box) mutable {
    box->setTitle(ph::lng_wallet_warning());
    box->addRow(object_ptr<Ui::FlatLabel>(
        box, rpl::combine(std::move(text), ph::lng_wallet_warning_reconnect()) | rpl::map(_1 + "\n\n" + _2),
        st::walletLabel));
    box->addButton(
        ph::lng_wallet_continue(),
        [=] {
          if (std::exchange(*saving, true)) {
            return;
          }
          saveSettingsSure(settings, [=] { logout(); });
        },
        st::attentionBoxButton);
    box->addButton(ph::lng_wallet_cancel(), [=] { box->closeBox(); });
  });
  _saveConfirmBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::showKeystore() {
  if (_keystoreBox) {
    _keystoreBox->closeBox();
  }

  auto deletionGuard = std::make_shared<bool>(false);
  auto handleAction = [=](Ton::KeyType keyType, const QByteArray &publicKey, KeystoreAction action) {
    switch (action) {
      case KeystoreAction::Export: {
        if (keyType == Ton::KeyType::Original) {
          askExportPassword();
        } else {
          exportFtabiKey(publicKey);
        }
        return;
      }
      case KeystoreAction::ChangePassword: {
        if (keyType == Ton::KeyType::Original) {
          changePassword();
        } else {
          askFtabiKeyChangePassword(publicKey);
        }
        return;
      }
      case KeystoreAction::Delete: {
        if (*deletionGuard) {
          return;
        }
        *deletionGuard = true;

        if (keyType != Ton::KeyType::Original) {
          _wallet->deleteFtabiKey(publicKey, [=](const Ton::Result<> &result) {
            if (!result) {
              *deletionGuard = false;
              return;
            }
            showKeystore();
          });
        }
        return;
      }
    }
  };

  auto creationGuard = std::make_shared<bool>(false);
  auto onCreate = [=] {
    if (*creationGuard) {
      return;
    }
    *creationGuard = true;

    addFtabiKey([=] { *creationGuard = false; }, [=](const QByteArray &) { showKeystore(); });
  };

  auto box = Box(KeystoreBox, getMainPublicKey(), _wallet->ftabiKeys(), sharePubKeyCallback(), handleAction, onCreate);
  _keystoreBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::exportFtabiKey(const QByteArray &publicKey) {
  const auto existingKeys = getExistingKeys();
  const auto it = existingKeys.find(publicKey);
  if (it == existingKeys.end()) {
    return showKeyNotFound();
  }

  const auto exporting = std::make_shared<bool>();
  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();
  const auto ready = [=](const QByteArray &passcode, const Fn<void(QString)> &showError) {
    if (*exporting) {
      return;
    }
    *exporting = true;
    const auto ready = [=](Ton::Result<std::pair<QString, std::vector<QString>>> result) {
      *exporting = false;
      if (!result) {
        if (IsIncorrectPasswordError(result.error())) {
          showError(ph::lng_wallet_passcode_incorrect(ph::now));
        } else {
          showGenericError(result.error());
        }
        return;
      }
      if (*weakBox) {
        (*weakBox)->closeBox();
      }
      showExportedFtabiKey(result.value().second);
    };
    _wallet->exportFtabiKey(publicKey, passcode, crl::guard(this, ready));
  };
  auto box = Box(EnterPasscodeBox, it->second.name,
                 [=](const QByteArray &passcode, const Fn<void(QString)> &showError) { ready(passcode, showError); });
  *weakBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::showExportedFtabiKey(const std::vector<QString> &words) {
  _layers->showBox(Box(ExportedFtabiKeyBox, words));
}

void Window::addFtabiKey(const Fn<void()> &cancel, const OnFtabiKeyCreated &done) {
  auto guard = std::make_shared<bool>(false);
  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();

  const auto submit = [=](const NewFtabiKey &newKey) {
    if (*guard) {
      return;
    }
    *guard = true;

    if (newKey.generate) {
      _wallet->createFtabiKey(newKey.name, Ton::kFtabiKeyDerivationPath, [=](Ton::Result<std::vector<QString>> result) {
        if (!result) {
          return showToast(result.error().details);
        }

        showNewFtabiKey(result.value(), done);
      });
    } else {
      importFtabiKey(newKey.name, cancel, done);
    }

    if (*weakBox) {
      (*weakBox)->closeBox();
    }
  };

  auto box = Box(NewFtabiKeyBox, cancel, submit);
  *weakBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::importFtabiKey(const QString &name, const Fn<void()> &cancel, const OnFtabiKeyCreated &done) {
  auto guard = std::make_shared<bool>(false);
  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();
  const auto submit = [=](const WordsList &words) {
    if (*guard) {
      return;
    }
    *guard = true;

    _wallet->importFtabiKey(                        //
        name, Ton::kFtabiKeyDerivationPath, words,  //
        crl::guard(this, [=](Ton::Result<> result) {
          if (result) {
            askNewFtabiKeyPassword([=](const QByteArray &publicKey) {
              if (*weakBox) {
                (*weakBox)->closeBox();
              }
              done(publicKey);
            });
          } else if (IsIncorrectMnemonicError(result.error())) {
            *guard = false;
            createShowIncorrectImport();
          } else {
            *guard = false;
            showGenericError(result.error());
          }
        }));
  };

  auto box = Box(ImportFtabiKeyBox, cancel, submit);
  *weakBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::showNewFtabiKey(const std::vector<QString> &words, const OnFtabiKeyCreated &done) {
  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();
  auto box = Box(GeneratedFtabiKeyBox, words, [=] {
    askNewFtabiKeyPassword([=](const QByteArray &publicKey) {
      if (*weakBox) {
        (*weakBox)->closeBox();
      }
      done(publicKey);
    });
  });
  *weakBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::askNewFtabiKeyPassword(const OnFtabiKeyCreated &done) {
  const auto saving = std::make_shared<bool>();
  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();
  auto box = Box(NewFtabiKeyPasswordBox, [=](const QByteArray &localPassword, const Fn<void(QString)> &showError) {
    if (std::exchange(*saving, true)) {
      return;
    }
    const auto onSave = [=](Ton::Result<QByteArray> result) {
      if (!result) {
        *saving = false;
        if (IsIncorrectPasswordError(result.error())) {
          showError(ph::lng_wallet_passcode_incorrect(ph::now));
        } else {
          showGenericError(result.error());
        }
        return;
      }
      if (*weakBox) {
        (*weakBox)->closeBox();
      }
      showToast(ph::lng_wallet_new_ftabi_key_done(ph::now));
      done(result.value());
    };
    _wallet->saveFtabiKey(localPassword, crl::guard(this, onSave));
  });
  *weakBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::askFtabiKeyChangePassword(const QByteArray &publicKey) {
  const auto saving = std::make_shared<bool>();
  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();
  auto box =
      Box(ChangePasscodeBox, [=](const QByteArray &old, const QByteArray &now, const Fn<void(QString)> &showError) {
        if (std::exchange(*saving, true)) {
          return;
        }
        const auto done = [=](Ton::Result<> result) {
          if (!result) {
            std::cout << result.error().details.toStdString() << std::endl;
            *saving = false;
            if (IsIncorrectPasswordError(result.error())) {
              showError(ph::lng_wallet_passcode_incorrect(ph::now));
            } else {
              showGenericError(result.error());
            }
            return;
          }
          if (*weakBox) {
            (*weakBox)->closeBox();
          }
          showToast(ph::lng_wallet_change_passcode_done(ph::now));
        };
        _wallet->changeFtabiPassword(publicKey, old, now, crl::guard(this, done));
      });
  *weakBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::importMultisig(const QString &address) {
  const auto onNewMultisig = [=](const Ton::Result<> &result) {
    if (result.has_value()) {
      showToast(ph::lng_wallet_add_multisig_succeeded(ph::now));
    } else {
      std::cout << result.error().details.toStdString() << std::endl;
      showMultisigError();
    }
  };

  _wallet->requestMultisigInfo(  //
      address, crl::guard(this, [=](Ton::Result<Ton::MultisigInfo> &&result) {
        if (!result.has_value()) {
          std::cout << result.error().details.toStdString() << std::endl;
          return showMultisigError();
        }
        _wallet->addMultisig(_wallet->publicKeys().back(), result.value(), crl::guard(this, onNewMultisig));
      }));
}

void Window::showMultisigError() {
  showSimpleError(ph::lng_wallet_add_multisig_failed_title(), ph::lng_wallet_add_multisig_failed_text(),
                  ph::lng_wallet_continue());
}

void Window::selectMultisigKey(const std::vector<QByteArray> &custodians, int defaultIndex, bool allowNewKeys,
                               const Fn<void(QByteArray)> &done) {
  auto showImportKeyError = [=] {
    showSimpleError(ph::lng_wallet_add_multisig_failed_title(), ph::lng_wallet_add_multisig_is_not_a_custodian(),
                    ph::lng_wallet_continue());
  };

  const auto closeBox = [=] {
    if (_keySelectionBox) {
      _keySelectionBox->closeBox();
    }
  };

  auto guard = std::make_shared<bool>(false);
  const auto addNewKey = crl::guard(this, [=] {
    if (std::exchange(*guard, true)) {
      return;
    }

    addFtabiKey(closeBox, [=](const QByteArray &publicKey) {
      if (allowNewKeys) {
        return done(publicKey);
      }

      const auto it = std::find(custodians.begin(), custodians.end(), publicKey);
      if (it != custodians.end()) {
        done(publicKey);
      } else {
        *guard = false;
        showImportKeyError();
      }
    });
  });

  const auto availableKeys = getAvailableKeys(custodians);

  if (availableKeys.empty()) {
    addNewKey();
  } else if (custodians.size() == 1 && !allowNewKeys) {
    done(custodians.front());
  } else {
    auto box = Box(SelectMultisigKeyBox, custodians, availableKeys, defaultIndex, allowNewKeys, addNewKey, done);
    _keySelectionBox = box.data();
    _layers->showBox(std::move(box));
  }
}

void Window::addNewMultisig() {
  const auto onNewMultisig = [=](const Ton::Result<> &result) {
    if (result.has_value()) {
      showToast(ph::lng_wallet_add_multisig_succeeded(ph::now));
    } else {
      std::cout << result.error().details.toStdString() << std::endl;
      showMultisigError();
    }
  };

  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();
  auto versionSelectionGuard = std::make_shared<bool>(false);
  const auto submit = [=](Ton::MultisigVersion version) {
    if (std::exchange(*versionSelectionGuard, true)) {
      return;
    }
    if (*weakBox) {
      (*weakBox)->closeBox();
    }

    auto keySelectionGuard = std::make_shared<bool>(false);
    const auto keySelected = [=](const QByteArray &publicKey) {
      if (std::exchange(*keySelectionGuard, true)) {
        return;
      }

      _wallet->requestNewMultisigAddress(version, publicKey, [=](Ton::Result<Ton::MultisigPredeployInfo> &&result) {
        if (!result.has_value()) {
          std::cout << result.error().details.toStdString() << std::endl;
          *keySelectionGuard = false;
          return showSimpleError(ph::lng_wallet_deploy_multisig_failed_title(),
                                 ph::lng_wallet_deploy_multisig_failed_text_already_exists(),
                                 ph::lng_wallet_continue());
        }
        if (_keySelectionBox) {
          _keySelectionBox->closeBox();
        }
        auto info = std::move(result->initialInfo);
        _wallet->addMultisig(  //
            getMainPublicKey(),
            Ton::MultisigInfo{
                .address = info.address,
                .version = info.version,
                .publicKey = info.publicKey,
                .expirationTime = Ton::GetExpirationTime(version),
            },
            crl::guard(this, onNewMultisig));
      });
    };

    const auto keys = getAllPublicKeys();
    selectMultisigKey(keys, 0, true, keySelected);
  };
  auto box = Box(SelectMultisigVersionBox, submit);
  *weakBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::deployMultisig(const QString &address) {
  auto state = _state.current();
  const auto it = state.multisigStates.find(address);
  if (it == state.multisigStates.end()) {
    return;
  }

  if (!_multisigDeploymentGuard) {
    _multisigDeploymentGuard = std::make_shared<bool>(false);
  }
  if (std::exchange(*_multisigDeploymentGuard, true)) {
    return;
  }

  if (_multisigDeploymentBox) {
    _multisigDeploymentBox->closeBox();
  }

  auto showConstructorBox = [=](const Ton::MultisigPredeployInfo &info) {
    if (_multisigDeploymentBox) {
      _multisigDeploymentBox->closeBox();
    }
    auto guard = std::make_shared<bool>(false);
    auto box = Box(DeployMultisigBox, info.initialInfo, [=](MultisigDeployInvoice &&invoice) {
      confirmTransaction(
          std::forward<decltype(invoice)>(invoice), [=](InvoiceField) {}, guard);
    });
    _sendBox = box.data();
    _layers->showBox(std::move(box));
  };

  auto stateHandlerGuard = std::make_shared<bool>(false);
  auto handleMultisigState = [=](Ton::Result<Ton::MultisigPredeployInfo> &&result,
                                 const Fn<void(Ton::MultisigInitialInfo)> &showAddressBox) {
    if (!result.has_value()) {
      std::cout << result.error().details.toStdString() << std::endl;
      if (_multisigDeploymentBox) {
        _multisigDeploymentBox->closeBox();
      }
      return showSimpleError(ph::lng_wallet_deploy_multisig_failed_title(),
                             ph::lng_wallet_deploy_multisig_failed_text_already_exists(), ph::lng_wallet_continue());
    }

    if (result->balance < Ton::kMinimalDeploymentBalance) {
      if (*stateHandlerGuard) {
        showToast(ph::lng_wallet_predeploy_multisig_insufficient_funds(ph::now));
      }
      showAddressBox(std::move(result->initialInfo));
    } else {
      showConstructorBox(*result);
    }
  };

  const auto version = it->second.version;
  const auto publicKey = it->second.publicKey;
  _wallet->requestNewMultisigAddress(version, publicKey, [=](Ton::Result<Ton::MultisigPredeployInfo> &&result) {
    handleMultisigState(std::forward<decltype(result)>(result), [=](Ton::MultisigInitialInfo &&info) {
      auto box = Box(PredeployMultisigBox, info, shareAddressCallback(), [=] {
        if (std::exchange(*stateHandlerGuard, true)) {
          return;
        }
        _wallet->requestNewMultisigAddress(version, publicKey, [=](auto &&result) {
          handleMultisigState(std::forward<decltype(result)>(result), [=](auto &&) { *stateHandlerGuard = false; });
        });
      });

      _multisigDeploymentBox = box.data();
      _layers->showBox(std::move(box));
    });

    *_multisigDeploymentGuard = false;
  });
}

QByteArray Window::getMainPublicKey() const {
  return _wallet->publicKeys().front();
}

std::vector<QByteArray> Window::getAllPublicKeys() const {
  const auto mainPublicKey = getMainPublicKey();
  const auto ftabiKeys = _wallet->ftabiKeys();

  std::vector<QByteArray> result;
  result.reserve(1 + ftabiKeys.size());

  result.emplace_back(mainPublicKey);

  for (const auto &key : ftabiKeys) {
    result.emplace_back(key.publicKey);
  }

  return result;
}

std::vector<Ton::AvailableKey> Window::getAvailableKeys(const std::vector<QByteArray> &custodians) const {
  auto existingKeys = getExistingKeys();

  std::vector<Ton::AvailableKey> availableKeys;
  availableKeys.reserve(custodians.size());
  for (const auto &custodian : custodians) {
    const auto it = existingKeys.find(custodian);
    if (it != existingKeys.end()) {
      availableKeys.emplace_back(it->second);
    }
  }

  return availableKeys;
}

base::flat_map<QByteArray, Ton::AvailableKey> Window::getExistingKeys() const {
  const auto mainPublicKey = getMainPublicKey();
  base::flat_map<QByteArray, Ton::AvailableKey> existingKeys;
  existingKeys.emplace(mainPublicKey, Ton::AvailableKey{
                                          .type = Ton::KeyType::Original,
                                          .publicKey = mainPublicKey,
                                          .name = ph::lng_wallet_keystore_main_wallet_key(ph::now),
                                      });
  for (const auto &key : _wallet->ftabiKeys()) {
    existingKeys.emplace(key.publicKey, Ton::AvailableKey{
                                            .type = Ton::KeyType::Ftabi,
                                            .publicKey = key.publicKey,
                                            .name = key.name,
                                        });
  }
  return existingKeys;
}

void Window::askExportPassword() {
  const auto exporting = std::make_shared<bool>();
  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();
  const auto ready = [=](const QByteArray &passcode, const Fn<void(QString)> &showError) {
    if (*exporting) {
      return;
    }
    *exporting = true;
    const auto ready = [=](Ton::Result<std::vector<QString>> result) {
      *exporting = false;
      if (!result) {
        if (IsIncorrectPasswordError(result.error())) {
          showError(ph::lng_wallet_passcode_incorrect(ph::now));
        } else {
          showGenericError(result.error());
        }
        return;
      }
      if (*weakBox) {
        (*weakBox)->closeBox();
      }
      showExported(*result);
    };
    _wallet->exportKey(getMainPublicKey(), passcode, crl::guard(this, ready));
  };
  auto box = Box(EnterPasscodeBox, ph::lng_wallet_keystore_main_wallet_key(ph::now),
                 [=](const QByteArray &passcode, const Fn<void(QString)> &showError) { ready(passcode, showError); });
  *weakBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::showExported(const std::vector<QString> &words) {
  _layers->showBox(Box(ExportedBox, words));
}

void Window::logoutWithConfirmation() {
  _layers->showBox(Box(DeleteWalletBox, [=] { logout(); }));
}

void Window::logout() {
  _wallet->deleteAllKeys(crl::guard(this, [=](Ton::Result<> result) {
    if (!result) {
      showGenericError(result.error());
      return;
    }
    showCreate();
  }));
}

void Window::back() {
  _infoTransitions.fire_copy(InfoTransition::Back);
}

}  // namespace Wallet
