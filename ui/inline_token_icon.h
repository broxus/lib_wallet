#pragma once

#include "ui/style/style_core.h"
#include "ton/ton_state.h"

class QPainter;

namespace Ui {

class RpWidget;

void PaintInlineTokenIcon(const Ton::Symbol &symbol, QPainter &p, int x, int y, const style::font &font);

[[nodiscard]] QImage InlineTokenIcon(const Ton::Symbol &symbol, int size);

not_null<RpWidget *> CreateInlineTokenIcon(rpl::producer<Ton::Symbol> kind, not_null<QWidget *> parent, int x, int y,
                                           const style::font &font);

[[nodiscard]] QImage TokenQr(const Ton::Symbol &token, const QString &text, int pixel, int max = 0);
[[nodiscard]] QImage TokenQrForShare(const Ton::Symbol &token, const QString &text);

}  // namespace Ui
