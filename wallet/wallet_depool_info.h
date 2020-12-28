#pragma once

#include "ui/rp_widget.h"
#include "ton/ton_state.h"

namespace Wallet {

struct DePoolInfoState {
	QString address;
	Ton::DePoolParticipantState participantState;
};

class DePoolInfo final {
public:
	DePoolInfo(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<DePoolInfoState> state);

	void setGeometry(QRect geometry);
	auto geometry() const -> const QRect&;

	void setVisible(bool visible);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void setupControls(rpl::producer<DePoolInfoState> &&state);

	Ui::RpWidget _widget;
};

[[nodiscard]] rpl::producer<DePoolInfoState> MakeDePoolInfoState(
	rpl::producer<Ton::WalletViewerState> state,
	rpl::producer<QString> selectedDePool);

} // namespace Wallet
