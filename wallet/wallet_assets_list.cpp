#include "wallet_assets_list.h"

#include "wallet/wallet_common.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/address_label.h"
#include "ui/image/image_prepare.h"
#include "ton/ton_state.h"
#include "styles/style_wallet.h"
#include "styles/palette.h"
#include "ui/layers/generic_box.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "wallet_phrases.h"

#include <QtWidgets/qlayout.h>
#include <QContextMenuEvent>
#include <iostream>

namespace Wallet {

namespace {
struct AssetItemLayout {
  QImage image;
  Ui::Text::String title;
  Ui::Text::String balanceGrams;
  Ui::Text::String balanceNano;
  Ui::Text::String address;
  int addressWidth = 0;
};

[[nodiscard]] const style::TextStyle &addressStyle() {
  const static auto result = Ui::ComputeAddressStyle(st::defaultTextStyle);
  return result;
}

auto addressPartWidth(const QString &address, int from, int length = -1) {
  return addressStyle().font->width(address.mid(from, length));
}

[[nodiscard]] AssetItemLayout prepareLayout(const AssetItem &data) {
  const auto [title, token, address, balance] = v::match(
      data,
      [](const TokenItem &item) { return std::make_tuple(item.token.name(), item.token, item.address, item.balance); },
      [](const DePoolItem &item) {
        return std::make_tuple(QString{"DePool"}, Ton::Symbol::ton(), item.address, item.total);
      });

  const auto formattedBalance = FormatAmount(std::max(balance, int64_t{}), token);

  auto result = AssetItemLayout();
  result.image = Ui::InlineTokenIcon(token, st::walletTokensListRowIconSize);
  result.title.setText(st::walletTokensListRowTitleStyle.style, title);

  result.balanceGrams.setText(st::walletTokensListRowGramsStyle, formattedBalance.gramsString);

  result.balanceNano.setText(st::walletTokensListRowNanoStyle,
                             formattedBalance.separator + formattedBalance.nanoString);

  result.address = Ui::Text::String(addressStyle(), address, _defaultOptions, st::walletAddressWidthMin);
  result.addressWidth = (addressStyle().font->spacew / 2) + std::max(addressPartWidth(address, 0, address.size() / 2),
                                                                     addressPartWidth(address, address.size() / 2));

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

    // draw token name
    p.setPen(st::walletTokensListRowTitleStyle.textFg);
    const auto titleTop = iconTop + st::walletTokensListRowIconSize;
    const auto titleLeft = iconLeft + (st::walletTokensListRowIconSize - _layout.title.maxWidth()) / 2;
    _layout.title.draw(p, titleLeft, titleTop, availableWidth);

    // draw balance
    p.setPen(st::walletTokensListRow.textFg);

    const auto nanoTop =
        padding.top() + st::walletTokensListRowGramsStyle.font->ascent - st::walletTokensListRowNanoStyle.font->ascent;
    const auto nanoLeft = availableWidth - _layout.balanceNano.maxWidth();
    _layout.balanceNano.draw(p, nanoLeft, nanoTop, availableWidth);

    const auto gramTop = padding.top();
    const auto gramLeft = availableWidth - _layout.balanceNano.maxWidth() - _layout.balanceGrams.maxWidth();
    _layout.balanceGrams.draw(p, gramLeft, gramTop, availableWidth);

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
    _height = st::walletTokensListRowHeight;
    // TODO: handle contents resize
  }

  auto data() const -> const AssetItem & {
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
                }
                default:
                  return;
              }
            },
            layout->lifetime());

  // open gate button
  const auto gateButton =
      Ui::CreateChild<Ui::RoundButton>(&_widget, ph::lng_wallet_tokens_list_swap(), st::walletCoverButton);
  gateButton->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);

  gateButton->clicks() | rpl::start_with_next([=] { _gateOpenRequests.fire({}); }, gateButton->lifetime());

  const auto topSectionHeight = st::walletTokensListRowsTopOffset;
  const auto bottomSectionHeight = gateButton->height() + 2 * st::walletTokensListGateButtonOffset;
  const auto contentHeight = lifetime().make_state<rpl::variable<int>>(
      st::walletTokensListPadding.top() + st::walletTokensListPadding.bottom() + bottomSectionHeight);

  _height = topSectionHeight + contentHeight->current() + bottomSectionHeight;

  //
  rpl::combine(_widget.sizeValue(), contentHeight->value()) |
      rpl::start_with_next(
          [=](QSize size, int) {
            const auto height = contentHeight->current();
            std::cout << "New height: " << size.height() << ", " << height << std::endl;

            const auto width = std::min(size.width(), st::walletRowWidthMax);
            const auto left = (size.width() - width) / 2;

            const auto gateButtonWidth = width / 2;
            const auto gateButtonTop =
                std::max((topSectionHeight + height + (bottomSectionHeight - gateButton->height()) / 2),
                         (size.height() - (bottomSectionHeight + gateButton->height()) / 2));

            titleLabel->move(left + st::walletTokensListPadding.left(), st::walletTokensListPadding.top());
            addAsset->move(left + width - addAsset->width() - st::walletTokensListPadding.left(),
                           st::walletTokensListPadding.top());

            const auto paddingLeft = st::walletTokensListPadding.left();
            const auto paddingRight = st::walletTokensListPadding.right();
            const auto layoutWidth = std::max(width - paddingLeft - paddingRight, 0);

            layout->setGeometry(QRect(left + paddingLeft, topSectionHeight, layoutWidth, layout->size().height()));

            gateButton->setGeometry(
                QRect((size.width() - gateButtonWidth) / 2, gateButtonTop, gateButtonWidth, gateButton->height()));

            std::cout << "Current top: " << gateButtonTop << " = " << gateButton->geometry().top() << std::endl;

            for (const auto &item : _buttons) {
              item.button->setFixedWidth(layoutWidth);
            }
          },
          lifetime());

  //
  std::move(state) |
      rpl::start_with_next(
          [=](AssetsListState &&state) {
            refreshItemValues(state);
            if (!mergeListChanged(std::move(state))) {
              return;
            }

            for (size_t i = 0; i < _rows.size(); ++i) {
              if (i < _buttons.size()) {
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
                            auto *menu = new QMenu(button);
                            menu->addAction(ph::lng_wallet_tokens_list_delete_item(ph::now), [=] {
                              _removeAssetRequests.fire(v::match(
                                  _rows[*i]->data(),
                                  [](const TokenItem &tokenItem) {
                                    return CustomAsset{.type = CustomAssetType::Token, .address = tokenItem.address};
                                  },
                                  [](const DePoolItem &dePoolItem) {
                                    return CustomAsset{.type = CustomAssetType::DePool, .address = dePoolItem.address};
                                  }));
                            });
                            return (new Ui::PopupMenu(button, menu))->popup(e->globalPos());
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

              button->setFixedHeight(st::walletTokensListRowHeight);

              _buttons.emplace_back(ButtonState{
                  .button = layout->add(std::move(button), QMargins{0, st::walletTokensListRowSpacing, 0, 0}),
                  .index = std::move(buttonIndex)});
            }

            reorder->cancel();

            while (_buttons.size() > _rows.size()) {
              std::cout << "Removed: " << *_buttons.back().index << std::endl;

              // remove unused buttons
              const auto &item = _buttons.back();
              layout->removeChild(item.button);
              _buttons.pop_back();
            }

            *contentHeight =
                static_cast<int>(_rows.size()) * (st::walletTokensListRowHeight + st::walletTokensListRowSpacing) -
                (_rows.empty() ? 0 : st::walletTokensListRowSpacing) + st::walletTokensListPadding.top() +
                st::walletTokensListPadding.bottom();

            layout->setMinimumHeight(std::max(contentHeight->current() + bottomSectionHeight, _widget.height()));
            _height = topSectionHeight + contentHeight->current() + bottomSectionHeight;

            reorder->start();
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

           result.items.emplace_back(TokenItem{
               .token = Ton::Symbol::ton(),
               .address = data.wallet.address,
               .balance = unlockedTonBalance,
           });

           for (const auto &[address, state] : data.wallet.dePoolParticipantStates) {
             result.items.emplace_back(DePoolItem{.address = address, .total = state.total, .reward = state.reward});
           }

           for (const auto &[token, state] : data.wallet.tokenStates) {
             result.items.emplace_back(TokenItem{
                 .token = token,
                 .address = state.walletContractAddress,
                 .balance = state.balance,
             });
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
      });
}

bool operator!=(const AssetItem &a, const AssetItem &b) {
  return !(a == b);
}

}  // namespace Wallet
