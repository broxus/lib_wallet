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
#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>

namespace Wallet {
namespace {

constexpr auto kRefreshEachDelay = 10 * crl::time(1000);
constexpr auto kRefreshInactiveDelay = 60 * crl::time(1000);
constexpr auto kRefreshWhileSendingDelay = 3 * crl::time(1000);

[[nodiscard]] bool ValidateTransferLink(const QString &link) {
  return QRegularExpression(
             QString("^((freeton://)?transfer|stake/)?[a-z0-9_\\-]{%1}/?($|\\?)").arg(kEncodedAddressLength),
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

  _window->body()->sizeValue() | rpl::start_with_next(
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
                case Create::Manager::Action::ShowCheckTooSoon:
                  createShowTooFastWords();
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

void Window::createShowTooFastWords() {
  showSimpleError(ph::lng_wallet_words_sure_title(), ph::lng_wallet_words_sure_text(), ph::lng_wallet_words_sure_ok());
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

void Window::createSavePasscode(const QByteArray &passcode, std::shared_ptr<bool> guard) {
  if (std::exchange(*guard, true)) {
    return;
  }
  if (!_importing) {
    createSaveKey(passcode, QString(), std::move(guard));
    return;
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

void Window::createSaveKey(const QByteArray &passcode, const QString &address, std::shared_ptr<bool> guard) {
  const auto done = [=](Ton::Result<QByteArray> result) {
    *guard = false;
    if (!result) {
      showGenericError(result.error());
      return;
    }
    _createManager->showReady(*result);
  };
  _wallet->saveKey(passcode, address, crl::guard(this, done));
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

  _viewer->loaded()                                                                                             //
      | rpl::filter([](const Ton::Result<std::pair<Ton::Symbol, Ton::LoadedSlice>> &value) { return !value; })  //
      | rpl::map(
            [](Ton::Result<std::pair<Ton::Symbol, Ton::LoadedSlice>> &&value) { return std::move(value.error()); })  //
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
                          sendMoney(TokenTransferInvoice{.token = selectedToken.symbol});
                        }
                      },
                      [&](const SelectedDePool &selectedDePool) {
                        sendStake(StakeInvoice{
                            .stake = 0,
                            .dePool = selectedDePool.address,
                        });
                      });
                  return;
                case Action::Receive:
                  v::match(
                      _selectedAsset.current().value_or(SelectedToken::defaultToken()),
                      [&](const SelectedToken &selectedToken) { receiveTokens(selectedToken.symbol); },
                      [&](const SelectedDePool &selectedDePool) {
                        auto state = _state.current();
                        const auto it = state.dePoolParticipantStates.find(selectedDePool.address);
                        if (it != state.dePoolParticipantStates.end() &&
                            (it->second.withdrawValue > 0 || !it->second.reinvest)) {
                          dePoolCancelWithdrawal(CancelWithdrawalInvoice{.dePool = selectedDePool.address});
                        } else {
                          dePoolWithdraw(WithdrawalInvoice{.amount = 0, .dePool = selectedDePool.address});
                        }
                      });
                  return;
                case Action::ChangePassword:
                  return changePassword();
                case Action::ShowSettings:
                  return showSettings();
                case Action::AddAsset:
                  return addAsset();
                case Action::DeployTokenWallet:
                  v::match(
                      _selectedAsset.current().value_or(SelectedToken::defaultToken()),
                      [&](const SelectedToken &selectedToken) {
                        auto state = _state.current();
                        const auto it = state.tokenStates.find(selectedToken.symbol);
                        if (it != state.tokenStates.end()) {
                          deployTokenWallet(
                              DeployTokenWalletInvoice{.rootContractAddress = it->first.rootContractAddress(),
                                                       .walletContractAddress = it->second.walletContractAddress,
                                                       .owned = true});
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
                  return _wallet->removeDePool(_wallet->publicKeys().front(), asset.address);
                case CustomAssetType::Token:
                  return _wallet->removeToken(_wallet->publicKeys().front(), asset.symbol);
                default:
                  return;
              }
            },
            _info->lifetime());

  _info->assetsReorderRequests()  //
      | rpl::start_with_next(
            [this](const std::pair<int, int> &indices) {
              _wallet->reorderAssets(_wallet->publicKeys().front(), indices.first, indices.second);
            },
            _info->lifetime());

  _info->preloadRequests()  //
      | rpl::start_with_next(
            [=](const std::pair<Ton::Symbol, Ton::TransactionId> &id) {
              if (id.first.isTon()) {
                _viewer->preloadSlice(id.second);
              } else {
                const auto state = _state.current();
                const auto it = state.tokenStates.find(id.first);
                if (it != end(state.tokenStates)) {
                  _viewer->preloadTokenSlice(id.first, it->second.walletContractAddress, id.second);
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

  const auto onNewToken = [this](const Ton::Result<> &result) {
    if (result.has_value()) {
      refreshNow();
      showToast(ph::lng_wallet_add_token_succeeded(ph::now));
    } else {
      showSimpleError(ph::lng_wallet_add_token_failed_title(), ph::lng_wallet_add_token_failed_text(),
                      ph::lng_wallet_continue());
    }
  };

  _info->newTokenWalletRequests()  //
      | rpl::start_with_next(
            [=](const QString *rootTokenAddress) {
              const auto state = _state.current();
              for (const auto &item : state.tokenStates) {
                if (item.first.rootContractAddress() == *rootTokenAddress) {
                  return;
                }
              }
              _wallet->addToken(_wallet->publicKeys().front(), *rootTokenAddress, crl::guard(this, onNewToken));
            },
            _info->lifetime());

  _info->collectTokenRequests()  //
      | rpl::start_with_next([=](const QString *eventContractAddress) { collectTokens(*eventContractAddress); },
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
                      sendMoney(TokenTransferInvoice{
                          .token = selectedToken.symbol,
                          .address = address,
                      });
                    }
                  };

                  const auto reveal = [=](const QString &address) { _wallet->openReveal(_rawAddress, address); };

                  auto resolveOwner = crl::guard(this, [=](const QString &wallet, const Fn<void(QString &&)> &done) {
                    _wallet->getWalletOwner(selectedToken.symbol.rootContractAddress(), wallet,
                                            crl::guard(this, [=](Ton::Result<QString> result) {
                                              if (result.has_value()) {
                                                return done(std::move(result.value()));
                                              }
                                            }));
                  });

                  _layers->showBox(Box(
                      ViewTransactionBox, std::move(data), selectedToken.symbol, _collectEncryptedRequests.events(),
                      _decrypted.events(), shareAddressCallback(), [=] { decryptEverything(publicKey); }, resolveOwner,
                      send, reveal));
                },
                [&](const SelectedDePool &selectedDePool) {
                  _layers->showBox(Box(ViewDePoolTransactionBox, std::move(data), shareAddressCallback()));
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
    auto box = Box(EnterPasscodeBox, [=](const QByteArray &passcode, Fn<void(QString)> showError) {
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

        if (tokenTransferInvoice.callbackAddress.isEmpty()) {
          const auto state = _state.current();
          const auto it = state.tokenStates.find(tokenTransferInvoice.token);
          if (it != state.tokenStates.end()) {
            tokenTransferInvoice.callbackAddress = it->second.rootOwnerAddress;
          }
        }

        return Box(SendGramsBox<TokenTransferInvoice>, tokenTransferInvoice, _state.value(), send);
      },
      [=](auto &&) -> object_ptr<Ui::GenericBox> { return nullptr; });

  if (box != nullptr) {
    _sendBox = box.data();
    _layers->showBox(std::move(box));
  }
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

  auto box = Box(CollectTokensBox, CollectTokensInvoice{.eventContractAddress = eventContractAddress}, send);
  _sendBox = box.data();
  _layers->showBox(std::move(box));
}

void Window::confirmTransaction(PreparedInvoice invoice, const Fn<void(InvoiceField)> &showInvoiceError,
                                const std::shared_ptr<bool> &guard) {
  if (*guard || !_sendBox) {
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
      [&](CollectTokensInvoice &collectTokensInvoice) {
        collectTokensInvoice.realAmount = Ton::CollectTokensTransactionToSend::realAmount;
      });

  auto done = [=](Ton::Result<Ton::TransactionCheckResult> result, PreparedInvoice &&invoice) mutable {
    *guard = false;
    if (!result.has_value()) {
      if (const auto field = ErrorInvoiceField(result.error())) {
        return showInvoiceError(*field);
      }

      v::match(
          invoice,
          [&](const TonTransferInvoice &tonTransferInvoice) {
            if (!tonTransferInvoice.sendUnencryptedText && result.error().details.startsWith("MESSAGE_ENCRYPTION")) {
              auto copy = tonTransferInvoice;
              copy.sendUnencryptedText = true;
              confirmTransaction(copy, showInvoiceError, guard);
            } else {
              showGenericError(result.error());
            }
          },
          [&](auto &&) { showGenericError(result.error()); });

      return;
    }

    showSendConfirmation(invoice, *result, showInvoiceError);
  };

  auto doneUnchanged = [done, invoice = invoice](const Ton::Result<Ton::TransactionCheckResult> &result) mutable {
    done(result, std::move(invoice));
  };

  v::match(
      invoice,
      [&](const TonTransferInvoice &tonTransferInvoice) {
        _wallet->checkSendGrams(_wallet->publicKeys().front(), tonTransferInvoice.asTransaction(),
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

        _wallet->checkSendTokens(_wallet->publicKeys().front(), tokenTransferInvoice.asTransaction(),
                                 crl::guard(_sendBox.data(), std::move(tokenHandler)));
      },
      [&](const StakeInvoice &stakeInvoice) {
        _wallet->checkSendStake(_wallet->publicKeys().front(), stakeInvoice.asTransaction(),
                                crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const WithdrawalInvoice &withdrawalInvoice) {
        _wallet->checkWithdraw(_wallet->publicKeys().front(), withdrawalInvoice.asTransaction(),
                               crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const CancelWithdrawalInvoice &cancelWithdrawalInvoice) {
        _wallet->checkCancelWithdraw(_wallet->publicKeys().front(), cancelWithdrawalInvoice.asTransaction(),
                                     crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const DeployTokenWalletInvoice &deployTokenWalletInvoice) {
        _wallet->checkDeployTokenWallet(_wallet->publicKeys().front(), deployTokenWalletInvoice.asTransaction(),
                                        crl::guard(_sendBox.data(), doneUnchanged));
      },
      [&](const CollectTokensInvoice &collectTokensInvoice) {
        _wallet->checkCollectTokens(_wallet->publicKeys().front(), collectTokensInvoice.asTransaction(),
                                    crl::guard(_sendBox.data(), doneUnchanged));
      });
}

void Window::askSendPassword(const PreparedInvoice &invoice, const Fn<void(InvoiceField)> &showInvoiceError) {
  const auto publicKey = _wallet->publicKeys().front();
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
      _wallet->updateViewersPassword(publicKey, passcode);
      decryptEverything(publicKey);
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
          _wallet->sendGrams(publicKey, passcode, tonTransferInvoice.asTransaction(), crl::guard(this, ready),
                             crl::guard(this, sent));
        },
        [&](const TokenTransferInvoice &tokenTransferInvoice) {
          _wallet->sendTokens(publicKey, passcode, tokenTransferInvoice.asTransaction(), crl::guard(this, ready),
                              crl::guard(this, sent));
        },
        [&](const StakeInvoice &stakeInvoice) {
          _wallet->sendStake(publicKey, passcode, stakeInvoice.asTransaction(), crl::guard(this, ready),
                             crl::guard(this, sent));
        },
        [&](const WithdrawalInvoice &withdrawalInvoice) {
          _wallet->withdraw(publicKey, passcode, withdrawalInvoice.asTransaction(), crl::guard(this, ready),
                            crl::guard(this, sent));
        },
        [&](const CancelWithdrawalInvoice &cancelWithdrawalInvoice) {
          _wallet->cancelWithdrawal(publicKey, passcode, cancelWithdrawalInvoice.asTransaction(),
                                    crl::guard(this, ready), crl::guard(this, sent));
        },
        [&](const DeployTokenWalletInvoice &deployTokenWalletInvoice) {
          _wallet->deployTokenWallet(publicKey, passcode, deployTokenWalletInvoice.asTransaction(),
                                     crl::guard(this, ready), crl::guard(this, sent));
        },
        [&](const CollectTokensInvoice &collectTokensInvoice) {
          _wallet->collectTokens(publicKey, passcode, collectTokensInvoice.asTransaction(), crl::guard(this, ready),
                                 crl::guard(this, sent));
        });
  };
  if (_sendConfirmBox) {
    _sendConfirmBox->closeBox();
  }
  auto box = Box(EnterPasscodeBox, [=](const QByteArray &passcode, Fn<void(QString)> showError) {
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

  const auto checkAmount = [&](int64_t realAmount) {
    return gramsAvailable > realAmount + checkResult.sourceFees.sum();
  };

  const auto sourceFees = checkResult.sourceFees.sum();

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
      [&](const CollectTokensInvoice &collectTokensInvoice) -> object_ptr<Ui::GenericBox> {
        if (!checkAmount(collectTokensInvoice.realAmount)) {
          showInvoiceError(InvoiceField::Address);
          return nullptr;
        }

        return Box(ConfirmTransactionBox<CollectTokensInvoice>, collectTokensInvoice,
                   collectTokensInvoice.realAmount + sourceFees,
                   [=] { askSendPassword(collectTokensInvoice, showInvoiceError); });
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

  auto box = Box(SendingTransactionBox, token, std::move(confirmed));

  _sendBox = box.data();
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
  _layers->showBox(std::move(box));

  if (_sendConfirmBox) {
    _sendConfirmBox->closeBox();
  }
}

void Window::showSendingDone(std::optional<Ton::Transaction> result, const PreparedInvoice &invoice) {
  if (result) {
    auto box = v::match(
        invoice,
        [&](const TonTransferInvoice &tonTransferInvoice) {
          return Box(SendingDoneBox<TonTransferInvoice>, *result, tonTransferInvoice, [this] { refreshNow(); });
        },
        [&](const TokenTransferInvoice &tokenTransferInvoice) {
          return Box(SendingDoneBox<TokenTransferInvoice>, *result, tokenTransferInvoice, [this, tokenTransferInvoice] {
            refreshNow();
            if (tokenTransferInvoice.transferType == Ton::TokenTransferType::SwapBack) {
              _wallet->openReveal(_rawAddress, tokenTransferInvoice.address);
            }
          });
        },
        [&](const StakeInvoice &stakeInvoice) {
          return Box(SendingDoneBox<StakeInvoice>, *result, stakeInvoice, [this] { refreshNow(); });
        },
        [&](const WithdrawalInvoice &withdrawalInvoice) {
          return Box(SendingDoneBox<WithdrawalInvoice>, *result, withdrawalInvoice, [this] { refreshNow(); });
        },
        [&](const CancelWithdrawalInvoice &cancelWithdrawalInvoice) {
          return Box(SendingDoneBox<CancelWithdrawalInvoice>, *result, cancelWithdrawalInvoice,
                     [this] { refreshNow(); });
        },
        [&](const DeployTokenWalletInvoice &deployTokenWalletInvoice) {
          return Box(SendingDoneBox<DeployTokenWalletInvoice>, *result, deployTokenWalletInvoice,
                     [this] { refreshNow(); });
        },
        [&](const CollectTokensInvoice &collectTokensInvoice) {
          return Box(SendingDoneBox<CollectTokensInvoice>, *result, collectTokensInvoice, [this] { refreshNow(); });
        });

    _layers->showBox(std::move(box));
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
  const auto send = [=](const CustomAsset &newAsset, const Fn<void(AddAssetField)> &showError) {
    if (*checking) {
      return;
    }

    if (!Ton::Wallet::CheckAddress(newAsset.address)) {
      return showError(AddAssetField::Address);
    }
    switch (newAsset.type) {
      case CustomAssetType::DePool: {
        _wallet->addDePool(_wallet->publicKeys().front(), newAsset.address, crl::guard(this, onNewDepool));
        break;
      }
      case CustomAssetType::Token: {
        _wallet->addToken(_wallet->publicKeys().front(), newAsset.address, crl::guard(this, onNewToken));
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

void Window::receiveTokens(const Ton::Symbol &selectedToken) {
  _layers->showBox(Box(
      ReceiveTokensBox, _rawAddress, selectedToken,
      [this, selectedToken = selectedToken] { createInvoice(selectedToken); }, shareAddressCallback(),
      [this, selectedToken = selectedToken] { _wallet->openGate(_rawAddress, selectedToken); }));
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

void Window::showToast(const QString &text) {
  Ui::Toast::Show(_window.get(), text);
}

void Window::changePassword() {
  const auto saving = std::make_shared<bool>();
  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();
  auto box = Box(ChangePasscodeBox, [=](const QByteArray &old, const QByteArray &now, Fn<void(QString)> showError) {
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

void Window::saveSettingsSure(const Ton::Settings &settings, Fn<void()> done) {
  const auto showError = [=](Ton::Error error) {
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

void Window::askExportPassword() {
  const auto exporting = std::make_shared<bool>();
  const auto weakBox = std::make_shared<QPointer<Ui::GenericBox>>();
  const auto ready = [=](const QByteArray &passcode, Fn<void(QString)> showError) {
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
    _wallet->exportKey(_wallet->publicKeys().front(), passcode, crl::guard(this, ready));
  };
  auto box = Box(EnterPasscodeBox,
                 [=](const QByteArray &passcode, Fn<void(QString)> showError) { ready(passcode, showError); });
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
