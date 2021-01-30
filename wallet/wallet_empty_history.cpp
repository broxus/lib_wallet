// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_empty_history.h"

#include "wallet/wallet_phrases.h"
#include "ton/ton_state.h"
#include "ui/widgets/labels.h"
#include "ui/address_label.h"
#include "ui/lottie_widget.h"
#include "styles/style_wallet.h"

namespace Wallet {

EmptyHistory::EmptyHistory(not_null<Ui::RpWidget *> parent, rpl::producer<EmptyHistoryState> state,
                           Fn<void(QImage, QString)> share)
    : _widget(parent), _share(share) {
  setupControls(std::move(state));
}

void EmptyHistory::setGeometry(QRect geometry) {
  _widget.setGeometry(geometry);
}

void EmptyHistory::setVisible(bool visible) {
  _widget.setVisible(visible);
}

rpl::lifetime &EmptyHistory::lifetime() {
  return _widget.lifetime();
}

void EmptyHistory::setupControls(rpl::producer<EmptyHistoryState> &&state) {
  const auto lottie = _widget.lifetime().make_state<Ui::LottieAnimation>(&_widget, Ui::LottieFromResource("empty"));
  lottie->stopOnLoop(1);
  lottie->start();

  auto justCreated = rpl::duplicate(state) | rpl::map([](const EmptyHistoryState &state) { return state.justCreated; });
  const auto title =
      Ui::CreateChild<Ui::FlatLabel>(&_widget,
                                     rpl::conditional(std::move(justCreated), ph::lng_wallet_empty_history_title(),
                                                      ph::lng_wallet_empty_history_welcome()),
                                     st::walletEmptyHistoryTitle);
  rpl::combine(_widget.sizeValue(), title->widthValue()) |
      rpl::start_with_next(
          [=](QSize size, int width) {
            const auto blockTop = (size.height() - st::walletEmptyHistoryHeight) / 2;

            lottie->setGeometry({(size.width() - st::walletEmptyLottieSize) / 2, blockTop + st::walletEmptyLottieTop,
                                 st::walletEmptyLottieSize, st::walletEmptyLottieSize});

            title->moveToLeft((size.width() - width) / 2, blockTop + st::walletEmptyHistoryTitleTop, size.width());
          },
          title->lifetime());

  auto addressDescription =  //
      rpl::duplicate(state)  //
      | rpl::map([](const EmptyHistoryState &state) {
          switch (state.addressType) {
            case AddressLabelType::YourAddress:
              return ph::lng_wallet_empty_history_address();
            case AddressLabelType::DePoolAddress:
              return ph::lng_wallet_empty_history_depool_address();
            case AddressLabelType::TokenAddress:
              return ph::lng_wallet_empty_history_token_address();
            default:
              Unexpected("Unreachable");
          }
        })  //
      | rpl::flatten_latest();

  const auto label =
      Ui::CreateChild<Ui::FlatLabel>(&_widget, std::move(addressDescription), st::walletEmptyHistoryLabel);
  rpl::combine(_widget.sizeValue(), label->widthValue()) |
      rpl::start_with_next(
          [=](QSize size, int width) {
            const auto blockTop = (size.height() - st::walletEmptyHistoryHeight) / 2;
            label->moveToLeft((size.width() - width) / 2, blockTop + st::walletEmptyHistoryLabelTop, size.width());
          },
          label->lifetime());

  const auto currentAddress = _widget.lifetime().make_state<rpl::variable<QString>>();
  std::move(state) |
      rpl::start_with_next([currentAddress](const EmptyHistoryState &state) { *currentAddress = state.address; },
                           _widget.lifetime());

  const auto address = Ui::CreateAddressLabel(&_widget, currentAddress->value(), st::walletEmptyHistoryAddress,
                                              [this, currentAddress] { _share(QImage(), currentAddress->current()); });

  rpl::combine(_widget.sizeValue(), address->widthValue()) |
      rpl::start_with_next(
          [=](QSize size, int width) {
            const auto blockTop = (size.height() - st::walletEmptyHistoryHeight) / 2;
            address->moveToLeft((size.width() - address->widthNoMargins()) / 2,
                                blockTop + st::walletEmptyHistoryAddressTop, size.width());
          },
          address->lifetime());
}

rpl::producer<EmptyHistoryState> MakeEmptyHistoryState(rpl::producer<Ton::WalletViewerState> state,
                                                       rpl::producer<std::optional<SelectedAsset>> selectedAsset,
                                                       bool justCreated) {
  return rpl::combine(std::move(state), std::move(selectedAsset))  //
         | rpl::map(
               [justCreated](const Ton::WalletViewerState &state, const std::optional<SelectedAsset> &selectedAsset) {
                 const auto asset = selectedAsset.value_or(SelectedToken{.token = Ton::Symbol::ton()});

                 const auto [address, isToken] = v::match(
                     asset,
                     [&](const SelectedToken &selectedToken) {
                       if (selectedToken.token.isTon()) {
                         return std::make_pair(state.wallet.address, AddressLabelType::YourAddress);
                       }

                       const auto it = state.wallet.tokenStates.find(selectedToken.token);
                       if (it != state.wallet.tokenStates.end()) {
                         return std::make_pair(it->second.walletContractAddress, AddressLabelType::TokenAddress);
                       } else {
                         // Unreachable in theory
                         return std::make_pair(state.wallet.address, AddressLabelType::TokenAddress);
                       }
                     },
                     [&](const SelectedDePool &selectedDePool) {
                       return std::make_pair(selectedDePool.address, AddressLabelType::DePoolAddress);
                     });

                 return EmptyHistoryState{address, isToken, justCreated};
               });
}
}  // namespace Wallet
