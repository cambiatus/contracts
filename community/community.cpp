#include "community.hpp"
#include "../utils/utils.cpp"
#include <eosio/crypto.hpp>

inline void verify_sha256_prefix(const std::string &value, const std::string &compared_hash)
{
  auto hash = eosio::sha256(value.c_str(), value.length());
  auto arr = hash.extract_as_byte_array();

  const char *hex_characters = "0123456789abcdef";
  std::string hash_prefix;
  const uint8_t *d = reinterpret_cast<const uint8_t *>(arr.data());

  auto prefix_size = compared_hash.length() / 2;
  for (uint32_t i = 0; i < prefix_size; ++i)
  {
    hash_prefix += hex_characters[d[i] >> 4];
    hash_prefix += hex_characters[d[i] & 0x0f];
  }

  eosio::check(compared_hash == hash_prefix,
               "fail to verify hash: " + compared_hash + " should be " + hash_prefix);
}

void cambiatus::create(eosio::asset cmm_asset, eosio::name creator, std::string logo,
                       std::string name, std::string description,
                       eosio::asset inviter_reward, eosio::asset invited_reward,
                       std::uint8_t has_objectives, std::uint8_t has_shop, std::uint8_t has_kyc,
                       std::uint8_t auto_invite, std::string subdomain, std::string website)
{
  require_auth(creator);

  const eosio::symbol new_symbol = cmm_asset.symbol;

  // Validates reward for invitater and invited users
  eosio::check(invited_reward.is_valid(), "invalid invited_reward");
  eosio::check(invited_reward.amount >= 0, "invited_reward must be equal or greater than 0");
  eosio::check(new_symbol == invited_reward.symbol, "unmatched symbols for max_supply and invited_reward");
  eosio::check(inviter_reward.is_valid(), "invalid inviter_reward");
  eosio::check(inviter_reward.amount >= 0, "inviter_reward must be equal or greater than 0");
  eosio::check(new_symbol == inviter_reward.symbol, "unmatched symbols for max_supply and inviter_reward");

  // Validates string fields
  eosio::check(name.size() <= 256, "name has more than 256 bytes");
  eosio::check(logo.size() <= 256, "logo has more than 256 bytes");

  // Check if community was created before
  communities community(_self, _self.value);
  auto existing_cmm = community.find(new_symbol.raw());
  eosio::check(existing_cmm == community.end(), "symbol already exists");

  // creates new community
  community.emplace(_self, [&](auto &c)
                    {
      c.symbol = new_symbol;
      c.creator = creator;
      c.logo = logo;
      c.name = name;
      c.description = description.substr(0, 255);
      c.inviter_reward = inviter_reward;
      c.invited_reward = invited_reward;
      c.has_objectives = has_objectives;
      c.has_shop = has_shop;
      c.has_kyc = has_kyc; });

  std::string user_type = "natural";
  eosio::action netlink = eosio::action(eosio::permission_level{get_self(), eosio::name{"active"}}, // Permission
                                        get_self(),                                                 // Account
                                        eosio::name{"netlink"},                                     // Action
                                        std::make_tuple(new_symbol, creator, creator, user_type));
  netlink.send();

  // Notify creator
  require_recipient(creator);

  // Create default member role
  std::vector<std::string> permissions{"invite", "claim", "order", "verify", "sell", "transfer"};
  roles role_table(_self, new_symbol.raw());
  role_table.emplace(_self, [&](auto &r)
                     { r.name = eosio::name{"member"}; r.permissions = permissions; });
}

void cambiatus::update(eosio::asset cmm_asset, std::string logo, std::string name,
                       std::string description, eosio::asset inviter_reward, eosio::asset invited_reward,
                       std::uint8_t has_objectives, std::uint8_t has_shop, std::uint8_t has_kyc,
                       std::uint8_t auto_invite, std::string subdomain, std::string website)
{
  communities community(_self, _self.value);
  const auto &cmm = community.get(cmm_asset.symbol.raw(), "can't find any community with given asset");

  require_auth(cmm.creator);

  // Validates string fields
  eosio::check(logo.size() <= 256, "logo has more than 256 bytes");
  eosio::check(name.size() <= 256, "name has more than 256 bytes");

  community.modify(cmm, _self, [&](auto &row)
                   {
      row.logo = logo;
      row.name = name;
      row.description = description.substr(0, 255);
      row.inviter_reward = inviter_reward;
      row.invited_reward = invited_reward;
      row.has_objectives = has_objectives;
      row.has_shop = has_shop;
      row.has_kyc = has_kyc; });
}

void cambiatus::netlink(eosio::symbol community_id, eosio::name inviter, eosio::name new_user, std::string user_type)
{
  eosio::check(is_account(new_user), "new user account doesn't exists");

  // This action can be performed by:
  // 1. The contract itself
  // 2. The inviter, directly
  // 3. The backend account, on behalf of the inviter
  if (eosio::get_sender() == get_self())
  {
    require_auth(get_self());
  }
  else if (has_auth(inviter))
  {
    require_auth(inviter);
  }
  else
  {
    require_auth(backend_account);
  }

  // Validate user type
  eosio::check(user_type == "natural" || user_type == "juridical", "user type must be 'natural' or 'juridical'");

  // Validates community
  communities community(_self, _self.value);
  const auto &cmm = community.get(community_id.raw(), "can't find any community with given asset");

  // Skip if already member
  if (is_member(community_id, new_user))
  {
    return;
  }

  // Validates if inviter is a member, except if its the creator
  if (cmm.creator != inviter)
  {
    eosio::check(is_member(community_id, inviter), "inviter is not part of the community");
    eosio::check(has_permission(community_id, inviter, permission::invite), "this user cannot invite with its current roles");
  }

  members member(_self, community_id.raw());
  member.emplace(_self, [&](auto &r)
                 { r.name = new_user; r.inviter = inviter; r.user_type = user_type; r.roles = { eosio::name{"member"} }; });

  // Skip rewards if inviter and invited is the same, may happen during community creation
  if (inviter == new_user)
    return;

  // Send inviter reward
  if (cmm.inviter_reward.amount > 0)
  {
    std::string memo_inviter = "Thanks for helping " + cmm.name + " grow!";
    eosio::action inviter_reward = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                                 currency_account,                                                 // Account
                                                 eosio::name{"issue"},                                             // Action
                                                 // to, quantity, memo
                                                 std::make_tuple(inviter, cmm.inviter_reward, memo_inviter));
    inviter_reward.send();
    require_recipient(inviter);
  }

  // Send invited reward
  if (cmm.invited_reward.amount > 0)
  {
    std::string memo_invited = "Welcome to " + cmm.name + "!";
    eosio::action invited_reward = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                                 currency_account,                                                 // Account
                                                 eosio::name{"issue"},                                             // Action
                                                 // to, quantity, memo
                                                 std::make_tuple(new_user, cmm.invited_reward, memo_invited));
    invited_reward.send();
    require_recipient(new_user);
  }
  else
  {
    eosio::action init_account = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                               currency_account,                                                 // Account
                                               eosio::name{"initacc"},                                           // Action
                                               std::make_tuple(cmm.invited_reward.symbol, new_user, inviter));
    init_account.send();
    require_recipient(new_user);
  }
}

void cambiatus::upsertobjctv(eosio::symbol community_id, std::uint64_t objective_id, std::string description, eosio::name editor)
{
  require_auth(editor);

  eosio::check(community_id.is_valid(), "Invalid symbol name for community");

  // Find community
  communities community(_self, _self.value);
  const auto &cmm = community.get(community_id.raw(), "Can't find community with given community_id");

  eosio::check(cmm.has_objectives, "This community don't have objectives enabled.");

  // Check if editor belongs to the community
  eosio::check(is_member(cmm.symbol, editor), "Editor doesn't belong to the community");

  if (objective_id > 0)
  {
    // Find objective
    objectives objective(_self, community_id.raw());
    const auto &found_objective = objective.get(objective_id, "Can't find objective with given ID");

    // Validate Auth can be either the community creator or the objective creator
    eosio::check(found_objective.creator == editor || cmm.creator == editor, "You must be either the creator of the objective or the community creator to edit");

    objective.modify(found_objective, _self, [&](auto &row)
                     { row.description = description.substr(0, 255); });
  }
  else
  {
    // Insert new objective
    objectives objective(_self, community_id.raw());
    objective.emplace(_self, [&](auto &o)
                      {
        o.id = get_available_id("objectives");
        o.description = description.substr(0, 255);
        o.community = community_id;
        o.creator = editor; });
  }
}

void cambiatus::upsertaction(eosio::symbol community_id, std::uint64_t action_id, std::uint64_t objective_id,
                             std::string description, eosio::asset reward,
                             eosio::asset verifier_reward, std::uint64_t deadline,
                             std::uint64_t usages, std::uint64_t usages_left,
                             std::uint64_t verifications, std::string verification_type,
                             std::string validators_str, std::uint8_t is_completed,
                             eosio::name creator,
                             std::uint8_t has_proof_photo, std::uint8_t has_proof_code,
                             std::string photo_proof_instructions, std::string image)
{
  // Validate creator
  eosio::check(is_account(creator), "invalid account for creator");
  require_auth(creator);

  // Validates that the objective exists
  objectives objective(_self, community_id.raw());
  auto itr_obj = objective.find(objective_id);
  eosio::check(itr_obj != objective.end(), "Can't find objective with given objective_id");
  auto &obj = *itr_obj;

  // Validate community
  communities community(_self, _self.value);
  auto itr_cmm = community.find(obj.community.raw());
  eosio::check(itr_cmm != community.end(), "Can't find community with given objective_id");
  auto &cmm = *itr_cmm;

  eosio::check(cmm.has_objectives, "This community don't have objectives enabled.");

  // Creator must belong to the community
  eosio::check(is_member(cmm.symbol, creator), "Creator doesn't belong to the community");

  // Validate assets
  eosio::check(reward.is_valid(), "invalid reward");
  eosio::check(reward.amount >= 0, "reward must be greater than or equal to 0");
  eosio::check(reward.symbol == obj.community, "reward must be a community token");

  eosio::check(verifier_reward.is_valid(), "invalid verifier_reward");
  eosio::check(verifier_reward.amount >= 0, "verifier reward must be greater than or equal to 0");
  eosio::check(verifier_reward.symbol == obj.community, "verifier_reward must be a community token");

  // Validate deadline
  if (deadline > 0)
  {
    eosio::check(now() < deadline, "Deadline must be somewhere in the future");
  }

  // Validate usages
  if (usages > 0)
  {
    eosio::check(usages <= 1000, "You can have a maximum of 1000 uses");
  }

  // Validate verification type
  eosio::check(verification_type == "claimable" || verification_type == "automatic", "verification type must be either 'claimable' or 'automatic'");

  // Validate that if we have verifications, it need to be at least three and it must be odd
  if (verifications > 0)
  {
    eosio::check(verifications >= 3 && ((verifications & 1) != 0), "You need at least three validators and it must be an odd number");
  }

  // ========================================= End validation, start upsert

  // Find action
  actions action(_self, _self.value);
  auto itr_act = action.find(action_id);

  if (action_id == 0)
  {
    // Get last used action id and update table_index table
    action_id = get_available_id("actions");

    action.emplace(_self, [&](auto &a)
                   {
        a.id = action_id;
                     a.objective_id = objective_id;
                     a.description = description.substr(0, 255);
                     a.reward = reward;
                     a.verifier_reward = verifier_reward;
                     a.deadline = deadline;
                     a.usages = usages;
                     a.usages_left = usages;
                     a.verifications = verifications;
                     a.verification_type = verification_type;
                     a.is_completed = 0;
                     a.creator = creator;
                     a.has_proof_photo = has_proof_photo;
                     a.has_proof_code = has_proof_code;
                     a.photo_proof_instructions = photo_proof_instructions.substr(0, 255); });
  }
  else
  {
    action.modify(itr_act, _self, [&](auto &a)
                  {
                    a.description = description.substr(0, 255);
                    a.reward = reward;
                    a.verifier_reward = verifier_reward;
                    a.deadline = deadline;
                    a.usages = usages;
                    a.usages_left = usages_left;
                    a.verifications = verifications;
                    a.verification_type = verification_type;
                    a.is_completed = is_completed;
                    a.has_proof_photo = has_proof_photo;
                    a.has_proof_code = has_proof_code;
                    a.photo_proof_instructions = photo_proof_instructions.substr(0, 255); });
  }

  if (verification_type == "claimable")
  {
    // Validate list of validators
    std::vector<std::string> strs = split(validators_str, "-");
    eosio::check(strs.size() >= verifications, "You cannot have a bigger number of verifications than accounts in the validator list");

    // Ensure list of validators in unique
    sort(strs.begin(), strs.end());
    auto strs_it = std::unique(strs.begin(), strs.end());
    eosio::check(strs_it == strs.end(), "You cannot add a validator more than once to an action");

    // Make sure we have at least 2 verifiers
    eosio::check(strs.size() >= 2, "You need at least two verifiers in a claimable action");

    // Define validators table, scoped by action
    validators validator(_self, action_id);

    // Clean up existing validators if action already exists

    // for (validator;itr_vals != validator.end();) {
    for (auto itr_vals = validator.begin(); itr_vals != validator.end();)
    {
      eosio::print_f("Test Table : {%, %}\n", itr_vals->action_id);
      itr_vals = validator.erase(itr_vals);
    }

    std::vector<std::string> validator_v = split(validators_str, "-");
    for (auto i : validator_v)
    {
      eosio::name acc = eosio::name{i};
      eosio::check((bool)acc, "account from validator list cannot be empty");
      eosio::check(is_account(acc), "account from validator list don't exist");

      // Must belong to the community
      eosio::check(is_member(cmm.symbol, acc), "one of the validators doesn't belong to the community");

      // Add list of validators
      validator.emplace(_self, [&](auto &v)
                        {
                          v.id = validator.available_primary_key();
                          v.action_id = action_id;
                          v.validator = acc; });
    };
  }
}

/// @abi action
/// Verify an automatic action, rewarding tokens
void cambiatus::reward(eosio::symbol community_id, std::uint64_t action_id, eosio::name receiver, eosio::name awarder)
{
  // Validates awarder
  eosio::check(is_account(awarder), "invalid account for awarder");
  eosio::check(is_account(receiver), "invalid account for receiver");
  require_auth(awarder);

  // Validates if action exists
  actions action_table(_self, _self.value);
  const auto &action = action_table.get(action_id, "can't find action with given action_id");

  // Validates action and objective are from the community
  objectives objective_table(_self, community_id.raw());
  const auto &obj = objective_table.get(action.objective_id, "can't find objective with given action_id");

  communities community(_self, _self.value);
  const auto &cmm = community.get(obj.community.raw(), "can't find community with given action_id");

  eosio::check(cmm.has_objectives, "this community don't have objectives enabled.");

  // Validates if receiver and awarder belong to the action community
  eosio::check(is_member(cmm.symbol, awarder), "verifier doesn't belong to the community");
  eosio::check(is_member(cmm.symbol, receiver), "receiver doesn't belong to the community");

  // Validate if the action type is `automatic`
  eosio::check(action.verification_type == "automatic", "can't verify actions that aren't automatic, you'll need to open a claim");

  eosio::check(action.is_completed == false, "this action is already completed");

  // Check if user has permission to reward
  eosio::check(has_permission(cmm.symbol, awarder, permission::award), "you cannot award with your current roles");

  if (action.usages > 0)
  {
    eosio::check(action.usages_left >= 1, "there are no usages left for this action");
  }

  // change status of verification
  action_table.modify(action, _self, [&](auto &a)
                      {
                        a.usages_left = a.usages_left - 1;

                        if (a.usages_left - 1 <= 0)
                        {
                          a.is_completed = 1;
                        } });

  // Find Token
  // cambiatus_tokens tokens(currency_account, currency_account.value);
  cambiatus_tokens tokens(currency_account, cmm.symbol.code().raw());
  const auto &token = tokens.get(cmm.symbol.code().raw(), "can't find token configurations on cambiatus token contract");

  if (action.reward.amount > 0)
  {
    // Reward Action Claimer
    std::string memo_action = "thanks for doing an action for your community";
    eosio::action reward_action = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                                currency_account,                                                 // Account
                                                eosio::name{"issue"},                                             // Action
                                                // to, quantity, memo
                                                std::make_tuple(receiver, action.reward, memo_action));
    reward_action.send();
  }

  // Don't reward awarder for automatic verifications
}

/// @abi action
/// Start a new claim on an action
void cambiatus::claimaction(eosio::symbol community_id, std::uint64_t action_id, eosio::name maker,
                            std::string proof_photo, std::string proof_code, uint32_t proof_time)
{
  // Validate maker
  eosio::check(is_account(maker), "invalid account for maker");
  require_auth(maker);

  eosio::check(proof_photo.length() <= 256,
               "Invalid length for proof photo url, must be less than 256 characters");

  if (!proof_code.empty())
  {
    eosio::check(proof_code.length() == 8, "proof code needs to be 8 chars");
    eosio::check(now() - proof_time <= proof_expiration_secs, "proof time has expired");
    std::string proof = std::to_string(action_id) + std::to_string(maker.value) + std::to_string(proof_time);
    verify_sha256_prefix(proof, proof_code);
  }

  // Validates if action exists
  actions action_table(_self, _self.value);
  const auto &objact = action_table.get(action_id, "Can't find action with given action_id");

  // Check if action is completed, have usages left or the deadline has been met
  eosio::check(objact.is_completed == false, "This is action is already completed, can't open claim");
  if (objact.deadline > 0)
  {
    eosio::check(objact.deadline > now(), "Deadline exceeded");
  }

  if (objact.usages > 0)
  {
    eosio::check(objact.usages_left >= 1, "There are no usages left for this action");
  }

  // Check if the action is claimable
  eosio::check(objact.verification_type == "claimable", "You can only open claims in claimable actions");

  // Check action proofs
  if (objact.has_proof_photo)
  {
    eosio::check(!proof_photo.empty(), "action requires proof photo");
  }
  if (objact.has_proof_code)
  {
    eosio::check(!proof_code.empty() && proof_time > 0, "action requires proof code");
  }

  // Validates maker belongs to the action community
  objectives objective(_self, community_id.raw());
  const auto &obj = objective.get(objact.objective_id, "Can't find objective with given action_id");

  communities community(_self, _self.value);
  const auto &cmm = community.get(community_id.raw(), "Can't find community with given action_id");

  eosio::check(cmm.has_objectives, "This community don't have objectives enabled.");

  eosio::check(is_member(cmm.symbol, maker), "Maker doesn't belong to the community");
  eosio::check(has_permission(community_id, maker, permission::claim), "you cannot claim with your current roles");

  // Get last used claim id and update item_index table
  uint64_t claim_id;
  claim_id = get_available_id("claims");

  // Emplace new claim
  // claimsnew claim_table(_self, _self.value);
  claims claim_table(_self, _self.value);
  claim_table.emplace(_self, [&](auto &c)
                      {
                        c.id = claim_id;
                        c.action_id = action_id;
                        c.claimer = maker;
                        c.status = "pending";
                        c.proof_photo = proof_photo;
                        c.proof_code = proof_code; });
}

/// @abi action
/// Send a vote to a given claim
void cambiatus::verifyclaim(eosio::symbol community_id, std::uint64_t claim_id, eosio::name verifier, std::uint8_t vote)
{
  // Validates verifier belongs to the action community
  claims claim_table(_self, _self.value);
  // claimsnew claim_table(_self, _self.value);
  auto itr_clm = claim_table.find(claim_id);
  eosio::check(itr_clm != claim_table.end(), "Can't find claim with given claim_id");
  auto &claim = *itr_clm;

  // Check if claim is already verified
  eosio::check(claim.status == "pending", "Can't vote on already verified claim");

  // Validates if action exists
  actions action(_self, _self.value);
  auto itr_objact = action.find(claim.action_id);
  eosio::check(itr_objact != action.end(), "Can't find action with given claim_id");
  auto &objact = *itr_objact;

  // Validates that the objective exists
  objectives objective(_self, community_id.raw());
  auto itr_obj = objective.find(objact.objective_id);
  eosio::check(itr_obj != objective.end(), "Can't find objective with given claim_id");
  auto &obj = *itr_obj;

  // Validate community
  communities community(_self, _self.value);
  auto itr_cmm = community.find(obj.community.raw());
  eosio::check(itr_cmm != community.end(), "Can't find community with given claim_id");
  auto &cmm = *itr_cmm;

  eosio::check(cmm.has_objectives, "This community don't have objectives enabled.");

  // Check if user belongs to the action_validator list
  validators validator(_self, objact.id);
  std::uint64_t validator_count = 0;
  for (auto itr_validators = validator.begin(); itr_validators != validator.end();)
  {
    if ((*itr_validators).validator == verifier)
    {
      validator_count++;
    }
    itr_validators++;
  }
  eosio::check(validator_count > 0, "Verifier is not in the action validator list");

  eosio::check(has_permission(cmm.symbol, verifier, permission::verify), "you cannot verify with your current roles");

  // Check if action is completed, have usages left or the deadline has been met
  eosio::check(objact.is_completed == false, "This is action is already completed, can't verify claim");

  if (objact.deadline > 0)
  {
    eosio::check(objact.deadline > now(), "Deadline exceeded");
  }

  if (objact.usages > 0)
  {
    eosio::check(objact.usages_left >= 1, "There are no usages left for this action");
  }

  // Get check index
  checks check(_self, _self.value);
  auto check_by_claim = check.get_index<eosio::name{"byclaim"}>();

  // Assert that verifier hasn't voted previously
  uint64_t checks_count = 0;
  for (auto itr_check_claim = check_by_claim.find(claim_id); itr_check_claim != check_by_claim.end(); itr_check_claim++)
  {
    auto check_claim = *itr_check_claim;
    bool existing_vote = check_claim.validator == verifier && check_claim.claim_id == claim_id;
    eosio::check(!existing_vote, "The verifier cannot check the same claim more than once");
  }

  // Add new check
  check.emplace(_self, [&](auto &c)
                {
                  c.id = check.available_primary_key();
                  c.claim_id = claim.id;
                  c.validator = verifier;
                  c.is_verified = vote; });

  if (objact.verifier_reward.amount > 0)
  {
    // Send verification reward
    std::string memo_verification = "Thanks for verifying an action for your community";
    eosio::action verification_reward = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                                      currency_account,                                                 // Account
                                                      eosio::name{"issue"},                                             // Action
                                                      // to, quantity, memo
                                                      std::make_tuple(verifier, objact.verifier_reward, memo_verification));
    verification_reward.send();
  }

  // In order to know if its approved or rejected, we will have to count all existing checks, to see if we already have all needed
  // Just check `objact.verifications <= check counter`

  // Then we will have to count the positive and negative votes...
  // If more than half was positive, its approved
  // else its rejected

  // If we don't have yet the number of votes necessary, then its pending

  // At every vote we will have to update the claim status
  std::uint64_t positive_votes = 0;
  std::uint64_t negative_votes = 0;

  auto checks_with_claim = check.get_index<eosio::name{"byclaim"}>();
  for (auto itr_vote = checks_with_claim.find(claim_id); itr_vote != checks_with_claim.end(); itr_vote++)
  {
    if (itr_vote->claim_id != claim_id)
      continue;

    if (itr_vote->is_verified == 1)
    {
      positive_votes++;
      eosio::print("\nFound a positive vote from: ", (*itr_vote).validator);
    }
    else
    {
      negative_votes++;
      eosio::print("\nFound a negative vote from: ", (*itr_vote).validator);
    }
  }

  eosio::print("\nPositive votes: ", positive_votes);
  eosio::print("\nNegative votes: ", negative_votes);

  std::uint64_t majority = (objact.verifications >> 1) + (objact.verifications & 1);

  eosio::print("\nMajority: ", majority);

  std::string status = "pending";
  if (positive_votes >= majority || negative_votes >= majority)
  {
    if (positive_votes > negative_votes)
    {
      status = "approved";
    }
    else
    {
      status = "rejected";
    }
  }

  eosio::print("\nFinal status of the claim is: ", status);
  claim_table.modify(itr_clm, _self, [&](auto &c)
                     { c.status = status; });

  if (status == "approved" && objact.reward.amount > 0)
  {
    // Send reward
    std::string memo_action = "Thanks for doing an action for your community";
    eosio::action reward_action = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                                currency_account,                                                 // Account
                                                eosio::name{"issue"},                                             // Action
                                                // to, quantity, memo
                                                std::make_tuple(claim.claimer, objact.reward, memo_action));
    reward_action.send();
  }

  // Check if action can be completed. Current claim must be either "approved" or "rejected"
  if (status != "pending" && !objact.is_completed && objact.usages > 0)
  {
    action.modify(itr_objact, _self, [&](auto &a)
                  {
                    a.usages_left = objact.usages_left - 1;
                    a.is_completed = (objact.usages_left - 1) == 0 ? 1 : 0; });
  }
}

void cambiatus::transfersale(std::uint64_t sale_id, eosio::name from, eosio::name to, eosio::asset quantity, std::uint64_t units)
{
  // Validate user
  require_auth(from);

  // Validate 'to' account
  eosio::check(is_account(to), "The sale creator (to) account doesn't exists");

  // Validate accounts are different
  eosio::check(from != to, "Can't sale for yourself");

  // Check if community exists
  communities community(_self, _self.value);
  const auto &cmm = community.get(quantity.symbol.raw(), "Can't find community with given Symbol");

  eosio::check(cmm.has_shop, "This community don't have shop enabled.");

  eosio::check(has_permission(quantity.symbol, from, permission::order), "you cannot create an order with your current roles");
  eosio::check(has_permission(quantity.symbol, to, permission::sell), "you cannot buy from this user, it doesn't have the permission to sell in this community anymore.");

  // Validate 'from' user belongs to sale community
  eosio::check(is_member(quantity.symbol, from), "You can't use transfersale to this sale if you aren't part of the community");
}

void cambiatus::upsertrole(eosio::symbol community_id, eosio::name name, std::string color, std::vector<std::string> &permissions)
{
  eosio::check(community_id.is_valid(), "provided symbol is not valid");

  // Find community
  communities community_table(_self, _self.value);
  const auto &community = community_table.get(community_id.raw(), "can't find community with given community_id");

  // Make sure we have admin's permission to upsert roles **or** the contract permission
  // Roles are automatically created during community creation process
  if (eosio::get_sender() == community.creator)
  {
    require_auth(community.creator);
  }
  else
  {
    require_auth(get_self());
  }

  // Validate permission list
  eosio::check(permissions.size() <= 6, "invalid cambiatus permissions");
  for (auto p : permissions)
  {
    eosio::check(p == "invite" || p == "claim" ||
                     p == "order" || p == "verify" ||
                     p == "sell" || p == "award" ||
                     p == "transfer",
                 "Invalid permission. Check permission list sent");
  }

  // Validate color
  eosio::check(color.length() == 7, "invalid color");
  eosio::check(color.front() == '#', "invalid color");

  // Upserts
  roles role_table(_self, community_id.raw());
  auto existing_role = role_table.find(name.value);

  if (existing_role == role_table.end())
  {
    role_table.emplace(_self, [&](auto &r)
                       {
                         r.name = name;
                         r.permissions = permissions; });
  }
  else
  {
    role_table.modify(existing_role, _self, [&](auto &r)
                      { r.permissions = permissions; });
  }
}

void cambiatus::assignroles(eosio::symbol community_id, eosio::name member, std::vector<eosio::name> &new_roles)
{
  eosio::check(community_id.is_valid(), "provided symbol is not valid");

  // Find community
  communities community_table(_self, _self.value);
  const auto &community = community_table.get(community_id.raw(), "can't find community with given community_id");

  // Make sure we have admin's permission to upsert roles
  require_auth(community.creator);

  // Check if all roles exist
  roles role_table(_self, community_id.raw());
  for (auto role : new_roles)
  {
    role_table.get(role.value, "this role doesn't exist");
  }

  eosio::check(is_member(community_id, member), "user don't belong to the community");

  // Update member roles
  members member_table(_self, community_id.raw());
  auto const &found_member = member_table.get(member.value, "user don't belong to the community");
  member_table.modify(found_member, _self, [&](auto &m)
                      { m.roles = new_roles; });
}

// set chain indices
void cambiatus::setindices(std::uint64_t sale_id, std::uint64_t objective_id, std::uint64_t action_id, std::uint64_t claim_id)
{
  require_auth(_self);
  indexes default_indexes;
  auto current_indexes = curr_indexes.get_or_create(_self, default_indexes);

  current_indexes.last_used_sale_id = sale_id;
  current_indexes.last_used_objective_id = objective_id;
  current_indexes.last_used_action_id = action_id;
  current_indexes.last_used_claim_id = claim_id;

  curr_indexes.set(current_indexes, _self);
}

void cambiatus::deleteobj(std::uint64_t id)
{
  require_auth(_self);

  objectives objective(_self, _self.value);
  auto x = objective.find(id);
  eosio::check(x != objective.end(), "Cant find objective with given di");
  objective.erase(x);
}

void cambiatus::deleteact(std::uint64_t id)
{
  require_auth(_self);

  actions action(_self, _self.value);
  auto found_action = action.find(id);
  eosio::check(found_action != action.end(), "Cant find action with given id");
  action.erase(found_action);
}

void cambiatus::clean(std::string t, eosio::name name_scope, eosio::symbol symbol_scope)
{
  // Clean up the old claims table after the migration
  require_auth(_self);

  eosio::check(t == "claim" ||
                   t == "community" ||
                   t == "network" ||
                   t == "member" ||
                   t == "objective" ||
                   t == "action" ||
                   t == "role" ||
                   t == "sale",
               "invalid value for table name");

  if (t == "claim")
  {
    claims claim_table(_self, _self.value);
    for (auto itr = claim_table.begin(); itr != claim_table.end();)
    {
      itr = claim_table.erase(itr);
    }
  }

  if (t == "community")
  {
    communities communities_table(_self, _self.value);
    for (auto itr = communities_table.begin(); itr != communities_table.end();)
    {
      itr = communities_table.erase(itr);
    }
  }

  if (t == "network")
  {
    networks network_table(_self, _self.value);
    for (auto itr = network_table.begin(); itr != network_table.end();)
    {
      itr = network_table.erase(itr);
    }
  }

  if (t == "member")
  {
    members member_table(_self, symbol_scope.raw());
    for (auto itr = member_table.begin(); itr != member_table.end();)
    {
      itr = member_table.erase(itr);
    }
  }

  if (t == "action")
  {
    actions action_table(_self, _self.value);
    for (auto itr = action_table.begin(); itr != action_table.end();)
    {
      itr = action_table.erase(itr);
    }
  }

  if (t == "objective")
  {
    objectives objective_table(_self, symbol_scope.raw());
    for (auto itr = objective_table.begin(); itr != objective_table.end();)
    {
      itr = objective_table.erase(itr);
    }
  }

  if (t == "role")
  {
    roles role_table(_self, symbol_scope.raw());
    for (auto itr = role_table.begin(); itr != role_table.end();)
    {
      itr = role_table.erase(itr);
    }
  }
}

// Get available key
uint64_t cambiatus::get_available_id(std::string table)
{
  eosio::check(table == "actions" || table == "objectives" || table == "sales" || table == "claims", "Table index not available");

  // Init indexes table
  indexes default_indexes;
  auto current_indexes = curr_indexes.get_or_create(_self, default_indexes);

  uint64_t id = 1;

  if (table == "actions")
  {
    id = current_indexes.last_used_action_id + 1;
    current_indexes.last_used_action_id = id;
    curr_indexes.set(current_indexes, _self);
  }
  else if (table == "objectives")
  {
    id = current_indexes.last_used_objective_id + 1;
    current_indexes.last_used_objective_id = id;
    curr_indexes.set(current_indexes, _self);
  }
  else if (table == "sales")
  {
    id = current_indexes.last_used_sale_id + 1;
    current_indexes.last_used_sale_id = id;
    curr_indexes.set(current_indexes, _self);
  }
  else if (table == "claims")
  {
    id = current_indexes.last_used_claim_id + 1;
    current_indexes.last_used_claim_id = id;
    curr_indexes.set(current_indexes, _self);
  }

  return id;
}

bool cambiatus::is_member(eosio::symbol community_id, eosio::name user)
{
  members member_table(_self, community_id.raw());
  auto itr = member_table.find(user.value);
  return itr != member_table.end();
}

bool cambiatus::has_permission(eosio::symbol community_id, eosio::name user, permission e_permission)
{
  std::string permission = cambiatus::permission_to_string(e_permission);

  members member_table(_self, community_id.raw());
  const auto &member = member_table.get(user.value, "user is not part of the communtiy");

  roles role_table(_self, community_id.raw());
  for (auto &&member_role : member.roles)
  {
    const auto &role = role_table.get(member_role.value, "user has a role that doesn't exist!");

    bool any = std::any_of(role.permissions.begin(), role.permissions.end(), [&](const std::string &elem)
                           { return elem == permission; });
    if (any)
    {
      return true;
    }
  }

  return false;
}

std::string cambiatus::permission_to_string(permission e_permission)
{
  switch (e_permission)
  {
  case permission::invite:
    return "invite";
  case permission::claim:
    return "claim";
  case permission::order:
    return "order";
  case permission::verify:
    return "verify";
  case permission::sell:
    return "sell";
  case permission::award:
    return "award";
  case permission::transfer:
    return "transfer";
  }
}

EOSIO_DISPATCH(cambiatus,
               (create)(update)(netlink)                   // Basic community
               (upsertrole)(assignroles)                   // Roles & Permission
               (upsertobjctv)(upsertaction)                // Objectives and Actions
               (reward)(claimaction)(verifyclaim)          // Verifications and Claims
               (transfersale)                              // Shop
               (setindices)(deleteobj)(deleteact)(clean)); // Admin actions
