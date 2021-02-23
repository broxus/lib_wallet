// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_view_transaction.h"

#include "wallet/wallet_common.h"
#include "wallet/wallet_phrases.h"
#include "ui/amount_label.h"
#include "ui/address_label.h"
#include "ui/lottie_widget.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/text/text_utilities.h"
#include "ton/ton_state.h"
#include "base/unixtime.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/palette.h"
#include "ton/ton_wallet.h"

#include <QtCore/QDateTime>
#include <QtGui/QtEvents>

namespace Wallet {
namespace {

struct TokenTransaction {
  Ton::Symbol token;
  QString recipient;
  int128 amount;
  bool incoming{};
  bool swapback{};
  bool mint{};
  bool direct{};
};

enum class NotificationType {
  EthEvent,
  TonEvent,
};

struct Notification {
  NotificationType type;
  QString eventAddress;
};

std::optional<TokenTransaction> TryGetTokenTransaction(const Ton::Transaction &data, const Ton::Symbol &selectedToken) {
  using ReturnType = std::optional<TokenTransaction>;
  return v::match(
      data.additional,
      [&](const Ton::TokenTransfer &transfer) -> ReturnType {
        return TokenTransaction{
            .token = selectedToken,
            .recipient = transfer.address,
            .amount = transfer.value,
            .incoming = transfer.incoming,
            .direct = transfer.direct,

        };
      },
      [&](const Ton::TokenSwapBack &swapBack) -> ReturnType {
        return TokenTransaction{
            .token = selectedToken,
            .recipient = swapBack.address,
            .amount = swapBack.value,
            .incoming = true,
            .swapback = true,
        };
      },
      [&](const Ton::TokenMint &tokenMint) -> ReturnType {
        return TokenTransaction{
            .token = selectedToken,
            .amount = tokenMint.value,
            .incoming = true,
            .mint = true,
        };
      },
      [&](const Ton::TokensBounced &tokensBounced) -> ReturnType {
        return TokenTransaction{
            .token = selectedToken,
            .amount = tokensBounced.amount,
            .incoming = true,
        };
      },
      [](auto &&) -> ReturnType { return std::nullopt; });
}

std::optional<Notification> TryGetNotification(const Ton::Transaction &data) {
  using ReturnType = std::optional<Notification>;
  return v::match(
      data.additional,
      [&](const Ton::EthEventStatusChanged &event) -> ReturnType {
        return Notification{.type = NotificationType::EthEvent, .eventAddress = data.incoming.source};
      },
      [&](const Ton::TonEventStatusChanged &event) -> ReturnType {
        return Notification{.type = NotificationType::TonEvent, .eventAddress = data.incoming.source};
      },
      [](auto &&) -> ReturnType { return std::nullopt; });
}

object_ptr<Ui::RpWidget> CreateSummary(not_null<Ui::RpWidget *> parent, const Ton::Transaction &data,
                                       const std::optional<TokenTransaction> &tokenTransaction) {
  const auto isTokenTransaction = tokenTransaction.has_value();
  const auto token = isTokenTransaction ? tokenTransaction->token : Ton::Symbol::ton();

  const auto showTransactionFee = isTokenTransaction || data.otherFee > 0;
  const auto showStorageFee = data.storageFee > 0;

  const auto feeSkip = st::walletTransactionFeeSkip;
  const auto secondFeeSkip = st::walletTransactionSecondFeeSkip;
  const auto service = IsServiceTransaction(data);
  const auto height = st::walletTransactionSummaryHeight - (service ? st::walletTransactionValue.diamond : 0) +
                      (showTransactionFee ? (st::normalFont->height + feeSkip) : 0) +
                      (showStorageFee ? (st::normalFont->height + (showTransactionFee ? secondFeeSkip : feeSkip)) : 0);
  auto result = object_ptr<Ui::FixedHeightWidget>(parent, height);

  const auto value = isTokenTransaction                //
                         ? tokenTransaction->incoming  //
                               ? tokenTransaction->amount
                               : -tokenTransaction->amount
                         : CalculateValue(data);
  const auto balance =
      service  //
          ? nullptr
          : result->lifetime().make_state<Ui::AmountLabel>(
                result.data(), rpl::single(FormatAmount(value, token, FormatFlag::Signed)), st::walletTransactionValue);

  const auto otherFee =
      showTransactionFee                     //
          ? Ui::CreateChild<Ui::FlatLabel>(  //
                result.data(),
                ph::lng_wallet_view_transaction_fee(ph::now).replace(
                    "{amount}",
                    FormatAmount(isTokenTransaction ? CalculateValue(data) : data.otherFee, Ton::Symbol::ton()).full),
                st::walletTransactionFee)
          : nullptr;

  const auto storageFee =
      showStorageFee  //
          ? Ui::CreateChild<Ui::FlatLabel>(result.data(),
                                           ph::lng_wallet_view_storage_fee(ph::now).replace(
                                               "{amount}", FormatAmount(data.storageFee, Ton::Symbol::ton()).full),
                                           st::walletTransactionFee)
          : nullptr;

  rpl::combine(result->widthValue(), balance ? balance->widthValue() : rpl::single(0),
               otherFee ? otherFee->widthValue() : rpl::single(0),
               storageFee ? storageFee->widthValue() : rpl::single(0)) |
      rpl::start_with_next(
          [=](int width, int bwidth, int, int) {
            auto top = st::walletTransactionValueTop;

            if (balance) {
              balance->move((width - bwidth) / 2, top);
              top += balance->height() + feeSkip;
            }
            if (otherFee) {
              otherFee->move((width - otherFee->width()) / 2, top);
              top += otherFee->height() + secondFeeSkip;
            }
            if (storageFee) {
              storageFee->move((width - storageFee->width()) / 2, top);
            }
          },
          result->lifetime());

  return result;
}

void SetupScrollByDrag(not_null<Ui::BoxContent *> box, not_null<Ui::RpWidget *> child) {
  auto moves = child->events()  //
               | rpl::filter([=](not_null<QEvent *> event) { return (event->type() == QEvent::MouseMove); });

  auto pressed = child->events()  //
                 | rpl::filter([=](not_null<QEvent *> event) {
                     const auto type = event->type();
                     static constexpr auto kLeft = Qt::LeftButton;
                     return ((type == QEvent::MouseButtonPress) || (type == QEvent::MouseButtonRelease)) &&
                            (static_cast<QMouseEvent *>(event.get())->button() == kLeft);
                   })  //
                 | rpl::map([=](not_null<QEvent *> event) { return (event->type() == QEvent::MouseButtonPress); });

  auto pressedY = rpl::combine(std::move(pressed), std::move(moves))                            //
                  | rpl::filter([](bool pressed, not_null<QEvent *> move) { return pressed; })  //
                  | rpl::map([](bool pressed, not_null<QEvent *> move) {
                      const auto pos = static_cast<QMouseEvent *>(move.get())->globalPos();
                      return pressed ? std::make_optional(pos.y()) : std::nullopt;
                    })  //
                  | rpl::distinct_until_changed();

  rpl::combine(std::move(pressedY), box->geometryValue())  //
      | rpl::start_with_next(
            [=](std::optional<int> y, QRect geometry) {
              if (!y) {
                box->onDraggingScrollDelta(0);
                return;
              }
              const auto parent = box->parentWidget();
              const auto global = parent->mapToGlobal(geometry.topLeft());
              const auto top = global.y();
              const auto bottom = top + geometry.height();
              const auto delta = (*y < global.y()) ? (*y - top) : (*y > bottom) ? (*y - bottom) : 0;
              box->onDraggingScrollDelta(delta);
            },
            child->lifetime());
}

}  // namespace

void ViewTransactionBox(not_null<Ui::GenericBox *> box, Ton::Transaction &&data, const Ton::Symbol &selectedToken,
                        rpl::producer<not_null<std::vector<Ton::Transaction> *>> collectEncrypted,
                        rpl::producer<not_null<const std::vector<Ton::Transaction> *>> decrypted,
                        const Fn<void(QImage, QString)> &share, const Fn<void(const QString &)> &viewInExplorer,
                        const Fn<void()> &decryptComment,
                        const Fn<void(const QString &, const Fn<void(QString &&)> &)> &resolveAddress,
                        const Fn<void(const QString &)> &send, const Fn<void(const QString &)> &collect,
                        const Fn<void(const QString &)> &executeSwapBack) {
  struct DecryptedText {
    QString text;
    bool success = false;
  };

  auto tokenTransaction = selectedToken.isToken() ? TryGetTokenTransaction(data, selectedToken) : std::nullopt;
  auto notification = TryGetNotification(data);
  const auto isTokenTransaction = tokenTransaction.has_value();

  auto resolvedAddress = std::make_shared<rpl::event_stream<QString>>();

  auto shouldWaitRecipient = tokenTransaction.has_value() && tokenTransaction->direct;
  auto emptyAddress = tokenTransaction.has_value() && tokenTransaction->recipient.isEmpty();
  auto address = [&]() -> rpl::producer<QString> {
    if (isTokenTransaction) {
      if (shouldWaitRecipient) {
        return resolvedAddress->events() |
               rpl::map([](QString &&address) { return Ton::Wallet::ConvertIntoRaw(address); });
      } else if (emptyAddress) {
        return rpl::single(QString{});
      } else {
        return rpl::single(tokenTransaction->swapback  //
                               ? tokenTransaction->recipient
                               : Ton::Wallet::ConvertIntoRaw(tokenTransaction->recipient));
      }
    } else {
      return rpl::single(Ton::Wallet::ConvertIntoRaw(ExtractAddress(data)));
    }
  }();

  auto currentAddress = box->lifetime().make_state<rpl::variable<QString>>();
  rpl::duplicate(address)  //
      | rpl::start_with_next([=](QString &&address) { *currentAddress = std::forward<QString>(address); },
                             box->lifetime());

  const auto isSwapBack = isTokenTransaction && tokenTransaction->swapback;

  const auto service = IsServiceTransaction(data);

  /*data.initializing  //
    ? ph::lng_wallet_row_init()
    : */
  box->setTitle(service  //
                    ? ph::lng_wallet_row_service()
                    : ph::lng_wallet_view_title());

  const auto id = data.id;
  const auto incoming = data.outgoing.empty() || (isTokenTransaction && tokenTransaction->incoming);
  const auto encryptedComment = IsEncryptedMessage(data);
  const auto decryptedComment = encryptedComment ? QString() : ExtractMessage(data);
  const auto hasComment = encryptedComment || !decryptedComment.isEmpty();
  auto decryptedText = rpl::producer<DecryptedText>();
  auto complexComment = [&] {
    decryptedText = std::move(decrypted)  //
                    | rpl::map([=](not_null<const std::vector<Ton::Transaction> *> list) {
                        const auto i = ranges::find(*list, id, &Ton::Transaction::id);
                        return (i != end(*list)) ? std::make_optional(*i) : std::nullopt;
                      })                                                                                            //
                    | rpl::filter([=](const std::optional<Ton::Transaction> &value) { return value.has_value(); })  //
                    | rpl::map([=](const std::optional<Ton::Transaction> &value) {
                        return IsEncryptedMessage(*value) ? DecryptedText{ph::lng_wallet_decrypt_failed(ph::now), false}
                                                          : DecryptedText{ExtractMessage(*value), true};
                      })            //
                    | rpl::take(1)  //
                    | rpl::start_spawning(box->lifetime());

    return rpl::single(Ui::Text::Link(ph::lng_wallet_click_to_decrypt(ph::now)))                   //
           | rpl::then(rpl::duplicate(decryptedText)                                               //
                       | rpl::map([=](const DecryptedText &decrypted) { return decrypted.text; })  //
                       | Ui::Text::ToWithEntities());
  };

  auto message = IsEncryptedMessage(data)  //
                     ? (complexComment() | rpl::type_erased())
                     : rpl::single(Ui::Text::WithEntities(ExtractMessage(data)));

  box->setStyle(service || emptyAddress ? st::walletNoButtonsBox : st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  box->addRow(CreateSummary(box, data, tokenTransaction));

  if (!service && !emptyAddress) {
    AddBoxSubtitle(box, incoming ? ph::lng_wallet_view_sender() : ph::lng_wallet_view_recipient());
    box->addRow(  //
        object_ptr<Ui::RpWidget>::fromRaw(Ui::CreateAddressLabel(box, std::move(address), st::walletTransactionAddress,
                                                                 [=] { share(QImage(), currentAddress->current()); })),
        {
            st::boxRowPadding.left(),
            st::boxRowPadding.top(),
            st::boxRowPadding.right(),
            st::walletTransactionDateTop,
        });
  }

  const QString transactionHash = data.id.hash.toHex();
  AddBoxSubtitle(box, ph::lng_wallet_view_hash());
  box->addRow(  //
      object_ptr<Ui::RpWidget>::fromRaw(Ui::CreateAddressLabel(
          box, rpl::single(transactionHash), st::walletTransactionAddress, [=] { viewInExplorer(transactionHash); })),
      {
          st::boxRowPadding.left(),
          st::boxRowPadding.top(),
          st::boxRowPadding.right(),
          st::walletTransactionDateTop,
      });

  AddBoxSubtitle(box, ph::lng_wallet_view_date());
  box->addRow(  //
      object_ptr<Ui::FlatLabel>(box, base::unixtime::parse(data.time).toString(Qt::DefaultLocaleLongDate),
                                st::walletLabel),
      {
          st::boxRowPadding.left(),
          st::boxRowPadding.top(),
          st::boxRowPadding.right(),
          st::walletTransactionDateTop,
      });

  if (hasComment) {
    AddBoxSubtitle(box, ph::lng_wallet_view_comment());
    const auto comment = box->addRow(object_ptr<Ui::FlatLabel>(box, std::move(message), st::walletLabel));
    if (IsEncryptedMessage(data)) {
      std::move(decryptedText)                                                           //
          | rpl::map([=](const DecryptedText &decrypted) { return decrypted.success; })  //
          | rpl::start_with_next(
                [=](bool success) {
                  comment->setSelectable(success);
                  if (!success) {
                    comment->setTextColorOverride(st::boxTextFgError->c);
                  }
                },
                comment->lifetime());

      std::move(collectEncrypted)  //
          | rpl::take(1)           //
          | rpl::start_with_next([=](not_null<std::vector<Ton::Transaction> *> list) { list->push_back(data); },
                                 comment->lifetime());

      comment->setClickHandlerFilter([=](const auto &...) {
        decryptComment();
        return false;
      });
    } else {
      comment->setSelectable(true);
    }
    SetupScrollByDrag(box, comment);
  }

  if (!service && !emptyAddress) {
    box->addRow(object_ptr<Ui::FixedHeightWidget>(box, st::walletTransactionBottomSkip));

    auto text = [&] {
      if (notification.has_value()) {
        switch (notification->type) {
          case NotificationType::EthEvent:
            return ph::lng_wallet_view_collect_tokens();
          case NotificationType::TonEvent:
            return ph::lng_wallet_view_execute_swapback();
          default:
            Unexpected("notification type");
        }
      } else if (incoming) {
        return ph::lng_wallet_view_send_to_address();
      } else {
        return ph::lng_wallet_view_send_to_recipient();
      }
    }();

    box->addButton(
           std::move(text)  //
               | rpl::map([selectedToken](QString &&text) { return text.replace("{ticker}", selectedToken.name()); }),
           [=] {
             if (notification.has_value()) {
               switch (notification->type) {
                 case NotificationType::EthEvent:
                   return collect(notification->eventAddress);
                 case NotificationType::TonEvent:
                   return executeSwapBack(notification->eventAddress);
               }
             } else {
               send(currentAddress->current());
             }
           },
           st::walletBottomButton)
        ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
  }

  if (shouldWaitRecipient) {
    resolveAddress(tokenTransaction->recipient, crl::guard(resolvedAddress, [=](QString &&owner) {
                     resolvedAddress->fire(std::forward<QString>(owner));
                   }));
  }
}

}  // namespace Wallet
