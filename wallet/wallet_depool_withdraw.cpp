#include "wallet_depool_withdraw.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/inline_token_icon.h"
#include "base/qt_signal_producer.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

namespace Wallet {

enum WithdrawalType {
  Part = 0,
  All = 1,
};

void DePoolWithdrawBox(not_null<Ui::GenericBox *> box, const WithdrawalInvoice &invoice,
                       rpl::producer<Ton::WalletState> state,
                       const Fn<void(WithdrawalInvoice, Fn<void(DePoolWithdrawField)> error)> &done) {
  const auto defaultToken = Ton::Symbol::ton();

  const auto prepared = box->lifetime().make_state<WithdrawalInvoice>(invoice);
  const auto totalStake = box->lifetime().make_state<rpl::variable<int64>>(0);

  rpl::duplicate(state)  //
      | rpl::start_with_next(
            [=](const Ton::WalletState &state) {
              const auto it = state.dePoolParticipantStates.find(invoice.dePool);
              if (it != state.dePoolParticipantStates.end()) {
                *totalStake = it->second.total;
              }
            },
            box->lifetime());

  box->setTitle(ph::lng_wallet_withdraw_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  const auto withdrawalType =
      box->lifetime().make_state<rpl::variable<WithdrawalType>>(static_cast<WithdrawalType>(invoice.all));
  const auto withdrawalKindSelector = std::make_shared<Ui::RadiobuttonGroup>(invoice.all);
  const auto radioButtonMargin = QMargins(st::walletSendAmountPadding.left(), 0, 0, 0);
  const auto radioButtonItemHeight =
      st::defaultCheckbox.margin.top() + st::defaultRadio.diameter + st::defaultCheckbox.margin.bottom();

  const auto withdrawAll = box->addRow(  //
      object_ptr<Ui::FixedHeightWidget>(box, radioButtonItemHeight), radioButtonMargin);
  Ui::CreateChild<Ui::Radiobutton>(withdrawAll, withdrawalKindSelector, WithdrawalType::All,
                                   ph::lng_wallet_withdraw_all(ph::now));

  const auto withdrawPart = box->addRow(  //
      object_ptr<Ui::FixedHeightWidget>(box, radioButtonItemHeight), radioButtonMargin);
  Ui::CreateChild<Ui::Radiobutton>(withdrawPart, withdrawalKindSelector, WithdrawalType::Part,
                                   ph::lng_wallet_withdraw_part(ph::now));

  const auto amountWrapper = box->addRow(object_ptr<Ui::VerticalLayout>(box), QMargins{});

  const auto subtitle = AddBoxSubtitle(amountWrapper, ph::lng_wallet_withdraw_amount());

  auto balanceText =
      rpl::combine(ph::lng_wallet_withdraw_locked(), totalStake->value()) |
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

  const auto amount = amountWrapper->add(  //
      object_ptr<Ui::InputField>::fromRaw(
          CreateAmountInput(box, rpl::single("0" + AmountSeparator() + "0"), 0, defaultToken)),
      st::walletSendAmountPadding);

  withdrawalKindSelector->setChangedCallback([=](int all) {
    amount->setEnabled(!all);
    amountWrapper->setMaximumHeight(all ? 0 : QWIDGETSIZE_MAX);
    amountWrapper->adjustSize();

    *withdrawalType = static_cast<WithdrawalType>(all);
  });

  const auto showError = crl::guard(box, [=](DePoolWithdrawField field) {
    switch (field) {
      case DePoolWithdrawField::Amount:
        amount->showError();
        return;
    }
    Unexpected("Field value in DePoolWithdrawBox error callback.");
  });

  const auto submit = [=] {
    auto collected = WithdrawalInvoice{};
    if (withdrawalType->current() == WithdrawalType::All) {
      collected.all = true;
    } else {
      const auto parsed = ParseAmountString(amount->getLastText(), defaultToken.decimals());
      if (!parsed) {
        amount->showError();
        return;
      }
      collected.amount = *parsed;
    }
    collected.dePool = prepared->dePool;

    done(collected, showError);
  };

  auto text =
      rpl::combine(rpl::single(rpl::empty_value()), rpl::single(withdrawalType->current())) |
      rpl::then(rpl::combine(
          rpl::single(rpl::empty_value()) | rpl::then(base::qt_signal_producer(amount, &Ui::InputField::changed)),
          withdrawalType->value())) |
      rpl::map([=](rpl::empty_value, const WithdrawalType &type) -> rpl::producer<QString> {
        const auto text = amount->getLastText();
        const auto value = ParseAmountString(text, defaultToken.decimals()).value_or(0);
        if (type == WithdrawalType::All) {
          return ph::lng_wallet_withdraw_button_all();
        } else if (value > 0) {
          return rpl::combine(ph::lng_wallet_withdraw_button_amount(),
                              ph::lng_wallet_grams_count(FormatAmount(value, defaultToken).full, defaultToken)()) |
                 rpl::map([=](QString &&text, const QString &amount) { return text.replace("{amount}", amount); });
        } else {
          return ph::lng_wallet_withdraw_button_part();
        }
      }) |
      rpl::flatten_latest();

  box->addButton(std::move(text), submit, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);

  const auto checkFunds = [=](const QString &amount) {
    if (const auto value = ParseAmountString(amount, defaultToken.decimals())) {
      const auto insufficient = (*value > std::max(totalStake->current(), 0ll));
      balanceLabel->setTextColorOverride(insufficient ? std::make_optional(st::boxTextFgError->c) : std::nullopt);
    }
  };

  totalStake->value() |
      rpl::start_with_next([=](int64 value) { checkFunds(amount->getLastText()); }, amount->lifetime());

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
