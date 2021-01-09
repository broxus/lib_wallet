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
enum class TokenKind;
TokenKind tokenFromString(const QString &token);
} // namespace Ton

namespace Ui {
class GenericBox;
class FlatLabel;
class InputField;
class VerticalLayout;
} // namespace Ui

namespace Wallet {

enum class InvoiceField;

inline constexpr auto kMaxCommentLength = 500;
inline constexpr auto kEncodedAddressLength = 48;
inline constexpr auto kRawAddressLength = 64;
inline constexpr auto kEtheriumAddressLength = 40;

struct SelectedToken {
	Ton::TokenKind token;

	static auto defaultToken() -> SelectedToken {
		return SelectedToken {
			.token = Ton::TokenKind::DefaultToken,
		};
	}
};

struct SelectedDePool {
	QString address;
};

using SelectedAsset = std::variant<SelectedToken, SelectedDePool>;

auto operator==(const SelectedAsset &a, const SelectedAsset &b) -> bool;

struct FormattedAmount {
	Ton::TokenKind token;
	QString gramsString;
	QString separator;
	QString nanoString;
	QString full;
};

struct TonTransferInvoice {
	int64 amount{};
	QString address{};
	QString comment{};
	bool sendUnencryptedText = false;

	auto asTransaction() const -> Ton::TransactionToSend;
};

struct TokenTransferInvoice {
	Ton::TokenKind token{};
	int64 amount{};
	int64 realAmount{};
	QString address{};
	bool swapBack = false;

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

	auto asTransaction() const -> Ton::CancelWithdrawalTransactionToSend;
};

using PreparedInvoice = std::variant<
	TonTransferInvoice,
	TokenTransferInvoice,
	StakeInvoice,
	WithdrawalInvoice,
	CancelWithdrawalInvoice>;

enum class Action {
	Refresh,
	Export,
	Send,
	Receive,
	ChangePassword,
	ShowSettings,
	LogOut,
	Back,
};

enum class InfoTransition {
	Back
};

enum class ViewRequestType {
	Ordinary,
	DePool
};

enum class FormatFlag {
	Signed = 0x01,
	Rounded = 0x02,
	Simple = 0x04,
};
constexpr bool is_flag_type(FormatFlag) { return true; };
using FormatFlags = base::flags<FormatFlag>;

[[nodiscard]] FormattedAmount FormatAmount(
	int64 amount,
	Ton::TokenKind token,
	FormatFlags flags = FormatFlags());
[[nodiscard]] QString AmountSeparator(Ton::TokenKind token = Ton::TokenKind::DefaultToken);
[[nodiscard]] std::optional<int64> ParseAmountString(const QString &amount, size_t decimals);
[[nodiscard]] PreparedInvoice ParseInvoice(QString invoice);
[[nodiscard]] int64 CalculateValue(const Ton::Transaction &data);
[[nodiscard]] QString ExtractAddress(const Ton::Transaction &data);
[[nodiscard]] bool IsEncryptedMessage(const Ton::Transaction &data);
[[nodiscard]] bool IsServiceTransaction(const Ton::Transaction &data);
[[nodiscard]] QString ExtractMessage(const Ton::Transaction &data);

[[nodiscard]] QString TransferLink(
	const QString &address,
    Ton::TokenKind token,
	int64 amount = 0,
	const QString &comment = QString());

not_null<Ui::FlatLabel*> AddBoxSubtitle(
	not_null<Ui::VerticalLayout*> box,
	rpl::producer<QString> text);
not_null<Ui::FlatLabel*> AddBoxSubtitle(
	not_null<Ui::GenericBox*> box,
	rpl::producer<QString> text);

[[nodiscard]] not_null<Ui::InputField*> CreateAmountInput(
	not_null<QWidget*> parent,
	rpl::producer<QString> placeholder,
	int64 amount,
	rpl::producer<Ton::TokenKind> token);
[[nodiscard]] not_null<Ui::InputField*> CreateCommentInput(
	not_null<QWidget*> parent,
	rpl::producer<QString> placeholder,
	const QString &value = QString());

[[nodiscard]] bool IsIncorrectPasswordError(const Ton::Error &error);
[[nodiscard]] bool IsIncorrectMnemonicError(const Ton::Error &error);
[[nodiscard]] std::optional<InvoiceField> ErrorInvoiceField(
	const Ton::Error &error);

} // namespace Wallet
