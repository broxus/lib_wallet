#include "wallet_send_stake.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/buttons.h"
#include "ui/inline_token_icon.h"
#include "base/qt_signal_producer.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

namespace Wallet {

void SendStakeBox(not_null<Ui::GenericBox *> box, const StakeInvoice &invoice, rpl::producer<Ton::WalletState> state,
                  const Fn<void(StakeInvoice, Fn<void(StakeInvoiceField)> error)> &done) {
  const auto defaultToken = Ton::Symbol::ton();

  const auto prepared = box->lifetime().make_state<StakeInvoice>(invoice);

  auto availableBalance = rpl::duplicate(state) | rpl::map([=](const Ton::WalletState &state) {
                            return state.account.fullBalance - state.account.lockedBalance;
                          });

  const auto funds = std::make_shared<int64>();

  box->setTitle(ph::lng_wallet_send_stake_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  const auto subtitle = AddBoxSubtitle(box, ph::lng_wallet_send_stake_amount());

  const auto amount = box->addRow(  //
      object_ptr<Ui::InputField>::fromRaw(
          CreateAmountInput(box, rpl::single("0" + AmountSeparator() + "0"), 0, defaultToken)),
      st::walletSendAmountPadding);

  box->addRow(object_ptr<Ui::FlatLabel>(box, ph::lng_wallet_send_stake_warning(), st::walletSendAbout),
              st::walletSendStakeWarningPadding);

  auto balanceText =
      rpl::combine(ph::lng_wallet_send_stake_balance(), rpl::duplicate(availableBalance)) |
      rpl::map([=](QString &&phrase, int64 value) {
        return phrase.replace("{amount}", FormatAmount(std::max(value, 0LL), defaultToken, FormatFlag::Rounded).full);
      });

  const auto diamondLabel =
      Ui::CreateInlineTokenIcon(defaultToken, subtitle->parentWidget(), 0, 0, st::walletSendBalanceLabel.style.font);
  const auto balanceLabel =
      Ui::CreateChild<Ui::FlatLabel>(subtitle->parentWidget(), std::move(balanceText), st::walletSendBalanceLabel);
  rpl::combine(subtitle->geometryValue(), balanceLabel->widthValue()) |
      rpl::start_with_next(
          [=](QRect rect, int innerWidth) {
            const auto diamondTop = rect.top() + st::walletSubsectionTitle.style.font->ascent - st::walletDiamondAscent;
            const auto diamondRight = st::boxRowPadding.right();
            diamondLabel->moveToRight(diamondRight, diamondTop);
            const auto labelTop = rect.top() + st::walletSubsectionTitle.style.font->ascent -
                                  st::walletSendBalanceLabel.style.font->ascent;
            const auto labelRight =
                diamondRight + st::walletDiamondSize + st::walletSendBalanceLabel.style.font->spacew;
            balanceLabel->moveToRight(labelRight, labelTop);
          },
          balanceLabel->lifetime());

  const auto showError = crl::guard(box, [=](StakeInvoiceField field) {
    switch (field) {
      case StakeInvoiceField::Amount:
        amount->showError();
        return;
    }
    Unexpected("Field value in SendStakeBox error callback.");
  });

  const auto submit = [=] {
    auto collected = StakeInvoice();
    const auto parsed = ParseAmountString(amount->getLastText(), defaultToken.decimals());
    if (!parsed) {
      amount->showError();
      return;
    }
    collected.stake = *parsed;
    collected.dePool = prepared->dePool;
    done(collected, showError);
  };

  auto text =
      rpl::single(rpl::empty_value()) | rpl::then(base::qt_signal_producer(amount, &Ui::InputField::changed)) |
      rpl::map([=]() -> rpl::producer<QString> {
        const auto text = amount->getLastText();
        const auto value = ParseAmountString(text, defaultToken.decimals()).value_or(0);
        if (value > 0) {
          return rpl::combine(ph::lng_wallet_send_stake_button_amount(),
                              ph::lng_wallet_grams_count(FormatAmount(value, defaultToken).full, defaultToken)())  //
                 | rpl::map([=](QString &&text, const QString &amount) { return text.replace("{amount}", amount); });
        } else {
          return ph::lng_wallet_send_stake_button();
        }
      }) |
      rpl::flatten_latest();

  box->addButton(std::move(text), submit, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);

  const auto checkFunds = [=](const QString &amount) {
    if (const auto value = ParseAmountString(amount, defaultToken.decimals())) {
      const auto insufficient = (*value > std::max(*funds, 0ll));
      balanceLabel->setTextColorOverride(insufficient ? std::make_optional(st::boxTextFgError->c) : std::nullopt);
    }
  };

  std::move(availableBalance)  //
      | rpl::start_with_next(
            [=](int64 value) {
              *funds = value;
              checkFunds(amount->getLastText());
            },
            amount->lifetime());

  Ui::Connect(amount, &Ui::InputField::changed,
              [=] { Ui::PostponeCall(amount, [=] { checkFunds(amount->getLastText()); }); });

  box->setFocusCallback([=] { amount->setFocusFast(); });

  Ui::Connect(amount, &Ui::InputField::submitted, [=] {
    if (ParseAmountString(amount->getLastText(), defaultToken.decimals())) {
      amount->showError();
    } else {
      submit();
    }
  });
}

}  // namespace Wallet
