#include "wallet_view_depool_transaction.h"

#include "wallet/wallet_common.h"
#include "wallet/wallet_phrases.h"
#include "ui/amount_label.h"
#include "ui/address_label.h"
#include "ui/lottie_widget.h"
#include "ui/widgets/buttons.h"
#include "base/unixtime.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/palette.h"
#include "ton/ton_wallet.h"

#include <QtCore/QDateTime>
#include <QtGui/QtEvents>

namespace Wallet {

namespace {

template <typename T>
object_ptr<Ui::RpWidget> CreateSummary(not_null<Ui::RpWidget *> parent, const Ton::Transaction &data,
                                       const T &dePoolTransaction) {
  const auto isOnRoundComplete = std::is_same_v<T, Ton::DePoolOnRoundCompleteTransaction>;
  const auto isStakeTransaction = std::is_same_v<T, Ton::DePoolOrdinaryStakeTransaction>;
  static_assert(isOnRoundComplete || isStakeTransaction);

  const auto defaultToken = Ton::Symbol::ton();

  const auto feeSkip = st::walletTransactionFeeSkip;
  const auto height = st::walletTransactionSummaryHeight + st::normalFont->height + feeSkip;
  auto result = object_ptr<Ui::FixedHeightWidget>(parent, height);

  int64 value{};
  int64 fee{};
  if constexpr (isStakeTransaction) {
    value = dePoolTransaction.stake;
    fee = -CalculateValue(data) - value + data.otherFee;
  } else if constexpr (isOnRoundComplete) {
    value = dePoolTransaction.reward;
    fee = data.otherFee;
  }

  const auto balance = result->lifetime().make_state<Ui::AmountLabel>(
      result.data(), rpl::single(FormatAmount(value, defaultToken)), st::walletTransactionValue);

  const auto otherFee = Ui::CreateChild<Ui::FlatLabel>(
      result.data(),
      ph::lng_wallet_view_transaction_fee(ph::now).replace("{amount}", FormatAmount(fee, defaultToken).full),
      st::walletTransactionFee);

  rpl::combine(result->widthValue(), balance->widthValue(), otherFee->widthValue())  //
      | rpl::start_with_next(
            [=](int width, int bwidth, int) {
              auto top = st::walletTransactionValueTop;

              if (balance) {
                balance->move((width - bwidth) / 2, top);
                top += balance->height() + feeSkip;
              }
              if (otherFee) {
                otherFee->move((width - otherFee->width()) / 2, top);
              }
            },
            result->lifetime());

  return result;
}

}  // namespace

void ViewDePoolTransactionBox(not_null<Ui::GenericBox *> box, const Ton::Transaction &data,
                              const Fn<void(QImage, QString)> &share) {
  box->setStyle(st::walletNoButtonsBox);
  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  const auto id = data.id;
  const auto address = Ton::Wallet::ConvertIntoRaw(ExtractAddress(data));

  v::match(
      data.additional,
      [&](const Ton::DePoolOnRoundCompleteTransaction &onRoundCompleteTransaction) {
        box->setTitle(ph::lng_wallet_view_round_complete());
        box->addRow(CreateSummary(box, data, onRoundCompleteTransaction));
      },
      [&](const Ton::DePoolOrdinaryStakeTransaction &ordinaryStakeTransaction) {
        box->setTitle(ph::lng_wallet_view_ordinary_stake());
        box->addRow(CreateSummary(box, data, ordinaryStakeTransaction));
      },
      [&](auto &&) { box->setTitle(ph::lng_wallet_view_title()); });

  AddBoxSubtitle(box, ph::lng_wallet_view_depool());

  box->addRow(  //
      object_ptr<Ui::RpWidget>::fromRaw(Ui::CreateAddressLabel(box, rpl::single(address), st::walletTransactionAddress,
                                                               [=] { share(QImage(), address); })),
      {
          st::boxRowPadding.left(),
          st::boxRowPadding.top(),
          st::boxRowPadding.right(),
          st::walletTransactionDateTop,
      });

  AddBoxSubtitle(box, ph::lng_wallet_view_date());
  box->addRow(object_ptr<Ui::FlatLabel>(box, base::unixtime::parse(data.time).toString(Qt::DefaultLocaleLongDate),
                                        st::walletLabel));
}

}  // namespace Wallet
