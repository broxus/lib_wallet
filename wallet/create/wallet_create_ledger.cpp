// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/create/wallet_create_ledger.h"

#include "wallet/wallet_phrases.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "ui/rp_widget.h"
#include "ui/lottie_widget.h"
#include "styles/style_wallet.h"
#include "ton/ton_state.h"

namespace Wallet::Create {

Ledger::Ledger(const std::vector<Ton::TonLedgerKey> &ledgerKeys) : Step(Type::Default) {
  setTitle(ph::lng_wallet_import_ledger_title(Ui::Text::RichLangValue), st::walletImportTitleTop);
  setDescription(ph::lng_wallet_import_ledger_description(Ui::Text::RichLangValue));
  initControls(ledgerKeys);
}

int Ledger::desiredHeight() const {
  return _desiredHeight;
}

std::vector<Ton::TonLedgerKey> Ledger::passLedgerKeys() const {
  return _passLedgerKeys();
}

void Ledger::initControls(const std::vector<Ton::TonLedgerKey> &ledgerKeys) {
  constexpr auto rows = 5;
  const auto wordsTop = st::walletImportWordsTop;
  const auto rowsBottom = wordsTop + rows * st::walletWordHeight;

  auto decodeLedgerKey = [](const QByteArray &ledgerKey) {
    auto decoded = QByteArray::fromBase64(ledgerKey, QByteArray::Base64Option::Base64UrlEncoding);
    return QString(decoded.mid(2, 32).toHex());
  };

  auto checkBoxes = std::make_shared<std::vector<std::pair<Ui::Checkbox*, Ton::TonLedgerKey>>>();
  for (auto const &ledgerKey : ledgerKeys) {
    auto decoded = decodeLedgerKey(ledgerKey.publicKey);
    auto text = QString::number(ledgerKey.account + 1) + "  0x" +  decoded.left(4) + "..." + decoded.right(6);
    auto *checkBox = Ui::CreateChild<Ui::Checkbox>(inner().get(), text, ledgerKey.created);
    checkBox->setDisabled(ledgerKey.created);
    checkBoxes->emplace_back(std::make_pair(checkBox, ledgerKey));
  }

  inner()->sizeValue() |
  rpl::start_with_next(
      [=](QSize size) {
        const auto x = size.width() / 2 - st::walletImportSkipLeft;
        auto y = contentTop() + wordsTop;
        for (const auto &checkBox : *checkBoxes) {
          checkBox.first->move(x, y);
          y += st::walletWordHeight;
        }
      },
      inner()->lifetime());

  _desiredHeight = rowsBottom + st::walletWordsNextSkip + st::walletWordsNextBottomSkip;

  _passLedgerKeys = [=]() {
    std::vector<Ton::TonLedgerKey> ledgerKeys;
    for (const auto &checkBox : *checkBoxes) {
      if (checkBox.first->checked() && !checkBox.first->isDisabled()) {
        ledgerKeys.push_back(checkBox.second);
      }
    }
    return ledgerKeys;
  };

}

void Ledger::showFinishedHook() {
  startLottie();
}

}  // namespace Wallet::Create
