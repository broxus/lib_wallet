#include "wallet_assets_list.h"

#include "wallet/wallet_common.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/address_label.h"
#include "ui/image/image_prepare.h"
#include "ton/ton_wallet.h"
#include "styles/style_wallet.h"
#include "styles/palette.h"
#include "ui/layers/generic_box.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "wallet_phrases.h"

#include <QtWidgets/qlayout.h>
#include <iostream>

namespace Wallet {

namespace {

enum class LayoutType { Compact, Full };

struct AssetItemLayout {
  QImage image;
  Ui::Text::String title;
  Ui::Text::String balanceGrams;
  Ui::Text::String balanceNano;
  Ui::Text::String address;
  int addressWidth = 0;
  Ui::Text::String outdated;
  LayoutType type;
};

int assetRowHeight(LayoutType type) {
  return type == LayoutType::Compact ? st::walletTokensListCompactRowHeight : st::walletTokensListRowHeight;
}

[[nodiscard]] const style::TextStyle &addressStyle() {
  const static auto result = Ui::ComputeAddressStyle(st::defaultTextStyle);
  return result;
}

auto addressPartWidth(const QString &address, int from, int length = -1) {
  return addressStyle().font->width(address.mid(from, length));
}

[[nodiscard]] AssetItemLayout prepareLayout(const AssetItem &data) {
  const auto [type, title, token, address, balance, outdated] = v::match(
      data,
      [](const TokenItem &item) {
        const auto layoutType = item.token.isToken() ? LayoutType::Compact : LayoutType::Full;

        return std::make_tuple(layoutType, item.token.name(), item.token,
                               item.token.isTon() ? Ton::Wallet::ConvertIntoRaw(item.address) : QString{}, item.balance,
                               item.outdated);
      },
      [](const DePoolItem &item) {
        return std::make_tuple(LayoutType::Full, QString{"DePool"}, Ton::Symbol::ton(),
                               Ton::Wallet::ConvertIntoRaw(item.address), int128{item.total}, false);
      },
      [](const MultisigItem &item) {
        return std::make_tuple(LayoutType::Full, QString{"Msig"}, Ton::Symbol::ton(),
                               Ton::Wallet::ConvertIntoRaw(item.address), int128{item.balance}, false);
      });

  const auto formattedBalance = FormatAmount(balance > 0 ? balance : 0, token);

  auto result = AssetItemLayout();
  result.type = type;
  result.image = Ui::InlineTokenIcon(token, st::walletTokensListRowIconSize);
  result.title.setText(st::walletTokensListRowTitleStyle.style, title);

  result.balanceGrams.setText(st::walletTokensListRowGramsStyle, formattedBalance.gramsString);

  result.balanceNano.setText(st::walletTokensListRowNanoStyle,
                             formattedBalance.separator + formattedBalance.nanoString);

  if (!address.isEmpty()) {
    result.address = Ui::Text::String(addressStyle(), address, _defaultOptions, st::walletAddressWidthMin);
    result.addressWidth = (addressStyle().font->spacew / 2) + std::max(addressPartWidth(address, 0, address.size() / 2),
                                                                       addressPartWidth(address, address.size() / 2));
  }

  if (outdated) {
    result.outdated = Ui::Text::String(st::walletTokensListOutdatedStyle, "old");
  }

  return result;
}

}  // namespace

class AssetsListRow final {
 public:
  explicit AssetsListRow(const AssetItem &item) : _data(item), _layout(prepareLayout(item)) {
  }

  AssetsListRow(const AssetsListRow &) = delete;
  AssetsListRow &operator=(const AssetsListRow &) = delete;
  ~AssetsListRow() = default;

  void paint(Painter &p, int /*x*/, int /*y*/) const {
    const auto padding = st::walletTokensListRowContentPadding;

    const auto availableWidth = _width - padding.left() - padding.right();
    const auto availableHeight = _height - padding.top() - padding.bottom();

    // draw icon
    const auto iconTop = padding.top() * 2;
    const auto iconLeft = iconTop;

    {
      PainterHighQualityEnabler hq(p);
      p.setPen(Qt::NoPen);
      p.setBrush(st::windowBgRipple);
      p.drawRoundedRect(QRect(iconLeft, iconTop, st::walletTokensListRowIconSize, st::walletTokensListRowIconSize),
                        st::roundRadiusLarge, st::roundRadiusLarge);
    }
    p.drawImage(iconLeft, iconTop, _layout.image);

    if (_layout.type == LayoutType::Full) {
      // draw asset name
      p.setPen(st::walletTokensListRowTitleStyle.textFg);
      const auto titleTop = iconTop + st::walletTokensListRowIconSize;
      const auto titleLeft = iconLeft + (st::walletTokensListRowIconSize - _layout.title.maxWidth()) / 2;
      _layout.title.draw(p, titleLeft, titleTop, availableWidth);
    }

    // draw balance
    p.setPen(st::walletTokensListRow.textFg);

    const auto nanoTop =
        padding.top() + st::walletTokensListRowGramsStyle.font->ascent - st::walletTokensListRowNanoStyle.font->ascent;
    const auto nanoLeft = availableWidth - _layout.balanceNano.maxWidth();
    _layout.balanceNano.draw(p, nanoLeft, nanoTop, availableWidth);

    const auto gramTop = padding.top();
    const auto gramLeft = availableWidth - _layout.balanceNano.maxWidth() - _layout.balanceGrams.maxWidth();
    _layout.balanceGrams.draw(p, gramLeft, gramTop, availableWidth);

    if (_layout.type == LayoutType::Compact) {
      // draw asset name
      p.setPen(st::walletTokensListRowTitleStyle.textFg);
      const auto titleTop = iconTop + _layout.image.height() - _layout.title.minHeight();
      _layout.title.drawRight(p, 0, titleTop, _layout.title.maxWidth(), availableWidth);
    }

    if (_layout.type == LayoutType::Full) {
      // draw address
      p.setPen(st::walletTokensListRowTitleStyle.textFg);

      const auto addressTop = availableHeight - padding.bottom() - addressStyle().font->ascent * 2;
      _layout.address.drawRightElided(p, padding.right(), addressTop, _layout.addressWidth, _width - padding.right(),
                                      /*lines*/ 2, style::al_bottomright,
                                      /*yFrom*/ 0,
                                      /*yTo*/ -1,
                                      /*removeFromEnd*/ 0,
                                      /*breakEverywhere*/ true);
    }

    if (!_layout.outdated.isEmpty()) {
      const auto outdatedLeft = 0;
      const auto outdatedTop = iconTop;

      const auto leftOffset = _layout.outdated.style()->font->width(QChar{' '});

      p.translate(outdatedLeft, outdatedTop);
      p.rotate(-45);
      p.fillRect(-availableWidth, 0, availableWidth * 2, _layout.outdated.minHeight(), st::boxTextFgError->c);
      p.setPen(st::windowBg->c);
      _layout.outdated.draw(p, leftOffset, 0, availableWidth);
    }
  }

  bool refresh(const AssetItem &item) {
    if (_data == item) {
      return false;
    }

    _layout = prepareLayout(item);
    _data = item;
    return true;
  }

  void resizeToWidth(int width) {
    if (_width == width) {
      return;
    }

    _width = width;
    _height = assetRowHeight(layoutType());
    // TODO: handle contents resize
  }

  LayoutType layoutType() {
    return _layout.type;
  }

  int height() {
    return _height;
  }

  [[nodiscard]] auto data() const -> const AssetItem & {
    return _data;
  }

 private:
  AssetItem _data;
  AssetItemLayout _layout;
  int _width = 0;
  int _height = 0;
};

AssetsList::~AssetsList() = default;

AssetsList::AssetsList(not_null<Ui::RpWidget *> parent, rpl::producer<AssetsListState> state,
                       not_null<Ui::ScrollArea *> scroll)
    : _widget(parent), _scroll(scroll) {
  setupContent(std::move(state));
}

void AssetsList::setGeometry(QRect geometry) {
  _widget.setGeometry(geometry);
}

rpl::producer<AssetItem> AssetsList::openRequests() const {
  return _openRequests.events();
}

rpl::producer<> AssetsList::gateOpenRequests() const {
  return _gateOpenRequests.events();
}

rpl::producer<> AssetsList::addAssetRequests() const {
  return _addAssetRequests.events();
}

rpl::producer<CustomAsset> AssetsList::removeAssetRequests() const {
  return _removeAssetRequests.events();
}

rpl::producer<std::pair<int, int>> AssetsList::reorderAssetRequests() const {
  return _reorderAssetRequests.events();
}

rpl::producer<int> AssetsList::heightValue() const {
  return _height.value();
}

rpl::lifetime &AssetsList::lifetime() {
  return _widget.lifetime();
}

void AssetsList::setupContent(rpl::producer<AssetsListState> &&state) {
  _widget.paintRequest() |
      rpl::start_with_next([=](QRect clip) { Painter(&_widget).fillRect(clip, st::walletTopBg); }, lifetime());

  // title
  const auto titleLabel =
      Ui::CreateChild<Ui::FlatLabel>(&_widget, ph::lng_wallet_tokens_list_accounts(), st::walletTokensListTitle);
  titleLabel->show();

  const auto addAsset =
      Ui::CreateChild<Ui::RoundButton>(&_widget, ph::lng_wallet_tokens_list_add(), st::walletCoverButton);
  addAsset->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);

  addAsset->clicks() | rpl::start_with_next([=] { _addAssetRequests.fire({}); }, addAsset->lifetime());

  // content
  const auto layout = Ui::CreateChild<Ui::VerticalLayout>(&_widget);
  layout->setContentsMargins(st::walletTokensListPadding);

  const auto wasReordered = lifetime().make_state<bool>(false);

  const auto reorder = Ui::CreateChild<Ui::VerticalLayoutReorder>(layout, layout, _scroll);
  reorder->updates()  //
      | rpl::start_with_next(
            [this, wasReordered](Ui::VerticalLayoutReorder::Single event) {
              switch (event.state) {
                case Ui::VerticalLayoutReorder::State::Started: {
                  *wasReordered = true;
                  return;
                }
                case Ui::VerticalLayoutReorder::State::Applied: {
                  base::reorder(_buttons, event.oldPosition, event.newPosition);
                  for (int i = 0; i < _buttons.size(); ++i) {
                    *_buttons[i].index = i;
                  }
                  _reorderAssetRequests.fire(std::make_pair(event.oldPosition, event.newPosition));
                }
                default:
                  return;
              }
            },
            layout->lifetime());

  // open gate button
  const auto topSectionHeight = st::walletTokensListRowsTopOffset;
  const auto contentHeight = lifetime().make_state<rpl::variable<int>>(st::walletTokensListPadding.top() +
                                                                       st::walletTokensListPadding.bottom());

  _height = topSectionHeight + contentHeight->current();

  //
  rpl::combine(_widget.sizeValue(), contentHeight->value()) |
      rpl::start_with_next(
          [=](QSize size, int) {
            const auto width = std::min(size.width(), st::walletRowWidthMax);
            const auto left = (size.width() - width) / 2;

            titleLabel->move(left + st::walletTokensListPadding.left(), st::walletTokensListPadding.top());
            addAsset->move(left + width - addAsset->width() - st::walletTokensListPadding.left(),
                           st::walletTokensListPadding.top());

            const auto paddingLeft = st::walletTokensListPadding.left();
            const auto paddingRight = st::walletTokensListPadding.right();
            const auto layoutWidth = std::max(width - paddingLeft - paddingRight, 0);

            layout->setGeometry(QRect(left + paddingLeft, topSectionHeight, layoutWidth, layout->size().height()));

            for (const auto &item : _buttons) {
              item.button->setFixedWidth(layoutWidth);
            }
          },
          lifetime());

  //
  std::forward<std::decay_t<decltype(state)>>(state) |
      rpl::start_with_next(
          [=](AssetsListState &&state) {
            refreshItemValues(state);
            if (!mergeListChanged(std::move(state))) {
              return _widget.update();
            }

            int totalHeight = 0;
            for (size_t i = 0; i < _rows.size(); ++i) {
              int rowHeight = assetRowHeight(_rows[i]->layoutType());
              totalHeight += rowHeight + st::walletTokensListRowSpacing;

              if (i < _buttons.size()) {
                _buttons[i].button->setFixedHeight(rowHeight);
                // skip already existing buttons
                continue;
              }

              auto button = object_ptr<Ui::RoundButton>(layout, rpl::single(QString()), st::walletTokensListRow);
              auto buttonIndex = std::make_shared<int>(i);

              auto *label = Ui::CreateChild<Ui::FixedHeightWidget>(button.data());
              button->sizeValue()  //
                  | rpl::start_with_next(
                        [=](QSize size) { label->setGeometry(QRect(0, 0, size.width(), size.height())); },
                        button->lifetime());

              label->paintRequest()  //
                  | rpl::start_with_next(
                        [this, label, i = buttonIndex](QRect clip) {
                          if (*i >= _rows.size()) {
                            return;
                          }
                          auto p = Painter(label);
                          _rows[*i]->resizeToWidth(label->width());
                          _rows[*i]->paint(p, clip.left(), clip.top());
                        },
                        label->lifetime());

              button->events() |
                  rpl::start_with_next(
                      [this, wasReordered, button = button.data(), i = buttonIndex](not_null<QEvent *> event) {
                        if (*i >= _rows.size()) {
                          return;
                        }
                        switch (event->type()) {
                          case QEvent::Type::ContextMenu: {
                            const auto persistent = v::match(
                                _rows[*i]->data(), [](const TokenItem &tokenItem) { return tokenItem.token.isTon(); },
                                [](auto &&) { return false; });
                            if (persistent) {
                              return;
                            }
                            const auto *e = dynamic_cast<QContextMenuEvent *>(event.get());
                            auto *menu = new QMenu(&_widget);
                            menu->addAction(ph::lng_wallet_tokens_list_delete_item(ph::now), [=] {
                              if (*i >= _rows.size()) {
                                return;
                              }
                              _removeAssetRequests.fire(v::match(
                                  _rows[*i]->data(),
                                  [](const TokenItem &tokenItem) {
                                    return CustomAsset{.type = CustomAssetType::Token, .symbol = tokenItem.token};
                                  },
                                  [](const DePoolItem &dePoolItem) {
                                    return CustomAsset{.type = CustomAssetType::DePool, .address = dePoolItem.address};
                                  },
                                  [](const MultisigItem &multisigItem) {
                                    return CustomAsset{.type = CustomAssetType::Multisig,
                                                       .address = multisigItem.address};
                                  }));
                            });
                            return (new Ui::PopupMenu(&_widget, menu))->popup(e->globalPos());
                          }
                          case QEvent::Type::MouseButtonPress: {
                            if (dynamic_cast<QMouseEvent *>(event.get())->button() == Qt::MouseButton::LeftButton) {
                              *wasReordered = false;
                            }
                            return;
                          }
                          case QEvent::Type::MouseButtonRelease: {
                            if (dynamic_cast<QMouseEvent *>(event.get())->button() == Qt::MouseButton::LeftButton &&
                                !*wasReordered) {
                              _openRequests.fire_copy(_rows[*i]->data());
                            }
                            return;
                          }
                          default:
                            return;
                        }
                      },
                      button->lifetime());

              button->setFixedHeight(rowHeight);

              _buttons.emplace_back(ButtonState{
                  .button = layout->add(std::move(button), QMargins{0, st::walletTokensListRowSpacing, 0, 0}),
                  .index = std::move(buttonIndex)});
            }

            reorder->cancel();

            while (_buttons.size() > _rows.size()) {
              // remove unused buttons
              const auto &item = _buttons.back();
              layout->removeChild(item.button);
              _buttons.pop_back();
            }

            *contentHeight = totalHeight - (_rows.empty() ? 0 : st::walletTokensListRowSpacing) +
                             st::walletTokensListPadding.top() + st::walletTokensListPadding.bottom();

            layout->setMinimumHeight(std::max(contentHeight->current(), _widget.height()));
            _height = topSectionHeight + contentHeight->current();

            reorder->start();

            _widget.update();
          },
          lifetime());
}

void AssetsList::refreshItemValues(const AssetsListState &data) {
  for (size_t i = 0; i < _rows.size() && i < data.items.size(); ++i) {
    _rows[i]->refresh(data.items[i]);
  }
}

bool AssetsList::mergeListChanged(AssetsListState &&data) {
  if (_rows.size() == data.items.size()) {
    return false;
  }

  while (_rows.size() > data.items.size()) {
    _rows.pop_back();
  }

  for (size_t i = _rows.size(); i < data.items.size(); ++i) {
    _rows.emplace_back(std::make_unique<AssetsListRow>(data.items[i]));
  }

  return true;
}

rpl::producer<AssetsListState> MakeTokensListState(rpl::producer<Ton::WalletViewerState> state) {
  return std::move(state) | rpl::map([=](const Ton::WalletViewerState &data) {
           const auto &account = data.wallet.account;
           const auto unlockedTonBalance = account.fullBalance - account.lockedBalance;

           AssetsListState result{};
           result.items.reserve(data.wallet.assetsList.size());

           for (const auto &item : data.wallet.assetsList) {
             result.items.emplace_back(v::match(
                 item,
                 [&](const Ton::AssetListItemWallet &) -> AssetItem {
                   return TokenItem{
                       .token = Ton::Symbol::ton(),
                       .address = data.wallet.address,
                       .balance = unlockedTonBalance,
                   };
                 },
                 [&](const Ton::AssetListItemDePool &dePool) -> AssetItem {
                   const auto it = data.wallet.dePoolParticipantStates.find(dePool.address);
                   if (it != end(data.wallet.dePoolParticipantStates)) {
                     return DePoolItem{
                         .address = dePool.address, .total = it->second.total, .reward = it->second.reward};
                   } else {
                     return DePoolItem{.address = dePool.address};
                   }
                 },
                 [&](const Ton::AssetListItemToken &token) -> AssetItem {
                   const auto it = data.wallet.tokenStates.find(token.symbol);
                   if (it != end(data.wallet.tokenStates)) {
                     return TokenItem{
                         .token = token.symbol,
                         .address = it->second.walletContractAddress,
                         .balance = it->second.balance,
                         .outdated = it->second.shouldUpdate().has_value(),
                     };
                   } else {
                     return TokenItem{.token = token.symbol, .address = Ton::kZeroAddress};
                   }
                 },
                 [&](const Ton::AssetListItemMultisig &multisig) -> AssetItem {
                   const auto it = data.wallet.multisigStates.find(multisig.address);
                   if (it != end(data.wallet.multisigStates)) {
                     const auto &accountState = it->second.accountState;
                     return MultisigItem{
                         .address = it->first,
                         .balance = accountState.fullBalance - accountState.lockedBalance,
                     };
                   } else {
                     return MultisigItem{
                         .address = it->first,
                         .balance = 0,
                     };
                   }
                 }));
           }
           return result;
         });
}

bool operator==(const AssetItem &a, const AssetItem &b) {
  if (a.index() != b.index()) {
    return false;
  }

  return v::match(
      a,
      [&](const TokenItem &left) {
        const auto &right = v::get<TokenItem>(b);
        return left.address == right.address && left.balance == right.balance && left.token == right.token;
      },
      [&](const DePoolItem &left) {
        const auto &right = v::get<DePoolItem>(b);
        return left.address == right.address && left.reward == right.reward && left.total == right.total;
      },
      [&](const MultisigItem &left) {
        const auto &right = v::get<MultisigItem>(b);
        return left.address == right.address && left.balance == right.balance;
      });
}

bool operator!=(const AssetItem &a, const AssetItem &b) {
  return !(a == b);
}

}  // namespace Wallet
