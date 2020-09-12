#include "token.hpp"
#include "../utils/utils.cpp"

/**
   Creates a Cambiatus token.
   @author Julien Lucca
   @version 1.0

   Every token is related to a community. The community must exist in order for a token to be created.
   We use eosio::symbol type and check for the given params with the following rules:

   1) Currently supports two Token Types: `mcc` for multual credit clearing and `expiry` for expiration tokens
   2) Only the community issuer can create new Tokens
   3) Symbol must be unique and the same for both the community and the token
 */
void token::create(eosio::name issuer, eosio::asset max_supply,
                   eosio::asset min_balance, std::string type)
{
  auto sym = max_supply.symbol;
  eosio::check(max_supply.symbol == min_balance.symbol, "All assets must share the same symbol");
  eosio::check(type == "mcc" || type == "expiry", "type must be 'mcc' or 'expiry'");

  // Find existing community
  bespiral_communities communities(community_account, community_account.value);
  const auto &cmm = communities.get(sym.raw(), "can't find community. Cambiatus Tokens require a community.");

  eosio::check(sym.is_valid(), "invalid symbol");
  eosio::check(max_supply.is_valid(), "invalid max_supply");
  eosio::check(max_supply.amount > 0, "max max_supply must be positive");

  // Community creator must be the one creating the token
  require_auth(cmm.creator);

  // MCC only validations
  if (type == "mcc")
  {
    eosio::check(min_balance.is_valid(), "invalid min_balance");
    eosio::check(min_balance.amount <= 0, "min_balance must be equal or less than 0");
    eosio::check(max_supply.symbol == min_balance.symbol, "unmatched symbols for max_supply and min_balance. They must be the same");
  }

  stats statstable(_self, sym.code().raw());
  auto existing = statstable.find(sym.code().raw());
  eosio::check(existing == statstable.end(), "token with this symbol already exists");

  statstable.emplace(_self, [&](auto &s) {
    s.supply.symbol = max_supply.symbol;
    s.max_supply = max_supply;
    s.min_balance = min_balance;
    s.issuer = issuer;
    s.type = type;
  });

  // Notify creator
  require_recipient(cmm.creator);

  // Netlink issuer
  if (issuer != cmm.creator)
  {
    require_recipient(issuer);
    eosio::action netlink_issuer = eosio::action(eosio::permission_level{cmm.creator, eosio::name{"active"}}, // Permission
                                                 community_account,                                           // Account
                                                 eosio::name{"netlink"},                                      // Action
                                                 // cmm_asset, new_user, inviter
                                                 std::make_tuple(max_supply, issuer, cmm.creator));
    netlink_issuer.send();
  }

  // Create new balance for the creator
  accounts accounts(_self, issuer.value);
  accounts.emplace(_self, [&](auto &a) {
    a.balance = eosio::asset(0, max_supply.symbol);
    a.last_activity = now();
  });
}

/**
   Update token configurations
   @author Julien Lucca
   @version 1.0
*/
void token::update(eosio::asset max_supply, eosio::asset min_balance)
{
  eosio::check(max_supply.symbol == min_balance.symbol, "All assets must share the same symbol");

  eosio::check(min_balance.is_valid(), "invalid min_balance");
  eosio::check(max_supply.is_valid(), "invalid max_supply");
  eosio::check(max_supply.amount > 0, "max max_supply must be positive");

  // Find existing community
  bespiral_communities communities(community_account, community_account.value);
  const auto &cmm = communities.get(max_supply.symbol.raw(), "can't find community. Cambiatus Tokens require a community.");

  // Find token stats
  stats statstable(_self, max_supply.symbol.code().raw());
  const auto &st = statstable.get(min_balance.symbol.code().raw(), "token with given symbol does not exist, create token before issue");

  require_auth(st.issuer);

  statstable.modify(st, _self, [&](auto &s) {
    s.max_supply = max_supply;
    s.min_balance = min_balance;
  });
}

/**
   Issue / Mint tokens.
   @author Julien Lucca
   @version 1.0

   Allows the community to issue new tokens. It can be done by only by the issuer, and it is limited by the maximum supply available.

   You can choose to send the newly minted tokens to a specific account.
 */
void token::issue(eosio::name to, eosio::asset quantity, std::string memo)
{
  eosio::symbol sym = quantity.symbol;
  eosio::check(sym.is_valid(), "invalid symbol name");
  eosio::check(memo.size() <= 256, "memo has more than 256 bytes");

  stats statstable(_self, sym.code().raw());
  const auto &st = statstable.get(sym.code().raw(), "token with given symbol does not exist, create token before issue");

  // Require auth from the bespiral community contract
  require_auth(_self);

  eosio::check(quantity.is_valid(), "invalid quantity");
  eosio::check(quantity.amount > 0, "must issue positive quantity");
  eosio::check(quantity.symbol == st.supply.symbol, "symbol mismatch");
  eosio::check(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

  statstable.modify(st, _self, [&](auto &s) {
    s.supply += quantity;
  });

  add_balance(st.issuer, quantity, st);

  if (to != st.issuer)
  {
    require_recipient(st.issuer);

    SEND_INLINE_ACTION(*this,
                       transfer,
                       {_self, eosio::name{"active"}},
                       {st.issuer, to, quantity, memo});
  }
}

void token::transfer(eosio::name from, eosio::name to, eosio::asset quantity, std::string memo)
{
  eosio::check(from != to, "cannot transfer to self");

  // Require auth from self or from contract
  if (has_auth(from))
  {
    require_auth(from);
  }
  else
  {
    require_auth(_self);
  }

  eosio::check(is_account(to), "destination account doesn't exists");

  // Find symbol stats
  auto sym = quantity.symbol;
  stats statstable(_self, sym.code().raw());
  const auto &st = statstable.get(sym.code().raw(), "token with given symbol doesn't exists");

  // Validate quantity and memo
  eosio::check(quantity.is_valid(), "invalid quantity");
  eosio::check(quantity.amount > 0, "quantity must be positive");
  eosio::check(quantity.symbol == st.max_supply.symbol, "symbol precision mismatch");
  eosio::check(memo.size() <= 256, "memo has more than 256 bytes");

  // Check if from belongs to the community
  bespiral_networks network(community_account, community_account.value);
  auto from_id = gen_uuid(quantity.symbol.raw(), from.value);
  auto itr_from = network.find(from_id);
  eosio::check(itr_from != network.end(), "from account doesn't belong to the community");

  // Check if to belongs to the community
  auto to_id = gen_uuid(quantity.symbol.raw(), to.value);
  auto itr_to = network.find(to_id);
  eosio::check(itr_to != network.end(), "to account doesn't belong to the community");

  // Transfer values
  sub_balance(from, quantity, st);
  add_balance(to, quantity, st);
}

/*
  Retire tokens of a given account
  It can only be called and signed from the contract itself and it is used by the expiry feature.
  It removes a certain quantity of tokens out of the circulation if the owner doesn't use it
 */
void token::retire(eosio::name from, eosio::asset quantity, std::string memo)
{
  eosio::check(eosio::get_sender() == get_self(), "This action can only be called from the contract");
  require_auth(get_self());

  auto sym = quantity.symbol;
  eosio::check(sym.is_valid(), "invalid symbol name");
  eosio::check(memo.size() <= 256, "memo has more than 256 bytes");

  token::stats statstable(_self, sym.code().raw());
  auto existing = statstable.find(sym.code().raw());
  eosio::check(existing != statstable.end(), "token with symbol does not exist");
  const auto &st = *existing;

  eosio::check(st.type == "expiry", "Cambiatus only retire tokens of the 'expiry' type");

  eosio::check(quantity.is_valid(), "invalid quantity");
  eosio::check(quantity.amount > 0, "must retire positive quantity");
  eosio::check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

  token::accounts accounts(_self, from.value);
  auto from_account = accounts.get(quantity.symbol.code().raw(), "Can't find the account");

  // Get expiration values
  token::expiry_options opts = get_expiration_opts(st);

  // Do nothing if it isn't expired yet
  if (from_account.last_activity + opts.expiration_period < now())
  {
    return;
  }

  // When the quantity is bigger, just invalidates what the user have
  if (from_account.balance >= quantity)
  {
    quantity = from_account.balance;
  }

  // Decrease balance from the user
  sub_balance(from, quantity, st);

  // Decrease available supply
  statstable.modify(st, _self, [&](auto &s) {
    s.supply -= quantity;
  });
}

void token::initacc(eosio::symbol currency, eosio::name account, eosio::name inviter)
{
  // Validate auth -- can only be called by the Cambiatus contracts
  // require_auth(_self);
  if (eosio::get_sender() == community_account)
  {
    require_auth(inviter);
  }
  else
  {
    require_auth(_self);
  }

  // Make sure token exists on the stats table
  stats statstable(_self, currency.code().raw());
  const auto &st = statstable.get(currency.code().raw(), "token with given symbol does not exist, create token before initacc");

  // Make sure account belongs to the given community
  // Check if from belongs to the community
  bespiral_networks network(community_account, community_account.value);
  auto network_id = gen_uuid(currency.raw(), account.value);
  auto itr_net = network.find(network_id);
  eosio::check(itr_net != network.end(), "account doesn't belong to the community");

  // Create account table entry
  accounts accounts(_self, account.value);
  auto found_account = accounts.find(currency.code().raw());

  if (found_account == accounts.end())
  {
    accounts.emplace(_self, [&](auto &a) {
      a.balance = eosio::asset(0, st.supply.symbol);
      a.last_activity = now();
    });
  }
}

/**
 * Upsert Expiration options for a given currency.
 * @author Julien Lucca
 * @version 1.0
 *
 * Upsert expiration details on `expiryopts` table. Also fill amounts for every account on the network and schedules its retirement
 *
 * 1) Upserts given expiration options (`expiration_period` in seconds and `renovation_amount` in eosio::asset) for the given `currency`
 * 2) Iterates over the network table. For every account on the community.
 *  2.1) Generate new schedule ID, a compound of the currency symbol and the account name
 *  2.2) Looks for any scheduled `retire` calls and cancels it
 *  2.3) Issue for the account the given `renovation_amount`
 *  2.4) Schedules a `retire` action for the given `renovation_amount` after the given `expiration_period` using the generated schedule ID
 */
void token::setexpiry(eosio::symbol currency, std::uint32_t expiration_period, eosio::asset renovation_amount)
{
  // Validate data
  eosio::check(currency.is_valid(), "invalid symbol name");

  // Validate community
  token::stats statstable(_self, currency.code().raw());
  auto existing = statstable.find(currency.code().raw());
  eosio::check(existing != statstable.end(), "token with symbol does not exist");
  const auto &st = *existing;

  eosio::check(st.type != "mcc", "you can only configure tokens of the 'expiry' type");
  eosio::check(currency == renovation_amount.symbol, "symbol precision mismatch");
  eosio::check(currency == st.supply.symbol, "symbol precision mismatch");

  // Only the token issuer can configure that
  require_auth(st.issuer);

  // Save data
  token::expiry_opts opts(_self, _self.value);
  auto old_opts = opts.find(currency.code().raw());

  if (old_opts == opts.end())
  {
    opts.emplace(_self, [&](auto &a) {
      a.currency = currency;
      a.expiration_period = expiration_period;
      a.renovation_amount = renovation_amount;
    });
  }
  else
  {
    opts.modify(old_opts, _self, [&](auto &a) {
      a.currency = currency;
      a.expiration_period = expiration_period;
      a.renovation_amount = renovation_amount;
    });
  }

  // TODO: Start working on the Network loop
  // You should first get a call on the community contract, find the network table structure you need and do validations

  bespiral_networks network(community_account, community_account.value);
  auto network_by_cmm = network.get_index<eosio::name{"usersbycmm"}>();
  for (auto itr = network_by_cmm.find(currency.raw()); itr != network_by_cmm.end(); itr++)
  {
    // TODO Issue tokens too

    auto id = gen_uuid(currency.raw(), itr->invited_user.value);
    eosio::cancel_deferred(id);

    std::string memo = "Your tokens expired! Its been " + std::to_string(expiration_period) + " seconds since the emission!";
    eosio::action retire = eosio::action(eosio::permission_level{get_self(), eosio::name{"active"}}, // Permission
                                         get_self(),                                                 // Account
                                         eosio::name{"retire"},                                      // Action
                                         std::make_tuple(itr->invited_user, currency, memo));

    // TODO: Remove this. Print row values for now
    eosio::print_f("Network table: {%, % with ID %}\n", itr->community, itr->invited_user, id);
  }
}

void token::sub_balance(eosio::name owner, eosio::asset value, const token::currency_stats &st)
{
  eosio::check(value.is_valid(), "Invalid value");
  eosio::check(value.amount > 0, "Can only transfer positive values");

  // Check for existing balance
  token::accounts accounts(_self, owner.value);
  auto from = accounts.find(value.symbol.code().raw());

  // Add balance table entry
  if (from == accounts.end())
  {
    eosio::check((value.amount * -1) >= st.min_balance.amount, "overdrawn community limit");

    accounts.emplace(_self, [&](auto &a) {
      a.balance = value;
      a.balance.amount *= -1;
      a.last_activity = now();
    });
  }
  else
  {
    auto new_balance = from->balance.amount - value.amount;
    eosio::check(new_balance >= st.min_balance.amount, "overdrawn community limit");
    accounts.modify(from, _self, [&](auto &a) {
      a.balance.amount -= value.amount;
      a.last_activity = now();
    });
  }
  return;
}

void token::add_balance(eosio::name recipient, eosio::asset value, const token::currency_stats &st)
{
  eosio::check(value.is_valid(), "Invalid value");
  eosio::check(value.amount > 0, "Can only transfer positive values");

  accounts accounts(_self, recipient.value);
  auto to = accounts.find(value.symbol.code().raw());

  if (to == accounts.end())
  {
    accounts.emplace(_self, [&](auto &a) {
      a.balance = value;
      a.last_activity = now();
    });
  }
  else
  {
    accounts.modify(to, _self, [&](auto &a) {
      a.balance += value;
      a.last_activity = now();
    });
  }
}

/*
  Gets the configuration for a given community. If it doesn't have any, it uses the contract default
 */
token::expiry_options token::get_expiration_opts(const token::currency_stats &st)
{
  // Default expiration values
  // 90 days * 24 hours * 60 minutes * 60 seconds
  std::uint32_t validation_period = 7776000;
  eosio::asset minimum_amount = eosio::asset(50, st.supply.symbol);

  token::expiry_opts opts(_self, _self.value);
  auto existing_opts = opts.find(st.supply.symbol.code().raw());

  if (existing_opts != opts.end())
  {
    const auto &exp_opts = *existing_opts;
    validation_period = exp_opts.expiration_period;
    minimum_amount = exp_opts.renovation_amount;
  }

  auto expiry_struct = token::expiry_options();
  expiry_struct.expiration_period = validation_period;
  expiry_struct.renovation_amount = minimum_amount;
  return expiry_struct;
}

EOSIO_DISPATCH(token,
               (create)(update)(issue)(transfer)(retire)(setexpiry)(initacc));
