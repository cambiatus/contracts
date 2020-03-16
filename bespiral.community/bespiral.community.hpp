#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/system.h>
#include<eosiolib/singleton.hpp>
#include <eosiolib/crypto.h>

class [[eosio::contract("bespiral.community")]] bespiral : public eosio::contract {
 public:

  using contract::contract;

  TABLE community {
    eosio::symbol symbol;

    eosio::name creator;
    std::string logo;
    std::string name;
    std::string description;
    eosio::asset inviter_reward;
    eosio::asset invited_reward;

    uint64_t primary_key() const { return symbol.raw(); };

    EOSLIB_SERIALIZE(community,
                     (symbol)(creator)(logo)(name)(description)
                     (inviter_reward)(invited_reward));
  };

  TABLE network {
    std::uint64_t id;

    eosio::symbol community;
    eosio::name invited_user;
    eosio::name invited_by;

    // keys and indexes
    std::uint64_t primary_key() const { return id; }
    std::uint64_t users_by_cmm() const { return community.raw(); }

    EOSLIB_SERIALIZE(network,
                     (id)(community)(invited_user)(invited_by));
  };

  TABLE objective {
    std::uint64_t id;
    std::string description;
    eosio::symbol community;
    eosio::name creator;

    // keys and indexes
    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_cmm() const { return community.raw(); }

    EOSLIB_SERIALIZE(objective,
                     (id)(description)
                     (community)(creator));
  };

  TABLE action {
    std::uint64_t id;
    std::uint64_t objective_id;
    std::string description;
    eosio::asset reward;
    eosio::asset verifier_reward;
    std::uint64_t deadline; // Max date where it can be claimed
    std::uint64_t usages; // Max usages
    std::uint64_t usages_left;
    std::uint64_t verifications; // # verifications needed
    std::string verification_type; // Can be 'automatic' and 'claimable'
    std::uint8_t is_completed;
    eosio::name creator;

    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_objective() const { return objective_id; }

    EOSLIB_SERIALIZE(action,
                     (id)(objective_id)(description)(reward)
                     (verifier_reward)(deadline)(usages)
                     (usages_left)(verifications)
                     (verification_type)(is_completed)(creator));
  };

  TABLE action_validator {
    std::uint64_t id;
    std::uint64_t action_id;
    eosio::name validator;

    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_action() const { return action_id; }

    EOSLIB_SERIALIZE(action_validator,
                     (id)(action_id)(validator));
  };

  TABLE claim {
    std::uint64_t id;
    std::uint64_t action_id;
    eosio::name claimer;
    std::uint8_t is_verified; // If the number of verifications reached the necessary #

    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_action() const { return action_id; }

    EOSLIB_SERIALIZE(claim,
                     (id)(action_id)(claimer)(is_verified));
  };

  TABLE check {
    std::uint64_t id;
    std::uint64_t claim_id;
    eosio::name validator;
    std::uint8_t is_verified; // Answer the verificator gave

    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_claim() const { return claim_id; }

    EOSLIB_SERIALIZE(check,
                     (id)(claim_id)(validator)(is_verified));
  };

  TABLE sale {
    std::uint64_t id;
    eosio::name creator;
    eosio::symbol community;
    std::string title;
    std::string description;
    std::string image;
    std::uint8_t track_stock;
    eosio::asset quantity;    // Actual price of product/service
    std::uint64_t units;      // How many are available

    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_cmm() const { return community.raw(); }
    std::uint64_t by_user() const { return creator.value; }

    EOSLIB_SERIALIZE(sale,
                     (id)(creator)(community)
                     (title)(description)(image)
                     (track_stock)(quantity)(units));
  };

  TABLE indexes {
    std::uint64_t last_used_sale_id;
    std::uint64_t last_used_objective_id;
    std::uint64_t last_used_action_id;
    std::uint64_t last_used_claim_id;
  };

  /// @abi action
  /// Creates a BeSpiral community
  ACTION create(eosio::asset cmm_asset, eosio::name creator, std::string logo, std::string name,
                std::string description, eosio::asset inviter_reward, eosio::asset invited_reward);

  /// @abi action
  /// Updates community attributes
  ACTION update(eosio::asset cmm_asset, std::string logo, std::string name,
                std::string description, eosio::asset inviter_reward, eosio::asset invited_reward);

  /// @abi action
  /// Adds a user to a community
  ACTION netlink(eosio::asset cmm_asset, eosio::name inviter, eosio::name new_user);

  /// @abi action
  /// Create a new community objective
  ACTION newobjective(eosio::asset cmm_asset, std::string description, eosio::name creator);

  /// @abi action
  /// Edit the description of a given objective
  ACTION updobjective(std::uint64_t objective_id, std::string description, eosio::name editor);

  /// @abi action
  /// Update action
  ACTION upsertaction(std::uint64_t action_id, std::uint64_t objective_id,
                      std::string description, eosio::asset reward,
                      eosio::asset verifier_reward, std::uint64_t deadline,
                      std::uint64_t usages, std::uint64_t usages_left,
                      std::uint64_t verifications, std::string verification_type,
                      std::string validators_str, std::uint8_t is_completed,
                      eosio::name creator);

  /// @abi action
  /// Start a new claim on an action
  ACTION claimaction(std::uint64_t action_id, eosio::name maker);

  /// @abi action
  /// Send a vote verification for a given claim. It has to be `claimable` verification_type
  ACTION verifyclaim(std::uint64_t claim_id, eosio::name verifier, std::uint8_t vote);

  /// @abi action
  /// Verify that a given action was completed. It has to have the `automatic` verification_type
  ACTION verifyaction(std::uint64_t action_id, eosio::name maker, eosio::name verifier);

  /// @abi action
  /// Create a new sale
  ACTION createsale(eosio::name from, std::string title, std::string description,
                    eosio::asset quantity, std::string image,
                    std::uint8_t track_stock, std::uint64_t units);

  /// @abi action
  /// Update some sale details
  ACTION updatesale(std::uint64_t sale_id, std::string title,
                    std::string description, eosio::asset quantity,
                    std::string image, std::uint8_t track_stock, std::uint64_t units);

  /// @abi action
  /// Delete a sale
  ACTION deletesale(std::uint64_t sale_id);

  /// @abi action
  /// Vote in a sale
  ACTION reactsale(std::uint64_t sale_id, eosio::name from, std::string type);

  /// @abi action
  /// Offchain event hook for when a transfer occours in our shop
  ACTION transfersale(std::uint64_t sale_id, eosio::name from, eosio::name to, eosio::asset quantity, std::uint64_t units);

	/// @abi action
	/// Set the indices for a chain
	ACTION setindices(std::uint64_t sale_id, std::uint64_t objective_id, std::uint64_t action_id, std::uint64_t claim_id);

  ACTION deleteact(std::uint64_t id);

  //Get available key
  uint64_t get_available_id(std::string table);


  typedef eosio::multi_index<eosio::name{"community"}, bespiral::community> communities;
  typedef eosio::multi_index<eosio::name{"network"},
                             bespiral::network,
                             eosio::indexed_by<eosio::name{"usersbycmm"},
                                               eosio::const_mem_fun<bespiral::network, uint64_t, &bespiral::network::users_by_cmm>>
                             > networks;

  typedef eosio::multi_index<eosio::name{"objective"},
                             bespiral::objective,
                             eosio::indexed_by<eosio::name{"bycmm"},
                                               eosio::const_mem_fun<bespiral::objective, uint64_t, &bespiral::objective::by_cmm>>
                             > objectives;

  typedef eosio::multi_index<eosio::name{"action"},
                             bespiral::action,
                             eosio::indexed_by<eosio::name{"byobj"},
                                               eosio::const_mem_fun<bespiral::action, uint64_t, &bespiral::action::by_objective>>
                             > actions;

  typedef eosio::multi_index<eosio::name{"validator"},
                             bespiral::action_validator,
                             eosio::indexed_by<eosio::name{"byaction"},
                                               eosio::const_mem_fun<bespiral::action_validator, uint64_t, &bespiral::action_validator::by_action>>
                             > validators;

  typedef eosio::multi_index<eosio::name{"claim"},
                             bespiral::claim,
                             eosio::indexed_by<eosio::name{"byaction"},
                                               eosio::const_mem_fun<bespiral::claim, uint64_t, &bespiral::claim::by_action>>
                             > claims;

  typedef eosio::multi_index<eosio::name{"check"},
                             bespiral::check,
                             eosio::indexed_by<eosio::name{"byclaim"},
                                               eosio::const_mem_fun<bespiral::check, uint64_t, &bespiral::check::by_claim>>
                             > checks;

  typedef eosio::multi_index<eosio::name{"sale"},
                             bespiral::sale,
                             eosio::indexed_by<eosio::name{"bycmm"}, eosio::const_mem_fun<bespiral::sale, uint64_t, &bespiral::sale::by_cmm>>,
                             eosio::indexed_by<eosio::name{"byuser"}, eosio::const_mem_fun<bespiral::sale, uint64_t, &bespiral::sale::by_user>>
                            > sales;

  typedef eosio::singleton<eosio::name{"indexes"}, bespiral::indexes> item_indexes;

  item_indexes curr_indexes;

  // Initialize our singleton table for indices
  bespiral(eosio::name receiver, eosio::name code, eosio::datastream<const char *> ds) : contract(receiver, code, ds), curr_indexes(_self, _self.value) {}
};

const auto currency_account = eosio::name{"bes.token"};
struct currency_stats {
  eosio::asset supply;
  eosio::asset max_supply;
  eosio::asset min_balance;
  eosio::name issuer;
  std::string type;

  uint64_t primary_key() const { return supply.symbol.code().raw(); }
};
typedef eosio::multi_index<eosio::name{"stat"}, currency_stats> bespiral_tokens;
