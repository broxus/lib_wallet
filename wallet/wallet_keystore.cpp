#include "wallet_keystore.h"

#include "wallet/wallet_phrases.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/address_label.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "wallet_common.h"

namespace Wallet {

namespace {

style::TextStyle ComputePubKeyStyle(const style::TextStyle &parent) {
  auto result = parent;
  result.font = result.font->monospace();
  result.linkFont = result.linkFont->monospace();
  result.linkFontOver = result.linkFontOver->monospace();
  return result;
}

QString PublicIntoRaw(const QByteArray &publicKey) {
  auto decoded = QByteArray::fromBase64(publicKey, QByteArray::Base64Option::Base64UrlEncoding);
  return decoded.mid(2, 32).toHex();
}

using DividerOffset = Ui::BoxContentDivider::Offset;

not_null<Ui::RpWidget *> CreatePubKeyLabel(not_null<Ui::RpWidget *> parent, rpl::producer<QString> &&text,
                                           const style::FlatLabel &st, const Fn<void()> &onClick) {
  const auto mono = parent->lifetime().make_state<style::FlatLabel>(st);
  mono->style = ComputePubKeyStyle(mono->style);
  mono->minWidth = 50;

  const auto result = CreateChild<Ui::RpWidget>(parent.get());
  const auto label = CreateChild<Ui::FlatLabel>(result, rpl::duplicate(text), *mono);
  label->setBreakEverywhere(true);

  label->setAttribute(Qt::WA_TransparentForMouseEvents);
  result->setCursor(style::cur_pointer);
  result->events()  //
      | rpl::start_with_next(
            [=](not_null<QEvent *> event) {
              if (event->type() != QEvent::MouseButtonRelease) {
                return;
              }
              const auto *e = dynamic_cast<QMouseEvent *>(event.get());
              if (e->button() == Qt::MouseButton::LeftButton) {
                onClick();
              }
            },
            result->lifetime());

  std::forward<std::decay_t<decltype(text)>>(text)  //
      | rpl::start_with_next(
            [mono, label, result](QString &&text) {
              const auto half = text.size() / 2;
              const auto first = text.mid(0, half);
              const auto second = text.mid(half);
              const auto width = std::max(mono->style.font->width(first), mono->style.font->width(second)) +
                                 mono->style.font->spacew / 2;
              label->resizeToWidth(width);
              result->resize(label->size());
            },
            parent->lifetime());

  result->widthValue()  //
      | rpl::start_with_next(
            [=](int width) {
              if (st.align & Qt::AlignHCenter) {
                label->moveToLeft((width - label->widthNoMargins()) / 2, label->getMargins().top(), width);
              } else {
                label->moveToLeft(0, label->getMargins().top(), width);
              }
            },
            result->lifetime());

  return result;
}

}  // namespace

void KeystoreBox(not_null<Ui::GenericBox *> box, const QByteArray &mainPublicKey,
                 const std::vector<Ton::FtabiKey> &ftabiKeys, const Fn<void(QString)> &share,
                 const Fn<void()> &createFtabiKey) {
  box->setWidth(st::boxWideWidth);
  box->setStyle(st::walletBox);
  box->setNoContentMargin(true);
  box->setTitle(ph::lng_wallet_keystore_title());

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  auto widget = box->lifetime().make_state<Ui::RpWidget>();
  auto scroll = Ui::CreateChild<Ui::ScrollArea>(widget, st::walletScrollArea);
  auto inner = scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(scroll)).data();

  int desiredHeight = 0;

  std::vector<Ui::RpWidget *> dividers;
  const auto addDivider = [&] {
    const auto &margin = st::walletSettingsDividerMargin;
    auto *divider = inner->add(object_ptr<Ui::BoxContentDivider>(widget), margin);
    desiredHeight += margin.top() + divider->height() + margin.bottom();
    dividers.emplace_back(divider);
  };

  std::vector<Ui::RpWidget *> items;
  auto addItem = [&](const QByteArray &pubkey) {
    const auto key = PublicIntoRaw(pubkey);

    auto item = inner->add(object_ptr<Ui::VerticalLayout>(box), QMargins{});
    items.emplace_back(item);

    const auto title = AddBoxSubtitle(item, rpl::single(QString{"Main wallet key"}));
    title->setSelectable(true);
    title->setContextMenuPolicy(Qt::ContextMenuPolicy::ActionsContextMenu);

    const auto &titlePadding = st::walletSubsectionTitlePadding;
    desiredHeight += titlePadding.top() + title->height() + titlePadding.bottom();

    const auto &labelPadding = st::boxRowPadding;
    const auto label = item->add(  //
        object_ptr<Ui::RpWidget>::fromRaw(
            CreatePubKeyLabel(box, rpl::single(key), st::walletTransactionAddress, [=] { share(key); })),
        labelPadding);
    desiredHeight += labelPadding.top() + label->height() + labelPadding.bottom();

    item->events()  //
        | rpl::start_with_next(
              [=](not_null<QEvent *> event) {
                switch (event->type()) {
                  case QEvent::Type::ContextMenu: {
                    const auto *e = dynamic_cast<QContextMenuEvent *>(event.get());
                    auto *menu = new QMenu(item);
                    menu->addAction(ph::lng_wallet_tokens_list_delete_item(ph::now), [=] {

                    });
                    return (new Ui::PopupMenu(item, menu))->popup(e->globalPos());
                  }
                  default:
                    return;
                }
              },
              item->lifetime());
  };

  addDivider();
  addItem(mainPublicKey);

  for (int i = 0; i < 10; ++i) {
    addDivider();
    addItem(mainPublicKey);
  }
  addDivider();

  widget->sizeValue()  //
      | rpl::start_with_next(
            [=](QSize size) {
              for (auto divider : dividers) {
                divider->setFixedWidth(size.width());
              }
              for (auto item : items) {
                item->setFixedWidth(size.width());
              }
              scroll->setGeometry({QPoint{}, size});
              inner->setGeometry(0, 0, size.width(), std::max(desiredHeight, size.height()));
            },
            box->lifetime());

  widget->resize(st::boxWideWidth, desiredHeight);

  box->addRow(object_ptr<Ui::RpWidget>::fromRaw(widget), QMargins());

  box->addButton(ph::lng_wallet_keystore_create(), createFtabiKey, st::walletWideBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

}  // namespace Wallet
