#include "inline_token_icon.h"

#include "ui/rp_widget.h"
#include "styles/style_wallet.h"

#include <QtGui/QPainter>

namespace Ui
{
namespace
{

const std::vector<std::pair<int, QString>> &Variants(TokenIconKind kind) {
	static const auto iconTon = std::vector<std::pair<int, QString>>{
		{22,  "gem.png"},
		{44,  "gem@2x.png"},
		{88,  "gem@4x.png"},
		{192, "gem@large.png"},
	};

	static const auto iconPepe = std::vector<std::pair<int, QString>>{
		{22,  "pepe.png"},
		{44,  "pepe@2x.png"},
		{88,  "pepe@4x.png"},
		{192, "pepe@large.png"},
	};

	switch (kind) {
		case TokenIconKind::Ton:
			return iconTon;
		case TokenIconKind::Pepe:
			return iconPepe;
		default:
			return iconTon;
	}
}

QString ChooseVariant(TokenIconKind kind, int desiredSize) {
	const auto &variants = Variants(kind);
	for (const auto &[size, name] : variants) {
		if (size == desiredSize || size >= desiredSize * 2) {
			return name;
		}
	}
	return variants.back().second;
}

QImage CreateImage(TokenIconKind kind, int size) {
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

QImage Image(TokenIconKind kind) {
	static const QImage images[] = {
		CreateImage(TokenIconKind::Ton, st::walletTokenIconSize * style::DevicePixelRatio()),
		CreateImage(TokenIconKind::Pepe, st::walletTokenIconSize * style::DevicePixelRatio()),
	};
	return images[static_cast<int>(kind)];
}

void Paint(TokenIconKind kind, QPainter &p, int x, int y) {
	p.drawImage(
		QRect(x, y, st::walletTokenIconSize, st::walletTokenIconSize),
		Image(kind));
}

} // namespace

void Ui::PaintInlineTokenIcon(TokenIconKind kind, QPainter &p, int x, int y, const style::font &font) {
	Paint(kind, p, x, y + font->ascent - st::walletTokenIconSize);
}

QImage InlineTokenIcon(TokenIconKind kind, int size) {
	return CreateImage(kind, size);
}

not_null<RpWidget *> CreateInlinePepe(
	TokenIconKind kind,
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

	result->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(result);
		Paint(kind, p, 0, 0);
	}, result->lifetime());

	return result;
}

} // namespace Ui
