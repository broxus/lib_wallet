// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_common.h"

#include "wallet/wallet_send_grams.h"
#include "ton/ton_state.h"
#include "ton/ton_result.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/input_fields.h"
#include "base/qthelp_url.h"
#include "styles/style_wallet.h"

#include <QtCore/QLocale>

constexpr auto kMaxAmountInt = 9;

namespace Wallet {
namespace {

constexpr auto ipow(int64_t base, size_t power, int64_t result = 1) -> int64_t {
	return power < 1 ? result : ipow(base * base, power >> 1u, (power & 0x1u) ? (result * base) : result);
}

struct FixedAmount {
	QString text;
	int position = 0;
};

std::optional<int64> ParseAmountInt(const QString &trimmed, size_t decimals) {
	const auto one = ipow(10, decimals);
	auto ok = false;
	const auto amount = int64(trimmed.toLongLong(&ok));
	return (ok
		&& (amount <= std::numeric_limits<int64>::max() / one)
		&& (amount >= std::numeric_limits<int64>::min() / one))
		? std::make_optional(amount * one)
		: std::nullopt;
}

std::optional<int64> ParseAmountFraction(QString trimmed, size_t decimals) {
	while (trimmed.size() < decimals) {
		trimmed.append('0');
	}
	auto zeros = 0;
	for (const auto &ch : trimmed) {
		if (ch == '0') {
			++zeros;
		} else {
			break;
		}
	}
	if (zeros == trimmed.size()) {
		return 0;
	} else if (trimmed.size() > decimals) {
		return std::nullopt;
	}
	auto ok = false;
	const auto value = trimmed.mid(zeros).toLongLong(&ok);
	return (ok && value > 0 && value < ipow(10, decimals))
		? std::make_optional(value)
		: std::nullopt;
}

[[nodiscard]] FixedAmount FixAmountInput(
		const QString &was,
		const QString &text,
		int position,
		size_t decimals) {
	const auto separator = FormatAmount(1, Ton::TokenKind::DefaultToken).separator;

	auto result = FixedAmount{ text, position };
	if (text.isEmpty()) {
		return result;
	} else if (text.startsWith('.')
		|| text.startsWith(',')
		|| text.startsWith(separator)) {
		result.text.prepend('0');
		++result.position;
	}
	auto separatorFound = false;
	auto digitsCount = 0;
	for (auto i = 0; i != result.text.size();) {
		const auto ch = result.text[i];
		const auto atSeparator = result.text.midRef(i).startsWith(separator);
		if (ch >= '0' && ch <= '9' &&
				((!separatorFound && digitsCount < kMaxAmountInt) ||
				 (separatorFound && digitsCount < decimals)))
		{
			++i;
			++digitsCount;
			continue;
		} else if (!separatorFound
			&& (atSeparator || ch == '.' || ch == ',')) {
			separatorFound = true;
			if (!atSeparator) {
				result.text.replace(i, 1, separator);
			}
			digitsCount = 0;
			i += separator.size();
			continue;
		}
		result.text.remove(i, 1);
		if (result.position > i) {
			--result.position;
		}
	}
	if (result.text == "0" && result.position > 0) {
		if (was.startsWith('0')) {
			result.text = QString();
			result.position = 0;
		} else {
			result.text += separator;
			result.position += separator.size();
		}
	}
	return result;
}

} // namespace

FormattedAmount FormatAmount(int64 amount, Ton::TokenKind token, FormatFlags flags) {
	const auto decimals = Ton::countDecimals(token);
	const auto one = ipow(10, decimals);

	auto result = FormattedAmount();
	result.token = token;
	const auto amountInt = amount / one;
	const auto amountFraction = std::abs(amount) % one;
	auto roundedFraction = amountFraction;
	if (flags & FormatFlag::Rounded) {
		if (std::abs(amountInt) >= 1'000'000 && (roundedFraction % 1'000'000)) {
			roundedFraction -= (roundedFraction % 1'000'000);
		} else if (std::abs(amountInt) >= 1'000 && (roundedFraction % 1'000)) {
			roundedFraction -= (roundedFraction % 1'000);
		}
	}
	const auto precise = (roundedFraction == amountFraction);
	auto fraction = amountFraction;
	auto zeros = 0u;
	while (zeros < decimals && fraction % 10u == 0) {
		fraction /= 10u;
		++zeros;
	}
	const auto system = QLocale::system();
	const auto locale = (flags & FormatFlag::Simple) ? QLocale::c() : system;
	const auto separator = system.decimalPoint();

	result.gramsString = locale.toString(amountInt);
	if ((flags & FormatFlag::Signed) && amount > 0) {
		result.gramsString = locale.positiveSign() + result.gramsString;
	} else if (amount < 0 && amountInt == 0) {
		result.gramsString = locale.negativeSign() + result.gramsString;
	}
	result.full = result.gramsString;
	if (zeros < decimals) {
		result.separator = separator;
		result.nanoString = QString("%1"
		).arg(fraction, decimals - zeros, 10, QChar('0'));
		if (!precise) {
			const auto fractionLength = (std::abs(amountInt) >= 1'000'000)
				? 3
				: (std::abs(amountInt) >= 1'000)
				? 6
				: decimals;
			result.nanoString = result.nanoString.mid(0, fractionLength);
		}
		result.full += separator + result.nanoString;
	}
	return result;
}

[[nodiscard]] QString AmountSeparator(Ton::TokenKind token) {
	return FormatAmount(1, token).separator;
}

std::optional<int64> ParseAmountString(const QString &amount, size_t decimals) {
	const auto trimmed = amount.trimmed();
	const auto separator = QString(QLocale::system().decimalPoint());
	const auto index1 = trimmed.indexOf('.');
	const auto index2 = trimmed.indexOf(',');
	const auto index3 = (separator == "." || separator == ",")
		? -1
		: trimmed.indexOf(separator);
	const auto found = (index1 >= 0 ? 1 : 0)
		+ (index2 >= 0 ? 1 : 0)
		+ (index3 >= 0 ? 1 : 0);
	if (found > 1) {
		return std::nullopt;
	}
	const auto index = (index1 >= 0)
		? index1
		: (index2 >= 0)
		? index2
		: index3;
	const auto used = (index1 >= 0)
		? "."
		: (index2 >= 0)
		? ","
		: separator;
	const auto amountInt = ParseAmountInt(trimmed.mid(0, index), decimals);
	const auto amountFraction = ParseAmountFraction(trimmed.mid(index + used.size()), decimals);
	if (index < 0 || index == trimmed.size() - used.size()) {
		return amountInt;
	} else if (index == 0) {
		return amountFraction;
	} else if (!amountFraction || !amountInt) {
		return std::nullopt;
	}
	return *amountInt + (*amountInt < 0 ? (-*amountFraction) : (*amountFraction));
}

PreparedInvoice ParseInvoice(QString invoice) {
	enum class InvoiceKind {
		Transfer,
		Stake
	} invoiceKind;

	const auto transferPrefix = qstr("transfer/");
	const auto stakePrefix = qstr("stake/");

	if (const auto transferPrefixPosition = invoice.indexOf(transferPrefix, 0, Qt::CaseInsensitive);
			transferPrefixPosition >= 0) {
		invoice = invoice.mid(transferPrefixPosition + transferPrefix.size());
		invoiceKind = InvoiceKind::Transfer;
	}
	else if (const auto stakePrefixPosition = invoice.indexOf(stakePrefix, 0, Qt::CaseInsensitive);
			stakePrefixPosition >= 0) {
		invoice = invoice.mid(stakePrefixPosition + stakePrefix.size());
		invoiceKind = InvoiceKind::Stake;
	} else {
		invoiceKind = InvoiceKind::Transfer;
	}

	QString address{};
	int64 amount{};
	auto token = Ton::TokenKind::DefaultToken;
	QString comment{};

	const auto paramsPosition = invoice.indexOf('?');
	if (paramsPosition >= 0) {
		const auto params = qthelp::url_parse_params(
			invoice.mid(paramsPosition + 1),
			qthelp::UrlParamNameTransform::ToLower);
		amount = params.value("amount").toULongLong();
        token = Ton::tokenFromString(params.value("token"));
        comment = params.value("text");
	}

	const auto colonPosition = invoice.indexOf(':');
	const auto hexPrefixPosition = invoice.indexOf("0x");
	if (colonPosition > 0) {
		const auto hasMinus = invoice[0] == '-';

		address = (hasMinus ? QString{ "-" } : QString{}) +
			invoice.mid(hasMinus, colonPosition).replace(
				QRegularExpression("[^\\d]"),
				QString()
			).mid(0, 2) + ":" +
			invoice.mid(colonPosition, std::max(paramsPosition - colonPosition, -1)).replace(
				QRegularExpression("[^a-fA-F0-9]"),
				QString()
			).mid(0, kRawAddressLength);
	} else if (hexPrefixPosition == 0) {
		address = QString{"0x"} +
			invoice.mid(2, std::max(paramsPosition - hexPrefixPosition, -1)).replace(
				QRegularExpression("[^a-fA-F0-9]"),
				QString()
			).mid(0, kEtheriumAddressLength);
	} else {
		address = invoice.mid(0, paramsPosition).replace(
			QRegularExpression("[^a-zA-Z0-9_\\-]"),
			QString()
		).mid(0, kEncodedAddressLength);
	}

	switch (invoiceKind) {
		case InvoiceKind::Transfer:
			if (token == Ton::TokenKind::DefaultToken) {
				return TonTransferInvoice {
					.amount = amount,
					.address = address,
					.comment = comment,
				};
			} else {
				return TokenTransferInvoice {
					.token = token,
					.amount = amount,
					.address = address
				};
			}
		case InvoiceKind::Stake:
			return StakeInvoice {
				.stake = amount,
				.dePool = address
			};
		default:
			Unexpected("Unknown invoice kind");
	}
}

int64 CalculateValue(const Ton::Transaction &data) {
	const auto outgoing = ranges::accumulate(
		data.outgoing,
		int64(0),
		ranges::plus(),
		&Ton::Message::value);
	return data.incoming.value - outgoing;
}

QString ExtractAddress(const Ton::Transaction &data) {
	return !data.outgoing.empty()
		? data.outgoing.front().destination
		: !data.incoming.source.isEmpty()
		? data.incoming.source
		: data.incoming.destination;
}

bool IsEncryptedMessage(const Ton::Transaction &data) {
	const auto &message = data.outgoing.empty()
		? data.incoming.message
		: data.outgoing.front().message;
	return !message.data.isEmpty()
		&& message.type == Ton::MessageDataType::EncryptedText;
}

bool IsServiceTransaction(const Ton::Transaction &data) {
	return data.outgoing.empty()
		&& data.incoming.source.isEmpty()
		&& data.incoming.message.text.isEmpty()
		&& data.incoming.message.data.isEmpty()
		&& !data.incoming.value;
}

QString ExtractMessage(const Ton::Transaction &data) {
	const auto &message = data.outgoing.empty()
		? data.incoming.message
		: data.outgoing.front().message;
	if (IsEncryptedMessage(data)) {
		return QString();
	} else if (message.type == Ton::MessageDataType::DecryptedText) {
		return message.text;
	} else if (!message.text.isEmpty()) {
		return message.text;
	}
	return QString();
}

QString TransferLink(
		const QString &address,
		Ton::TokenKind token,
		int64 amount,
		const QString &comment) {
	const auto base = QString{ "https://freeton.broxus.com" };

	auto params = QStringList();
	params.push_back("address=" + address);
    params.push_back("token=" + toString(token));

	if (amount > 0) {
		params.push_back("amount=" + QString::number(amount));
	}
	if (!comment.isEmpty()) {
		params.push_back("text=" + qthelp::url_encode(comment));
	}

	return base + '?' + params.join('&');
}

not_null<Ui::FlatLabel*> AddBoxSubtitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	return container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(text),
			st::walletSubsectionTitle),
		st::walletSubsectionTitlePadding);
}

not_null<Ui::FlatLabel*> AddBoxSubtitle(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> text) {
	return AddBoxSubtitle(box->verticalLayout(), std::move(text));
}

not_null<Ui::InputField*> CreateAmountInput(
		not_null<QWidget*> parent,
		rpl::producer<QString> placeholder,
		int64 amount,
		rpl::producer<Ton::TokenKind> token) {

	const auto result = Ui::CreateChild<Ui::InputField>(
		parent.get(),
		st::walletInput,
		Ui::InputField::Mode::SingleLine,
		placeholder);

	auto tokenState = result->lifetime().make_state<Ton::TokenKind>(Ton::TokenKind::DefaultToken);

	rpl::duplicate(
		token
	) | rpl::start_with_next([=](Ton::TokenKind value) {
		*tokenState = value;
	}, result->lifetime());

	result->setText(amount > 0
		? FormatAmount(amount, *tokenState, FormatFlag::Simple).full
		: QString());

	const auto lastAmountValue = std::make_shared<QString>();
	Ui::Connect(result, &Ui::InputField::changed, [=] {
		Ui::PostponeCall(result, [=] {
			const auto position = result->textCursor().position();
			const auto now = result->getLastText();
			const auto fixed = FixAmountInput(
				*lastAmountValue,
				now,
				position,
				Ton::countDecimals(*tokenState));
			*lastAmountValue = fixed.text;
			if (fixed.text == now) {
				return;
			}
			result->setText(fixed.text);
			result->setFocusFast();
			result->setCursorPosition(fixed.position);
		});
	});
	return result;
}

not_null<Ui::InputField*> CreateCommentInput(
		not_null<QWidget*> parent,
		rpl::producer<QString> placeholder,
		const QString &value) {
	const auto result = Ui::CreateChild<Ui::InputField>(
		parent.get(),
		st::walletInput,
		Ui::InputField::Mode::MultiLine,
		std::move(placeholder),
		value);
	result->setMaxLength(kMaxCommentLength);
	Ui::Connect(result, &Ui::InputField::changed, [=] {
		Ui::PostponeCall(result, [=] {
			const auto text = result->getLastText();
			const auto utf = text.toUtf8();
			if (utf.size() <= kMaxCommentLength) {
				return;
			}
			const auto position = result->textCursor().position();
			const auto update = [&](const QString &text, int position) {
				result->setText(text);
				result->setCursorPosition(position);
			};
			const auto after = text.midRef(position).toUtf8();
			if (after.size() <= kMaxCommentLength) {
				const auto remove = utf.size() - kMaxCommentLength;
				const auto inutf = text.midRef(0, position).toUtf8().size();
				const auto inserted = utf.mid(inutf - remove, remove);
				auto cut = QString::fromUtf8(inserted).size();
				auto updated = text.mid(0, position - cut)
					+ text.midRef(position);
				while (updated.toUtf8().size() > kMaxCommentLength) {
					++cut;
					updated = text.mid(0, position - cut)
						+ text.midRef(position);
				}
				update(updated, position - cut);
			} else {
				update(after.mid(after.size() - kMaxCommentLength), 0);
			}
		});
	});
	return result;
}

bool IsIncorrectPasswordError(const Ton::Error &error) {
	return error.details.startsWith(qstr("KEY_DECRYPT"));
}

bool IsIncorrectMnemonicError(const Ton::Error &error) {
	return error.details.startsWith(qstr("INVALID_MNEMONIC"))
		|| error.details.startsWith(qstr("NEED_MNEMONIC_PASSWORD"));
}

std::optional<Wallet::InvoiceField> ErrorInvoiceField(
		const Ton::Error &error) {
	const auto text = error.details;
	if (text.startsWith(qstr("NOT_ENOUGH_FUNDS"))) {
		return InvoiceField::Amount;
	} else if (text.startsWith(qstr("MESSAGE_TOO_LONG"))) {
		return InvoiceField::Comment;
	} else if (text.startsWith(qstr("INVALID_ACCOUNT_ADDRESS"))) {
		return InvoiceField::Address;
	}
	return std::nullopt;
}

auto operator==(const SelectedAsset &a, const SelectedAsset &b) -> bool {
	if (a.index() != b.index()) {
		return false;
	}

	return v::match(a, [&](const SelectedToken &left) {
		const auto &right = v::get<SelectedToken>(b);
		return left.token == right.token;
	}, [&](const SelectedDePool &left) {
		const auto &right = v::get<SelectedDePool>(b);
		return left.address == right.address;
	});
}

auto TonTransferInvoice::asTransaction() const -> Ton::TransactionToSend {
	return Ton::TransactionToSend {
		.amount = amount,
		.recipient = address,
		.comment = comment,
		.allowSendToUninited = true,
		.sendUnencryptedText = sendUnencryptedText
	};
}

auto TokenTransferInvoice::asTransaction() const -> Ton::TokenTransactionToSend {
	return Ton::TokenTransactionToSend {
		.token = token,
		.realAmount = realAmount,
		.recipient = address,
		.swapBack = swapBack
	};
}

auto StakeInvoice::asTransaction() const -> Ton::StakeTransactionToSend {
	return Ton::StakeTransactionToSend {
		.stake = stake,
		.depoolAddress = dePool,
	};
}

auto WithdrawalInvoice::asTransaction() const -> Ton::WithdrawalTransactionToSend {
	return Ton::WithdrawalTransactionToSend {
		.amount = amount,
		.all = all,
		.depoolAddress = dePool,
	};
}

auto CancelWithdrawalInvoice::asTransaction() const -> Ton::CancelWithdrawalTransactionToSend {
	return Ton::CancelWithdrawalTransactionToSend {
		.depoolAddress = dePool,
	};
}

} // namespace Wallet
