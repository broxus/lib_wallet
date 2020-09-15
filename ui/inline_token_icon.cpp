#include "inline_token_icon.h"

#include "ui/rp_widget.h"
#include "styles/style_wallet.h"

#include <QtGui/QPainter>

namespace Ui
{
namespace
{

const std::vector<std::pair<int, QString>> &Variants(Ton::TokenKind kind) {
	static const auto iconTon = std::vector<std::pair<int, QString>>{
		{22,  "gem.png"},
		{44,  "gem@2x.png"},
		{88,  "gem@4x.png"},
		{192, "gem@large.png"},
	};

	static const auto iconUsdt = std::vector<std::pair<int, QString>>{
		{24,  "usdt.png"},
		{48,  "usdt@2x.png"},
		{92,  "usdt@4x.png"},
		{192, "usdt@large.png"},
	};

	switch (kind) {
		case Ton::TokenKind::Ton:
			return iconTon;
		case Ton::TokenKind::USDT:
			return iconUsdt;
		default:
			return iconTon;
	}
}

QString ChooseVariant(Ton::TokenKind kind, int desiredSize) {
	const auto &variants = Variants(kind);
	for (const auto &[size, name] : variants) {
		if (size == desiredSize || size >= desiredSize * 2) {
			return name;
		}
	}
	return variants.back().second;
}

QImage CreateImage(Ton::TokenKind kind, int size) {
	Expects(size > 0);

	const auto variant = ChooseVariant(kind, size);
	auto result = QImage(":/gui/art/" + variant).scaled(
		size,
		size,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	result.setDevicePixelRatio(1.);

	Ensures(!result.isNull());
	return result;
}

QImage Image(Ton::TokenKind kind) {
	static const QImage images[] = {
		CreateImage(Ton::TokenKind::Ton, st::walletTokenIconSize * style::DevicePixelRatio()),
		CreateImage(Ton::TokenKind::USDT, st::walletTokenIconSize * style::DevicePixelRatio()),
	};
	return images[static_cast<int>(kind)];
}

void Paint(Ton::TokenKind kind, QPainter &p, int x, int y) {
	p.drawImage(
		QRect(x, y, st::walletTokenIconSize, st::walletTokenIconSize),
		Image(kind));
}

} // namespace

void Ui::PaintInlineTokenIcon(Ton::TokenKind kind, QPainter &p, int x, int y, const style::font &font) {
	Paint(kind, p, x, y + font->ascent - st::walletTokenIconSize);
}

QImage InlineTokenIcon(Ton::TokenKind kind, int size) {
	return CreateImage(kind, size);
}

not_null<RpWidget *> CreateInlineTokenIcon(
	rpl::producer<std::optional<Ton::TokenKind>> kind,
	not_null<QWidget *> parent,
	int x,
	int y,
	const style::font &font) {
	auto result = Ui::CreateChild<RpWidget>(parent.get());

	result->setGeometry(
		x,
		y + font->ascent - st::walletTokenIconAscent,
		st::walletDiamondSize,
		st::walletDiamondSize);

	rpl::combine(
		result->paintRequest(),
		std::move(kind)
	) | rpl::start_with_next([=](const QRect&, std::optional<Ton::TokenKind> kind) {
		auto p = QPainter(result);
		Paint(kind.value_or(Ton::TokenKind::Ton), p, 0, 0);
	}, result->lifetime());

	return result;
}

} // namespace Ui
