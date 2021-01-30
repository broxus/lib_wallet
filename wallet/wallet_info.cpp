// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_info.h"

#include "wallet/wallet_top_bar.h"
#include "wallet/wallet_common.h"
#include "wallet/wallet_cover.h"
#include "wallet/wallet_empty_history.h"
#include "wallet/wallet_history.h"
#include "wallet/wallet_assets_list.h"
#include "wallet/wallet_depool_info.h"
#include "ui/rp_widget.h"
#include "ui/lottie_widget.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/text/text_utilities.h"
#include "styles/style_wallet.h"
#include <ui/wrap/slide_wrap.h>

#include <QtCore/QDateTime>
#include <iostream>

namespace Wallet {

namespace {

auto mapAssetItem(const AssetItem &item) -> SelectedAsset {
  return v::match(
      item,
      [](const TokenItem &item) {
        return SelectedAsset{SelectedToken{
            .token = item.token,
        }};
      },
      [](const DePoolItem &item) { return SelectedAsset{SelectedDePool{.address = item.address}}; });
}

}  // namespace

Info::Info(not_null<QWidget *> parent, Data data)
    : _widget(std::make_unique<Ui::RpWidget>(parent))
    , _scroll(Ui::CreateChild<Ui::ScrollArea>(_widget.get(), st::walletScrollArea))
    , _inner(_scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(_scroll.get())))
    , _selectedAsset(std::nullopt) {
  setupControls(std::move(data));
  _widget->show();
}

Info::~Info() = default;

void Info::setGeometry(QRect geometry) {
  _widget->setGeometry(geometry);
}

rpl::producer<std::optional<SelectedAsset>> Info::selectedAsset() const {
  return _selectedAsset.value();
}

rpl::producer<Action> Info::actionRequests() const {
  return _actionRequests.events();
}

rpl::producer<CustomAsset> Info::removeAssetRequests() const {
  return _removeAssetRequests.events();
}

rpl::producer<std::pair<Ton::Symbol, Ton::TransactionId>> Info::preloadRequests() const {
  return _preloadRequests.events();
}

rpl::producer<Ton::Transaction> Info::viewRequests() const {
  return _viewRequests.events();
}

rpl::producer<Ton::Transaction> Info::decryptRequests() const {
  return _decryptRequests.events();
}

void Info::setupControls(Data &&data) {
  const auto &state = data.state;
  const auto topBar = _widget->lifetime().make_state<TopBar>(
      _widget.get(), MakeTopBarState(rpl::duplicate(state), rpl::duplicate(data.updates),
                                     rpl::duplicate(_selectedAsset.value()), _widget->lifetime()));
  topBar->actionRequests() | rpl::start_to_stream(_actionRequests, topBar->lifetime());

  // ton data stream
  auto loaded =
      std::move(data.loaded)  //
      | rpl::filter(
            [](const Ton::Result<std::pair<Ton::Symbol, Ton::LoadedSlice>> &value) { return value.has_value(); })  //
      | rpl::map([](Ton::Result<std::pair<Ton::Symbol, Ton::LoadedSlice>> &&value) { return std::move(*value); });

  std::move(data.transitionEvents)  //
      | rpl::start_with_next(
            [=](InfoTransition transition) {
              switch (transition) {
                case InfoTransition::Back:
                  _selectedAsset = std::nullopt;
                  return;
                default:
                  return;
              }
            },
            lifetime());

  _inner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  // create wrappers
  const auto assetsListWrapper = Ui::CreateChild<Ui::FixedHeightWidget>(_inner.get(), _widget->height());
  const auto tonHistoryWrapper = Ui::CreateChild<Ui::FixedHeightWidget>(_inner.get(), _widget->height());

  // create tokens list page
  const auto assetsList = _widget->lifetime().make_state<AssetsList>(
      assetsListWrapper, MakeTokensListState(rpl::duplicate(state)), _scroll);

  assetsList->openRequests() |
      rpl::start_with_next([=](AssetItem item) { _selectedAsset = mapAssetItem(item); }, assetsList->lifetime());

  assetsList->gateOpenRequests() |
      rpl::start_with_next([openGate = std::move(data.openGate)]() { openGate(); }, assetsList->lifetime());

  assetsList->addAssetRequests() |
      rpl::start_with_next([=] { _actionRequests.fire(Action::AddAsset); }, assetsList->lifetime());

  assetsList->removeAssetRequests() |
      rpl::start_with_next(
          [=](CustomAsset &&customAsset) { _removeAssetRequests.fire(std::forward<CustomAsset>(customAsset)); },
          assetsList->lifetime());

  // create ton history page

  // top cover
  const auto cover = _widget->lifetime().make_state<Cover>(
      tonHistoryWrapper,
      MakeCoverState(rpl::duplicate(state), _selectedAsset.value(), data.justCreated, data.useTestNetwork));

  // register top cover events
  rpl::merge(cover->sendRequests() | rpl::map([] { return Action::Send; }),
             cover->receiveRequests() | rpl::map([] { return Action::Receive; })) |
      rpl::start_to_stream(_actionRequests, cover->lifetime());

  // create transactions lists
  const auto history = _widget->lifetime().make_state<History>(
      tonHistoryWrapper, MakeHistoryState(rpl::duplicate(state)), std::move(loaded), std::move(data.collectEncrypted),
      std::move(data.updateDecrypted), _selectedAsset.value());

  const auto emptyHistory = _widget->lifetime().make_state<EmptyHistory>(
      tonHistoryWrapper, MakeEmptyHistoryState(rpl::duplicate(state), _selectedAsset.value(), data.justCreated),
      data.share);

  //  const auto dePoolInfo = _widget->lifetime().make_state<DePoolInfo>(
  //      tonHistoryWrapper,
  //      MakeDePoolInfoState(  //
  //          rpl::duplicate(state),
  //          _selectedAsset.value()  //
  //              | rpl::map([](const std::optional<SelectedAsset> &selectedAsset) {
  //                  if (!selectedAsset.has_value()) {
  //                    return QString{};
  //                  }
  //                  return v::match(
  //                      *selectedAsset, [](const SelectedDePool &selectedDePool) { return selectedDePool.address; },
  //                      [](auto &&) { return QString{}; });
  //                })));

  // register layout relations

  // set scroll height to full page
  _widget->sizeValue() |
      rpl::start_with_next(
          [=](QSize size) {
            _scroll->setGeometry(QRect(QPoint(), size).marginsRemoved({0, st::walletTopBarHeight, 0, 0}));
          },
          _scroll->lifetime());

  // set wrappers size same as scroll height
  rpl::combine(_scroll->sizeValue(), assetsList->heightValue(), history->heightValue(),
               //dePoolInfo->heightValue(),
               _selectedAsset.value()) |
      rpl::start_with_next(
          [=](QSize size, int tokensListHeight, int historyHeight,
              //int dePoolInfoHeight,
              std::optional<SelectedAsset> asset) {
            if (asset.has_value()) {
              const auto [contentHeight, historyVisible, dePoolInfoVisible] = v::match(
                  *asset,
                  [&](const SelectedToken &selectedToken) {
                    return std::make_tuple(historyHeight, historyHeight == 0, false);
                  },
                  [&](const SelectedDePool &selectedDePool) {
                    //return std::make_tuple(dePoolInfoHeight, false, true);
                    return std::make_tuple(historyHeight, historyHeight == 0, false);
                  });

              const auto innerHeight = std::max(size.height(), cover->height() + contentHeight);
              _inner->setGeometry({0, 0, size.width(), innerHeight});

              const auto coverHeight = st::walletCoverHeight;

              cover->setGeometry(QRect(0, 0, size.width(), coverHeight));
              emptyHistory->setGeometry(QRect(0, coverHeight, size.width(), size.height() - coverHeight));
              //dePoolInfo->setGeometry(QRect(0, coverHeight, size.width(), size.height() - coverHeight));

              emptyHistory->setVisible(historyVisible);
              //dePoolInfo->setVisible(dePoolInfoVisible);

              tonHistoryWrapper->setGeometry(QRect(0, 0, size.width(), innerHeight));
              history->updateGeometry({0, st::walletCoverHeight}, size.width());
            } else {
              const auto innerHeight = std::max(size.height(), tokensListHeight);
              _inner->setGeometry(QRect(0, 0, size.width(), innerHeight));

              assetsListWrapper->setGeometry(QRect(0, 0, size.width(), innerHeight));
              assetsList->setGeometry(QRect(0, 0, size.width(), innerHeight));
            }
          },
          _scroll->lifetime());

  rpl::combine(_scroll->scrollTopValue(), _scroll->heightValue(), _selectedAsset.value()) |
      rpl::start_with_next(
          [=](int scrollTop, int scrollHeight, const std::optional<SelectedAsset> &asset) {
            if (asset.has_value()) {
              history->setVisibleTopBottom(scrollTop, scrollTop + scrollHeight);
            }
          },
          history->lifetime());

  history->preloadRequests() | rpl::start_to_stream(_preloadRequests, history->lifetime());

  history->viewRequests() | rpl::start_to_stream(_viewRequests, history->lifetime());

  history->decryptRequests() | rpl::start_to_stream(_decryptRequests, history->lifetime());

  // initialize default layouts
  _selectedAsset.value() | rpl::start_with_next(
                               [=](std::optional<SelectedAsset> token) {
                                 assetsListWrapper->setVisible(!token.has_value());
                                 tonHistoryWrapper->setVisible(token.has_value());
                               },
                               lifetime());
}

rpl::lifetime &Info::lifetime() {
  return _widget->lifetime();
}

}  // namespace Wallet
