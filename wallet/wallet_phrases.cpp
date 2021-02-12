// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_phrases.h"

#include "ton/ton_state.h"

#include <QtCore/QDate>
#include <QtCore/QTime>

namespace ph {

const auto walletCountStart = start_phrase_count();

phrase lng_wallet_cancel = "Cancel";
phrase lng_wallet_continue = "Continue";
phrase lng_wallet_done = "Done";
phrase lng_wallet_save = "Save";
phrase lng_wallet_warning = "Warning";
phrase lng_wallet_error = "Error";
phrase lng_wallet_ok = "OK";

phrase lng_wallet_copy_address = "Copy Wallet Address";

phrase lng_wallet_intro_title = "TON Crystal Wallet";
phrase lng_wallet_intro_description =
    "TON Crystal wallet allows you to make fast and\nsecure blockchain-based payments\nwithout intermediaries.";
phrase lng_wallet_intro_create = "Create My Wallet";
phrase lng_wallet_intro_import = "Import existing wallet";

phrase lng_wallet_import_title = "24 Secret Words";
phrase lng_wallet_import_description =
    "Please restore access to your wallet by\nentering the 24 secret words you wrote\ndown when creating the wallet.";
phrase lng_wallet_import_dont_have = "I don't have them";
phrase lng_wallet_import_incorrect_title = "Incorrect words";
phrase lng_wallet_import_incorrect_text =
    "Sorry, you have entered incorrect secret words. Please double check and try again.";
phrase lng_wallet_import_incorrect_retry = "Try again";

phrase lng_wallet_too_bad_title = "Too Bad";
phrase lng_wallet_too_bad_description = "Without the secret words, you can't\nrestore access to your wallet.";
phrase lng_wallet_too_bad_enter_words = "Enter words";

phrase lng_wallet_created_title = "Congratulations";
phrase lng_wallet_created_description =
    "Your TON Crystal wallet has just been created.\nOnly you control it.\n\nTo be able to always have access to "
    "it,\nplease set up a secure password and write\ndown secret words.";

phrase lng_wallet_words_title = "24 secret words";
phrase lng_wallet_words_description =
    "Write down these 24 words in the correct\norder and store them in a secret place.\n\nUse these secret words to "
    "restore access to\nyour wallet if you lose your password or\naccess to this device.";
phrase lng_wallet_words_sure_title = "Sure done?";
phrase lng_wallet_words_sure_text = "You didn't have enough time to write these words down.";
phrase lng_wallet_words_sure_ok = "OK, Sorry";

phrase lng_wallet_check_title = "Test Time!";
phrase lng_wallet_check_description =
    "Now let's check that you wrote your secret\nwords correctly.\n\nPlease enter the words {index1}, {index2} and "
    "{index3} below:";
phrase lng_wallet_check_incorrect_title = "Incorrect words";
phrase lng_wallet_check_incorrect_text = "The secret words you have entered do not match the ones in the list.";
phrase lng_wallet_check_incorrect_view = "See words";
phrase lng_wallet_check_incorrect_retry = "Try again";

phrase lng_wallet_set_passcode_title = "Secure Password";
phrase lng_wallet_set_passcode_description = "Please choose a secure password\nfor confirming your payments.";
phrase lng_wallet_set_passcode_enter = "Enter your password";
phrase lng_wallet_set_passcode_repeat = "Repeat your password";

phrase lng_wallet_change_passcode_title = "Change Password";
phrase lng_wallet_change_passcode_old = "Enter your old password";
phrase lng_wallet_change_passcode_new = "Enter a new password";
phrase lng_wallet_change_passcode_repeat = "Repeat the new password";
phrase lng_wallet_change_passcode_done = "Password changed successfully.";

phrase lng_wallet_ready_title = "Ready to go!";
phrase lng_wallet_ready_description =
    "You're all set. Now you have a wallet that\nonly you control \xe2\x80\x93 directly, without\nmiddlemen or "
    "bankers.";
phrase lng_wallet_ready_show_account = "View My Wallet";

phrase lng_wallet_sync = "syncing...";
phrase lng_wallet_sync_percent = "syncing... {percent}%";
phrase lng_wallet_refreshing = "updating...";
phrase lng_wallet_refreshed_just_now = "updated just now";

phrase lng_wallet_cover_balance = "Your balance";
phrase lng_wallet_cover_balance_test = "Your test balance";
phrase lng_wallet_cover_locked = "Locked";
phrase lng_wallet_cover_reward = "Withdrawing / Reward";
phrase lng_wallet_cover_receive_full = "Receive {ticker}";
phrase lng_wallet_cover_receive = "Receive";
phrase lng_wallet_cover_cancel_withdrawal = "Cancel";
phrase lng_wallet_cover_send = "Send";
phrase lng_wallet_cover_stake = "Stake";
phrase lng_wallet_cover_deploy = "Deploy";
phrase lng_wallet_cover_withdraw = "Withdraw";

phrase lng_wallet_update = "Update Wallet";
phrase lng_wallet_update_short = "Update";

phrase lng_wallet_tokens_list_accounts = "Accounts";
phrase lng_wallet_tokens_list_add = "Add";
phrase lng_wallet_tokens_list_delete_item = "Delete";
phrase lng_wallet_tokens_list_swap = "Swap Tokens";

phrase lng_wallet_empty_history_title = "Wallet Created";
phrase lng_wallet_empty_history_welcome = "Welcome";
phrase lng_wallet_empty_history_address = "Your wallet address";
phrase lng_wallet_empty_history_depool_address = "DePool address";
phrase lng_wallet_empty_history_token_address = "TIP3 Token wallet";

phrase lng_wallet_depool_info_title = "DePool Added";
phrase lng_wallet_depool_info_stakes_title = "Stakes";
phrase lng_wallet_depool_info_vestings_title = "Vestings";
phrase lng_wallet_depool_info_locks_title = "Locks";
phrase lng_wallet_depool_info_empty = "None";
phrase lng_wallet_depool_info_id = "#{amount}:";
phrase lng_wallet_depool_info_remaining_amount = "Remaining amount: {amount}";
phrase lng_wallet_depool_info_last_withdrawal_time = "Last withdrawal time: {amount}";
phrase lng_wallet_depool_info_withdrawal_period = "Withdrawal period: {amount}";
phrase lng_wallet_depool_info_withdrawal_value = "Withdrawal value: {amount}";
phrase lng_wallet_depool_info_owner = "Owner:";

phrase lng_wallet_row_from = "from:";
phrase lng_wallet_row_reward_from = "reward from:";
phrase lng_wallet_row_swap_back_to = "swap back to:";
phrase lng_wallet_row_ordinary_stake_to = "ordinary stake to:";
phrase lng_wallet_row_to = "to:";
phrase lng_wallet_row_init = "Wallet initialization";
phrase lng_wallet_row_service = "Empty transaction";
phrase lng_wallet_row_fees = "blockchain fees: {amount}";
phrase lng_wallet_row_pending_date = "Pending";
phrase lng_wallet_click_to_decrypt = "Enter password to view comment";
phrase lng_wallet_decrypt_failed = "Decryption failed :(";

phrase lng_wallet_view_title = "Transaction";
phrase lng_wallet_view_ordinary_stake = "Ordinary Stake";
phrase lng_wallet_view_round_complete = "Round Reward";
phrase lng_wallet_view_transaction_fee = "{amount} transaction fee";
phrase lng_wallet_view_storage_fee = "{amount} storage fee";
phrase lng_wallet_view_sender = "Sender";
phrase lng_wallet_view_recipient = "Recipient";
phrase lng_wallet_view_depool = "DePool";
phrase lng_wallet_view_date = "Date";
phrase lng_wallet_view_comment = "Comment";
phrase lng_wallet_view_send_to_address = "Send {ticker} to this address";
phrase lng_wallet_view_send_to_recipient = "Send {ticker} to this Recipient";
phrase lng_wallet_view_reveal = "Reveal";

phrase lng_wallet_receive_title = "Receive {ticker}";
phrase lng_wallet_receive_description =
    "Share this address with other TON Crystal wallet owners to receive TON from them.";
phrase lng_wallet_receive_description_test =
    "Share this address to receive Ruby. Note: this link won't work for real TON.";
phrase lng_wallet_receive_show_as_packed = "Show as packed";
phrase lng_wallet_receive_create_invoice = "Create Invoice";
phrase lng_wallet_receive_share = "Share Wallet Address";
phrase lng_wallet_receive_swap = "Swap {ticker}";
phrase lng_wallet_receive_deploy = "Deploy wallet";
phrase lng_wallet_receive_copied = "Transfer link copied to clipboard.";
phrase lng_wallet_receive_address_copied = "Wallet address copied to clipboard.";
phrase lng_wallet_receive_copied_qr = "QR Code copied to clipboard.";

phrase lng_wallet_invoice_title = "Create Invoice";
phrase lng_wallet_invoice_amount = "Amount";
phrase lng_wallet_invoice_number = "Amount in {ticker} you expect to receive";
phrase lng_wallet_invoice_comment = "Comment (optional)";
phrase lng_wallet_invoice_comment_about =
    "You can specify the amount and purpose of the payment to save the sender some time.";
phrase lng_wallet_invoice_url = "Invoice URL";
phrase lng_wallet_invoice_copy_url = "Copy Invoice URL";
phrase lng_wallet_invoice_url_about =
    "Share this address with other TON Crystal wallet owners to receive {ticker} from them.";
phrase lng_wallet_invoice_url_about_test =
    "Share this address to receive Ruby. Note: this link won't work for real TON.";
phrase lng_wallet_invoice_generate_qr = "Generate QR Code";
phrase lng_wallet_invoice_share = "Share Invoice URL";
phrase lng_wallet_invoice_qr_title = "Invoice QR";
phrase lng_wallet_invoice_qr_amount = "Expected amount";
phrase lng_wallet_invoice_qr_comment = "Comment";
phrase lng_wallet_invoice_qr_share = "Share QR Code";
phrase lng_wallet_invoice_copied = "Invoice link copied to clipboard.";

phrase lng_wallet_menu_settings = "Settings";
phrase lng_wallet_menu_change_passcode = "Change password";
phrase lng_wallet_menu_export = "Back up wallet";
phrase lng_wallet_menu_delete = "Log Out";

phrase lng_wallet_delete_title = "Log Out";
phrase lng_wallet_delete_about =
    "This will disconnect the wallet from this app. You will be able to restore your wallet using **24 secret words** "
    "\xe2\x80\x93 or import another wallet.\n\nWallets are located in the decentralized TON Blockchain. If you want "
    "the wallet to be deleted simply transfer all the TON Crystals from it and leave it empty.";
phrase lng_wallet_delete_disconnect = "Disconnect";

phrase lng_wallet_send_title = "Send {ticker}";
phrase lng_wallet_send_token_transfer_direct = "By token wallet address";
phrase lng_wallet_send_token_transfer_to_owner = "By owner address";
phrase lng_wallet_send_token_transfer_swapback = "Swap back";
phrase lng_wallet_send_token_proxy_address = "Event proxy address";
phrase lng_wallet_send_recipient = "Recipient wallet address";
phrase lng_wallet_send_address = "Enter wallet address";
phrase lng_wallet_send_token_direct_address = "Enter token wallet address";
phrase lng_wallet_send_token_owner_address = "Enter owner or ETH address";
phrase lng_wallet_send_token_ethereum_address = "Enter Ethereum address";
phrase lng_wallet_send_token_proxy_address_placeholder = "Enter event proxy address";
phrase lng_wallet_send_about =
    "Copy the wallet address of the recipient here or ask them to send you a freeton:// link.";
phrase lng_wallet_send_amount = "Amount";
phrase lng_wallet_send_balance = "Balance: {amount}";
phrase lng_wallet_send_comment = "Comment (optional)";
phrase lng_wallet_send_button = "Send {ticker}";
phrase lng_wallet_send_button_amount = "Send {amount}";
phrase lng_wallet_send_button_swap_back = "Swap Back {ticker}";
phrase lng_wallet_send_button_swap_back_amount = "Swap Back {amount}";

phrase lng_wallet_send_stake_title = "Send stake";
phrase lng_wallet_send_stake_amount = "Amount";
phrase lng_wallet_send_stake_balance = "Balance: {amount}";
phrase lng_wallet_send_stake_warning =
    "The staking mechanism is currently at the testing stage, do not use the amount that you cannot lose.";
phrase lng_wallet_send_stake_button = "Send stake";
phrase lng_wallet_send_stake_button_amount = "Stake {amount}";

phrase lng_wallet_withdraw_title = "Withdraw";
phrase lng_wallet_withdraw_all = "All";
phrase lng_wallet_withdraw_part = "Part";
phrase lng_wallet_withdraw_amount = "Amount";
phrase lng_wallet_withdraw_locked = "Locked: {amount}";
phrase lng_wallet_withdraw_button_part = "Withdraw part";
phrase lng_wallet_withdraw_button_all = "Withdraw all";
phrase lng_wallet_withdraw_button_amount = "Withdraw {amount}";

phrase lng_wallet_cancel_withdrawal_title = "Cancel withdrawal";
phrase lng_wallet_cancel_withdrawal_description = "It will reset withdrawal amount and enable reinvestment.";
phrase lng_wallet_cancel_withdrawal_button = "Confirm";

phrase lng_wallet_deploy_token_wallet_title = "Deploy token wallet";
phrase lng_wallet_deploy_token_wallet_owned_description =
    "Deployed TIP3 token wallet can decrease transaction fee for sender.";
phrase lng_wallet_deploy_token_wallet_target_description =
    "The recipient does not have a token wallet. In order for it to receive tokens, it must be created.";
phrase lng_wallet_deploy_token_wallet_button = "Confirm";

phrase lng_wallet_send_failed_title = "Sending failed";
phrase lng_wallet_send_failed_text =
    "Could not perform the transaction. Please check your wallet's balance and try again.";

phrase lng_wallet_add_depool_succeeded = "DePool added successfully!";
phrase lng_wallet_add_depool_failed_title = "Failed to add DePool";
phrase lng_wallet_add_depool_failed_text =
    "The specified account was not found, or it is neither DePoolV1 nor DePoolV2";

phrase lng_wallet_add_token_succeeded = "TIP3 Token added successfully!";
phrase lng_wallet_add_token_failed_title = "Failed to add TIP3 Token";
phrase lng_wallet_add_token_failed_text = "The specified account was not found, or it is not a root token contract";

phrase lng_wallet_send_tokens_recipient_not_found = "Recipient token wallet not found";
phrase lng_wallet_send_tokens_recipient_changed = "Sending directly to the token wallet";

phrase lng_wallet_confirm_title = "Confirmation";
phrase lng_wallet_confirm_text = "Do you want to send **{grams}** to:";
phrase lng_wallet_confirm_withdrawal_text = "Do you want to withdraw **{grams}** from:";
phrase lng_wallet_confirm_cancel_withdrawal_text = "Do you want to cancel withdrawal from:";
phrase lng_wallet_confirm_deploy_token_wallet_text = "Do you want to deploy a token wallet for:";
phrase lng_wallet_confirm_fee = "Fee: ~{grams}";
phrase lng_wallet_confirm_send = "Send {ticker}";
phrase lng_wallet_confirm_withdrawal = "Withdraw";
phrase lng_wallet_confirm_cancel_withdrawal = "Confirm";
phrase lng_wallet_confirm_deploy_token_wallet = "Deploy";
phrase lng_wallet_confirm_warning = "**Note:** your comment \xC2\xAB{comment}\xC2\xBB **will not be encrypted**.";

phrase lng_wallet_same_address_title = "Warning";
phrase lng_wallet_same_address_text =
    "Sending TON from a wallet to the same wallet doesn't make sense, you will simply waste a portion of the value on "
    "blockchain fees.";
phrase lng_wallet_same_address_proceed = "Proceed";

phrase lng_wallet_passcode_title = "Password";
phrase lng_wallet_passcode_enter = "Enter your password";
phrase lng_wallet_passcode_next = "Next";
phrase lng_wallet_passcode_incorrect = "Incorrect password.";

phrase lng_wallet_sending_title = "Sending {ticker}";
phrase lng_wallet_sending_text = "Please wait a few seconds for your\ntransaction to be processed...";
phrase lng_wallet_sending_all_stake = "All stake was requested for withdrawal.";

phrase lng_wallet_sent_title = "Done!";
phrase lng_wallet_sent_cancel_withdrawal = "Withdrawal cancelled";
phrase lng_wallet_sent_deploy_token_wallet = "Deployed";
phrase lng_wallet_sent_close = "Close";
phrase lng_wallet_sent_close_view = "View";

phrase lng_wallet_add_asset_title = "New Asset";
phrase lng_wallet_add_asset_token = "Add TIP3 Token";
phrase lng_wallet_add_asset_depool = "Add DePool";
phrase lng_wallet_add_asset_address = "Address";
phrase lng_wallet_add_asset_token_address = "Root token contract address";
phrase lng_wallet_add_asset_depool_address = "DePool address";
phrase lng_wallet_add_asset_confirm = "Confirm";

phrase lng_wallet_settings_title = "Settings";
phrase lng_wallet_settings_version_title = "Version and updates";
phrase lng_wallet_settings_autoupdate = "Update automatically";
phrase lng_wallet_settings_version = "Version {version}";
phrase lng_wallet_settings_checking = "Checking for updates...";
phrase lng_wallet_settings_latest = "Latest version is installed";
phrase lng_wallet_settings_check = "Check for updates";
phrase lng_wallet_settings_downloading = "Downloading update {progress}...";
phrase lng_wallet_settings_ready = "New version is ready";
phrase lng_wallet_settings_fail = "Update check failed :(";
phrase lng_wallet_settings_update = "Update Wallet";
phrase lng_wallet_settings_configuration = "Server Settings";
phrase lng_wallet_settings_update_config = "Update config automatically";
phrase lng_wallet_settings_config_url = "Config update URL";
phrase lng_wallet_settings_config_from_file = "Load from file";
phrase lng_wallet_settings_mainnet = "Main Network";
phrase lng_wallet_settings_testnet = "Test Network";
phrase lng_wallet_settings_blockchain_name = "Blockchain ID";

phrase lng_wallet_warning_reconnect = "If you proceed, you will need to reconnect your wallet using 24 secret words.";
phrase lng_wallet_warning_blockchain_name =
    "Are you sure you want to change the blockchain ID? You don't need this unless you're testing your own TON "
    "network.";
phrase lng_wallet_warning_to_testnet =
    "Are you sure you want to switch to the Test Free TON network? It exists only for testing purposes.";
phrase lng_wallet_warning_to_mainnet =
    "Are you sure you want to switch to the Main Free TON network? TON will have real value there.";
phrase lng_wallet_bad_config = "Sorry, this config is invalid.";
phrase lng_wallet_bad_config_url = "Could not load config from URL.";
phrase lng_wallet_wait_pending = "Please wait until the current transaction is completed.";
phrase lng_wallet_wait_syncing = "Please wait until the synchronization is completed.";

phrase lng_wallet_downloaded = "{ready} / {total} {mb}";

Fn<phrase(int)> lng_wallet_refreshed_minutes_ago = [](int minutes) {
  return (minutes == 1) ? "updated one minute ago" : "updated " + QString::number(minutes) + " minutes ago";
};

Fn<phrase(QDate)> lng_wallet_short_date = [](QDate date) {
  const auto month = date.month();
  const auto result = [&]() -> QString {
    switch (month) {
      case 1:
        return "January";
      case 2:
        return "February";
      case 3:
        return "March";
      case 4:
        return "April";
      case 5:
        return "May";
      case 6:
        return "June";
      case 7:
        return "July";
      case 8:
        return "August";
      case 9:
        return "September";
      case 10:
        return "October";
      case 11:
        return "November";
      case 12:
        return "December";
    }
    return QString();
  }();
  if (result.isEmpty()) {
    return result;
  }
  const auto small = result + ' ' + QString::number(date.day());
  const auto year = date.year();
  const auto current = QDate::currentDate();
  const auto currentYear = current.year();
  const auto currentMonth = current.month();
  if (year == currentYear) {
    return small;
  }
  const auto yearIsMuchGreater = [](int year, int otherYear) { return (year > otherYear + 1); };
  const auto monthIsMuchGreater = [](int year, int month, int otherYear, int otherMonth) {
    return (year == otherYear + 1) && (month + 12 > otherMonth + 3);
  };
  if (false || yearIsMuchGreater(year, currentYear) || yearIsMuchGreater(currentYear, year) ||
      monthIsMuchGreater(year, month, currentYear, currentMonth) ||
      monthIsMuchGreater(currentYear, currentMonth, year, month)) {
    return small + ", " + QString::number(year);
  }
  return small;
};

Fn<phrase(QTime)> lng_wallet_short_time = [](QTime time) { return time.toString(Qt::SystemLocaleShortDate); };

Fn<phrase(QString, const Ton::Symbol &)> lng_wallet_grams_count = [](QString text, const Ton::Symbol &symbol) {
  return text + " " + symbol.name();
};

Fn<phrase(QString, const Ton::Symbol &)> lng_wallet_grams_count_sent = [](QString text, const Ton::Symbol &symbol) {
  return text + " " + symbol.name() + ((text == "1") ? " has been sent." : " have been sent.");
};

Fn<phrase(QString)> lng_wallet_grams_count_withdrawn = [](QString text) {
  return text + " " + Ton::Symbol::ton().name() +
         ((text == "1") ? " was requested for withdrawal." : " were requested for withdrawal.");
};

}  // namespace ph
