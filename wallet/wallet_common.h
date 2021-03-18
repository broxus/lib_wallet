// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/flags.h"

#include "ton/ton_state.h"

namespace Ton {
struct Error;
class Symbol;
}  // namespace Ton

namespace Ui {
class GenericBox;
class FlatLabel;
class InputField;
class VerticalLayout;
}  // namespace Ui

namespace Wallet {

enum class InvoiceField;

inline constexpr auto kMaxCommentLength = 500;
inline constexpr auto kMaxCustodiansLength = 2500;
inline constexpr auto kEncodedAddressLength = 48;
inline constexpr auto kRawAddressLength = 64;
inline constexpr auto kEtheriumAddressLength = 40;

inline constexpr auto kExplorerPath = "https://ton-explorer.com/transactions/";

using int128 = Ton::int128;

struct SelectedToken {
  Ton::Symbol symbol;

  static auto defaultToken() -> SelectedToken {
    return SelectedToken{
        .symbol = Ton::Symbol::ton(),
    };
  }
};

struct FixedAmount {
  QString text;
  int position = 0;
};

struct SelectedDePool {
  QString address;
};

struct SelectedMultisig {
  QString address;
};

using SelectedAsset = std::variant<SelectedToken, SelectedDePool, SelectedMultisig>;

auto operator==(const SelectedAsset &a, const SelectedAsset &b) -> bool;

struct AddNotification {
  Ton::Symbol symbol;
  Ton::Transaction transaction;
};

struct RemoveNotification {
  Ton::Symbol symbol;
  Ton::TransactionId transactionId;
};

struct RefreshNotifications {};

using NotificationsHistoryUpdate = std::variant<AddNotification, RemoveNotification, RefreshNotifications>;

enum CustomAssetType { Token = 0, DePool = 1, Multisig = 2 };

struct CustomAsset {
  CustomAssetType type;
  QString address;
  Ton::Symbol symbol;
};

struct NewAsset {
  CustomAssetType type;
  QString address;
};

struct FormattedAmount {
  Ton::Symbol token;
  QString gramsString;
  QString separator;
  QString nanoString;
  QString full;
};

struct TonTransferInvoice {
  int64 amount{};
  QString address{};
  QString comment{};

  auto asTransaction() const -> Ton::TransactionToSend;
};

struct TokenTransferInvoice {
  Ton::Symbol token;
  int128 amount{};
  int64 realAmount{};
  QString rootContractAddress;
  QString walletContractAddress;
  QString ownerAddress{};
  QString address;
  QString callbackAddress{};
  Ton::TokenTransferType transferType{Ton::TokenTransferType::ToOwner};

  auto asTransaction() const -> Ton::TokenTransactionToSend;
};

struct StakeInvoice {
  int64 stake{};
  int64 realAmount{};
  QString dePool{};

  auto asTransaction() const -> Ton::StakeTransactionToSend;
};

struct WithdrawalInvoice {
  int64 amount{};
  int64 realAmount{};
  bool all = false;
  QString dePool{};

  auto asTransaction() const -> Ton::WithdrawalTransactionToSend;
};

struct CancelWithdrawalInvoice {
  QString dePool{};
  int64 realAmount{};

  auto asTransaction() const -> Ton::CancelWithdrawalTransactionToSend;
};

struct DeployTokenWalletInvoice {
  QString rootContractAddress;
  QString walletContractAddress;
  int64 realAmount{};
  bool owned{};

  auto asTransaction() const -> Ton::DeployTokenWalletTransactionToSend;
};

struct UpgradeTokenWalletInvoice {
  QString rootContractAddress;
  QString walletContractAddress;
  Ton::TokenVersion oldVersion;
  int128 amount{};
  int64 realAmount{};

  auto asTransaction() const -> Ton::UpgradeTokenWalletTransactionToSend;
};

struct CollectTokensInvoice {
  QString eventContractAddress;
  int64 realAmount{};

  auto asTransaction() const -> Ton::CollectTokensTransactionToSend;
};

struct MultisigDeployInvoice {
  Ton::MultisigInitialInfo initialInfo;
  uint8 requiredConfirmations;
  std::vector<QByteArray> owners;

  auto asTransaction() const -> Ton::DeployMultisigTransactionToSend;
};

struct MultisigSubmitTransactionInvoice {
  QByteArray publicKey;
  QString multisigAddress;
  QString address;
  int64 amount;
  bool bounce;
  QString comment;

  auto asTransaction() const -> Ton::SubmitTransactionToSend;
};

struct MultisigConfirmTransactionInvoice {
  QByteArray publicKey;
  QString multisigAddress;
  int64 transactionId;

  auto asTransaction() const -> Ton::ConfirmTransactionToSend;
};

using PreparedInvoice = std::variant<  //
    TonTransferInvoice,
    //
    TokenTransferInvoice,       //
    DeployTokenWalletInvoice,   //
    UpgradeTokenWalletInvoice,  //
    CollectTokensInvoice,       //
    //
    StakeInvoice,             //
    WithdrawalInvoice,        //
    CancelWithdrawalInvoice,  //
    //
    MultisigDeployInvoice,             //
    MultisigSubmitTransactionInvoice,  //
    MultisigConfirmTransactionInvoice>;

enum class Action {
  Refresh,
  Export,
  Send,
  Receive,
  ChangePassword,
  ShowSettings,
  ShowKeystore,
  AddAsset,
  Deploy,
  Upgrade,
  LogOut,
  Back,
};

enum class InfoTransition { Back };

enum class ViewRequestType { Ordinary, DePool };

enum class RecipientWalletType { Main, Multisig };

enum class FormatFlag {
  Signed = 0x01,
  Rounded = 0x02,
  Simple = 0x04,
};
constexpr bool is_flag_type(FormatFlag) {
  return true;
};
using FormatFlags = base::flags<FormatFlag>;

struct ParsedAddressTon {
  QString address;
  bool packed{};
};
struct ParsedAddressEth {
  QString address;
};
using ParsedAddress = std::variant<ParsedAddressTon, ParsedAddressEth>;

[[nodiscard]] FormattedAmount FormatAmount(const int128 &amount, const Ton::Symbol &symbol,
                                           FormatFlags flags = FormatFlags());
[[nodiscard]] QString AmountSeparator();
[[nodiscard]] std::optional<int128> ParseAmountString(const QString &amount, size_t decimals);
[[nodiscard]] ParsedAddress ParseAddress(const QString &address);
[[nodiscard]] PreparedInvoice ParseInvoice(QString invoice);
[[nodiscard]] int64 CalculateValue(const Ton::Transaction &data);
[[nodiscard]] QString ExtractAddress(const Ton::Transaction &data);
[[nodiscard]] bool IsEncryptedMessage(const Ton::Transaction &data);
[[nodiscard]] bool IsServiceTransaction(const Ton::Transaction &data);
[[nodiscard]] QString ExtractMessage(const Ton::Transaction &data);

[[nodiscard]] QString FormatTransactionId(int64 transactionId);

[[nodiscard]] QString TransferLink(const QString &address, const Ton::Symbol &symbol, const int128 &amount = 0,
                                   const QString &comment = QString());

not_null<Ui::FlatLabel *> AddBoxSubtitle(not_null<Ui::VerticalLayout *> box, rpl::producer<QString> text);
not_null<Ui::FlatLabel *> AddBoxSubtitle(not_null<Ui::GenericBox *> box, rpl::producer<QString> text);

[[nodiscard]] not_null<Ui::InputField *> CreateAmountInput(not_null<QWidget *> parent,
                                                           rpl::producer<QString> placeholder, const int128 &amount,
                                                           const Ton::Symbol &symbol);
[[nodiscard]] not_null<Ui::InputField *> CreateCommentInput(not_null<QWidget *> parent,
                                                            rpl::producer<QString> placeholder,
                                                            const QString &value = QString());

[[nodiscard]] bool IsIncorrectPasswordError(const Ton::Error &error);
[[nodiscard]] bool IsIncorrectMnemonicError(const Ton::Error &error);
[[nodiscard]] std::optional<InvoiceField> ErrorInvoiceField(const Ton::Error &error);

template <typename A, typename T, typename... Ts>
constexpr auto is_any_of = std::is_same_v<A, T> || (std::is_same_v<A, Ts> || ...);

}  // namespace Wallet
