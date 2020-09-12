#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/transaction.hpp>

#define TOSTR_(T) #T
#define TOSTR(T) TOSTR_(T)

class [[eosio::contract("cambiatus.token")]] token : public eosio::contract
{
public:
  using contract::contract;

  TABLE account
  {
    eosio::asset balance;
    uint32_t last_activity;

    uint64_t primary_key() const { return balance.symbol.code().raw(); }

    EOSLIB_SERIALIZE(account, (balance)(last_activity));
  };

  TABLE currency_stats
  {
    eosio::asset supply;
    eosio::asset max_supply;
    eosio::asset min_balance;
    eosio::name issuer;
    std::string type;

    uint64_t primary_key() const { return supply.symbol.code().raw(); }

    EOSLIB_SERIALIZE(currency_stats, (supply)(max_supply)(min_balance)(issuer)(type));
  };

  TABLE expiry_options
  {
    eosio::symbol currency;
    std::uint32_t expiration_period;
    eosio::asset renovation_amount;

    uint64_t primary_key() const { return currency.code().raw(); }

    EOSLIB_SERIALIZE(expiry_options, (currency)(expiration_period)(renovation_amount));
  };

  /// @abi action
  /// Create a new BeSpiral Token
  ACTION create(eosio::name issuer, eosio::asset max_supply, eosio::asset min_balance, std::string type);

  /// @abi action
  /// Update a BeSpiral Token properties
  ACTION update(eosio::asset max_supply, eosio::asset min_balance);

  /// @abi action
  /// Transfer BeSpiral compatible tokens between users.
  ACTION transfer(eosio::name from, eosio::name to, eosio::asset quantity, std::string memo);

  /// @abi action
  /// Issue / Mint new BeSpiral compatible tokens
  ACTION issue(eosio::name to, eosio::asset quantity, std::string memo);

  /// @abi action
  /// Retire tokens from a given account
  ACTION retire(eosio::name from, eosio::asset quantity, std::string memo);

  /// @abi action
  /// Set expiry options to a given token
  ACTION setexpiry(eosio::symbol currency, std::uint32_t expiration_period, eosio::asset renovation_amount);

  /// @abi action
  /// Init empty balance for a given account
  ACTION initacc(eosio::symbol currency, eosio::name account, eosio::name inviter);

  typedef eosio::multi_index<eosio::name{"accounts"}, account> accounts;
  typedef eosio::multi_index<eosio::name{"stat"}, currency_stats> stats;
  typedef eosio::multi_index<eosio::name{"expiryopts"}, expiry_options> expiry_opts;

  void sub_balance(eosio::name owner, eosio::asset value, const token::currency_stats &st);
  void add_balance(eosio::name owner, eosio::asset value, const token::currency_stats &st);
  void renovate_expiration(eosio::name account, const token::currency_stats &st);

  token::expiry_options get_expiration_opts(const token::currency_stats &st);
};

const auto community_account = eosio::name{TOSTR(__COMMUNITY_ACCOUNT__)};

struct community
{
  eosio::symbol symbol;

  eosio::name creator;
  std::string logo;
  std::string title;
  std::string description;

  eosio::asset inviter_reward;
  eosio::asset invited_reward;

  std::uint8_t has_objectives;
  std::uint8_t has_shop;

  std::uint64_t primary_key() const { return symbol.raw(); }
};
typedef eosio::multi_index<eosio::name{"community"}, community> bespiral_communities;

struct network
{
  std::uint64_t id;

  eosio::symbol community;
  eosio::name invited_user;
  eosio::name invited_by;

  std::uint64_t primary_key() const { return id; }
  std::uint64_t users_by_cmm() const { return community.raw(); }
};

typedef eosio::multi_index<eosio::name{"network"},
                           network,
                           eosio::indexed_by<eosio::name{"usersbycmm"},
                                             eosio::const_mem_fun<network, uint64_t, &network::users_by_cmm>>>
    bespiral_networks;
