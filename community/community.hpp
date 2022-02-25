#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>

#define TOSTR_(T) #T
#define TOSTR(T) TOSTR_(T)

enum permission
{
  invite,
  claim,
  order,
  verify,
  sell,
  award,
  transfer
};

class [[eosio::contract("community")]] cambiatus : public eosio::contract
{
public:
  using contract::contract;

  TABLE community
  {
    eosio::symbol symbol;

    eosio::name creator;
    std::string logo;
    std::string name;
    std::string description;

    eosio::asset inviter_reward;
    eosio::asset invited_reward;

    std::uint8_t has_objectives;
    std::uint8_t has_shop;
    std::uint8_t has_kyc;

    uint64_t primary_key() const { return symbol.raw(); };

    EOSLIB_SERIALIZE(community,
                     (symbol)(creator)(logo)(name)(description)(inviter_reward)(invited_reward)(has_objectives)(has_shop)(has_kyc));
  };

  TABLE network
  {
    std::uint64_t id;

    eosio::symbol community;
    eosio::name invited_user;
    eosio::name invited_by;
    std::string user_type;

    // keys and indexes
    std::uint64_t primary_key() const { return id; }
    std::uint64_t users_by_cmm() const { return community.raw(); }

    EOSLIB_SERIALIZE(network,
                     (id)(community)(invited_user)(invited_by)(user_type));
  };

  TABLE member
  {
    eosio::name name;
    eosio::name inviter;
    std::string user_type;
    std::vector<eosio::name> roles;

    std::uint64_t primary_key() const { return name.value; }

    EOSLIB_SERIALIZE(member,
                     (name)(inviter)(user_type)(roles));
  };

  /**
   * Roles is a way to identify people's relation with their community.
   * It can be used to allow people to be recognized by their role in the whole of the community, to give them awards or to help them to identify themselves.
   * They can be customized with a color and can have a name
   *
   * Roles can also be atttached with permissions that gives them abilities within the community such as the ability to invite new people in, to sell goods and services and more
   *
   * The initial abilities that we offer are:
   *  invite: invite new users `netlink`
   *  claim: allow claiming actions `claimaction`
   *  order: create new orders `transfersale`
   *  verify: verify autenticity on claims `verifyclaim`
   *  sell: sell items on the shop `createsale` `updatesale` .
   *  award: awards an action `verifyaction` (`award`)
   *
   * All of those roles are used from within the contract to mediate the usage of some actions.
   *
   *
   * Roles are scoped by community.
   */
  TABLE role
  {
    eosio::name name;
    std::vector<std::string> permissions;

    std::uint64_t primary_key() const { return name.value; }

    EOSLIB_SERIALIZE(role, (name)(permissions));
  };

  TABLE objective
  {
    std::uint64_t id;
    std::string description;
    eosio::symbol community;
    eosio::name creator;

    // keys and indexes
    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_cmm() const { return community.raw(); }

    EOSLIB_SERIALIZE(objective,
                     (id)(description)(community)(creator));
  };

  TABLE action
  {
    std::uint64_t id;
    std::uint64_t objective_id;
    std::string description;
    eosio::asset reward;
    eosio::asset verifier_reward;
    std::uint64_t deadline; // Max date where it can be claimed
    std::uint64_t usages;   // Max usages
    std::uint64_t usages_left;
    std::uint64_t verifications;   // # verifications needed
    std::string verification_type; // Can be 'automatic' and 'claimable'
    std::uint8_t is_completed;
    eosio::name creator;
    std::uint8_t has_proof_photo;
    std::uint8_t has_proof_code;
    std::string photo_proof_instructions;

    // std::vector<eosio::name> roles; // Validators of the action

    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_objective() const { return objective_id; }

    EOSLIB_SERIALIZE(action,
                     (id)(objective_id)(description)(reward)(verifier_reward)(deadline)(usages)(usages_left)(verifications)(verification_type)(is_completed)(creator)(has_proof_photo)(has_proof_code)(photo_proof_instructions));
  };

  TABLE action_validator
  {
    std::uint64_t id;
    std::uint64_t action_id;
    eosio::name validator;

    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_action() const { return action_id; }

    EOSLIB_SERIALIZE(action_validator,
                     (id)(action_id)(validator));
  };

  TABLE claim
  {
    std::uint64_t id;
    std::uint64_t action_id;
    eosio::name claimer;
    std::string status; // Can be: `approved` `rejected` `pending`
    std::string proof_photo;
    std::string proof_code;

    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_action() const { return action_id; }

    EOSLIB_SERIALIZE(claim,
                     (id)(action_id)(claimer)(status)(proof_photo)(proof_code));
  };

  TABLE check
  {
    std::uint64_t id;
    std::uint64_t claim_id;
    eosio::name validator;
    std::uint8_t is_verified; // Answer the verificator gave

    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_claim() const { return claim_id; }

    EOSLIB_SERIALIZE(check,
                     (id)(claim_id)(validator)(is_verified));
  };

  TABLE sale
  {
    std::uint64_t id;
    eosio::name creator;
    eosio::symbol community;
    std::string title;
    std::string description;
    std::string image;
    std::uint8_t track_stock;
    eosio::asset quantity; // Actual price of product/service
    std::uint64_t units;   // How many are available

    std::uint64_t primary_key() const { return id; }
    std::uint64_t by_cmm() const { return community.raw(); }
    std::uint64_t by_user() const { return creator.value; }

    EOSLIB_SERIALIZE(sale,
                     (id)(creator)(community)(title)(description)(image)(track_stock)(quantity)(units));
  };

  TABLE indexes
  {
    std::uint64_t last_used_sale_id;
    std::uint64_t last_used_objective_id;
    std::uint64_t last_used_action_id;
    std::uint64_t last_used_claim_id;
  };

  /// @abi action
  /// Creates a cambiatus community
  ACTION create(eosio::asset cmm_asset, eosio::name creator, std::string logo, std::string name,
                std::string description, eosio::asset inviter_reward, eosio::asset invited_reward,
                std::uint8_t has_objectives, std::uint8_t has_shop, std::uint8_t has_kyc,
                std::uint8_t auto_invite, std::string subdomain, std::string website);

  /// @abi action
  /// Updates community attributes
  ACTION update(eosio::asset cmm_asset, std::string logo, std::string name,
                std::string description, eosio::asset inviter_reward, eosio::asset invited_reward,
                std::uint8_t has_objectives, std::uint8_t has_shop, std::uint8_t has_kyc,
                std::uint8_t auto_invite, std::string subdomain, std::string website);

  /// @abi action
  /// Adds a user to a community
  ACTION netlink(eosio::symbol community_id, eosio::name inviter, eosio::name new_user, std::string user_type);

  /// @abi action
  /// Create/Edit a given objective
  ACTION upsertobjctv(eosio::symbol community_id, std::uint64_t objective_id, std::string description, eosio::name editor);

  /// @abi action
  /// Update action
  ACTION upsertaction(eosio::symbol community_id, std::uint64_t action_id, std::uint64_t objective_id,
                      std::string description, eosio::asset reward,
                      eosio::asset verifier_reward, std::uint64_t deadline,
                      std::uint64_t usages, std::uint64_t usages_left,
                      std::uint64_t verifications, std::string verification_type,
                      std::string validators_str, std::uint8_t is_completed,
                      eosio::name creator,
                      std::uint8_t has_proof_photo, std::uint8_t has_proof_code,
                      std::string photo_proof_instructions, std::string image);

  /// @abi action
  /// Start a new claim on an action
  ACTION claimaction(eosio::symbol community_id, std::uint64_t action_id, eosio::name maker,
                     std::string proof_photo, std::string proof_code, uint32_t proof_time);

  /// @abi action
  /// Send a vote verification for a given claim. It has to be `claimable` verification_type
  ACTION verifyclaim(eosio::symbol community_id, std::uint64_t claim_id, eosio::name verifier, std::uint8_t vote);

  /// @abi action
  /// Verify that a given action was completed. It has to have the `automatic` verification_type
  ACTION reward(eosio::symbol community_id, std::uint64_t action_id, eosio::name receiver, eosio::name awarder);

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
  /// Offchain event hook for when a transfer occours in our shop
  ACTION transfersale(std::uint64_t sale_id, eosio::name from, eosio::name to, eosio::asset quantity, std::uint64_t units);

  /// @abi action
  /// Upserts roles in a community
  ACTION upsertrole(eosio::symbol community_id, eosio::name name, std::string color, std::vector<std::string> & permissions);

  /// @abi action
  /// Sets a number of roles for an user
  ACTION assignroles(eosio::symbol community_id, eosio::name member, std::vector<eosio::name> & roles);

  /// @abi action
  /// Set the indices for a chain
  ACTION setindices(std::uint64_t sale_id, std::uint64_t objective_id, std::uint64_t action_id, std::uint64_t claim_id);

  /// @abi action
  /// Deletes an objective
  ACTION deleteobj(std::uint64_t id);

  /// @abi action
  /// Deletes an action
  ACTION deleteact(std::uint64_t id);

  ACTION clean(std::string t, eosio::name name_scope, eosio::symbol symbol_scope);

  // Get available key
  uint64_t get_available_id(std::string table);

  // Convinience methods
  bool is_member(eosio::symbol community_id, eosio::name user);
  bool has_permission(eosio::symbol community_id, eosio::name user, permission e_permission);
  std::string permission_to_string(permission e_permission);

  typedef eosio::multi_index<eosio::name{"community"}, cambiatus::community> communities;
  typedef eosio::multi_index<eosio::name{"member"}, cambiatus::member> members;

  typedef eosio::multi_index<eosio::name{"network"},
                             cambiatus::network,
                             eosio::indexed_by<eosio::name{"usersbycmm"},
                                               eosio::const_mem_fun<cambiatus::network, uint64_t, &cambiatus::network::users_by_cmm> > >
      networks;

  typedef eosio::multi_index<eosio::name{"objective"},
                             cambiatus::objective,
                             eosio::indexed_by<eosio::name{"bycmm"},
                                               eosio::const_mem_fun<cambiatus::objective, uint64_t, &cambiatus::objective::by_cmm> > >
      objectives;

  typedef eosio::multi_index<eosio::name{"action"},
                             cambiatus::action,
                             eosio::indexed_by<eosio::name{"byobj"},
                                               eosio::const_mem_fun<cambiatus::action, uint64_t, &cambiatus::action::by_objective> > >
      actions;

  typedef eosio::multi_index<eosio::name{"validator"},
                             cambiatus::action_validator,
                             eosio::indexed_by<eosio::name{"byaction"},
                                               eosio::const_mem_fun<cambiatus::action_validator, uint64_t, &cambiatus::action_validator::by_action> > >
      validators;

  typedef eosio::multi_index<eosio::name{"claim"},
                             cambiatus::claim,
                             eosio::indexed_by<eosio::name{"byaction"}, eosio::const_mem_fun<cambiatus::claim, uint64_t, &cambiatus::claim::by_action> > >
      claims;

  typedef eosio::multi_index<eosio::name{"check"},
                             cambiatus::check,
                             eosio::indexed_by<eosio::name{"byclaim"},
                                               eosio::const_mem_fun<cambiatus::check, uint64_t, &cambiatus::check::by_claim> > >
      checks;

  typedef eosio::multi_index<eosio::name{"sale"},
                             cambiatus::sale,
                             eosio::indexed_by<eosio::name{"bycmm"},
                                               eosio::const_mem_fun<cambiatus::sale, uint64_t, &cambiatus::sale::by_cmm> >,
                             eosio::indexed_by<eosio::name{"byuser"},
                                               eosio::const_mem_fun<cambiatus::sale, uint64_t, &cambiatus::sale::by_user> > >
      sales;

  typedef eosio::multi_index<eosio::name{"role"}, cambiatus::role> roles;

  typedef eosio::singleton<eosio::name{"indexes"}, cambiatus::indexes> item_indexes;

  item_indexes curr_indexes;

  // Initialize our singleton table for indices
  cambiatus(eosio::name receiver, eosio::name code, eosio::datastream<const char *> ds) : contract(receiver, code, ds), curr_indexes(_self, _self.value) {}
};

const auto currency_account = eosio::name{TOSTR(__TOKEN_ACCOUNT__)};
const auto backend_account = eosio::name{TOSTR(__BACKEND_ACCOUNT__)};
const uint32_t proof_expiration_secs = __PROOF_EXPIRATION_SECS__;

// Add global reference for a table from the token contract
struct currency_stats
{
  eosio::asset supply;
  eosio::asset max_supply;
  eosio::asset min_balance;
  eosio::name issuer;
  std::string type;

  uint64_t primary_key() const { return supply.symbol.code().raw(); }
};
typedef eosio::multi_index<eosio::name{"stat"}, currency_stats> cambiatus_tokens;
