#include "bespiral.community.hpp"
#include "../utils/utils.cpp"

void bespiral::create(eosio::asset cmm_asset, eosio::name creator, std::string logo,
                      std::string name, std::string description,
                      eosio::asset inviter_reward, eosio::asset invited_reward) {
  require_auth(creator);

  const eosio::symbol new_symbol = cmm_asset.symbol;

  // Validates reward for invitater and invited users
  eosio_assert(invited_reward.is_valid(), "invalid invited_reward");
  eosio_assert(invited_reward.amount >= 0, "invited_reward must be equal or greater than 0");
  eosio_assert(new_symbol == invited_reward.symbol, "unmatched symbols for max_supply and invited_reward");
  eosio_assert(inviter_reward.is_valid(), "invalid inviter_reward");
  eosio_assert(inviter_reward.amount >= 0, "inviter_reward must be equal or greater than 0");
  eosio_assert(new_symbol == inviter_reward.symbol, "unmatched symbols for max_supply and inviter_reward");

  // Validates string fields
  eosio_assert(name.size() <= 256, "name has more than 256 bytes");
  eosio_assert(description.size() <= 256, "description has more than 256 bytes");
  eosio_assert(logo.size() <= 256, "logo has more than 256 bytes");

  // Check if community was created before
  communities community(_self, _self.value);
  auto existing_cmm = community.find(new_symbol.raw());
  eosio_assert(existing_cmm == community.end(), "symbol already exists");

  // creates new community
  community.emplace(_self, [&](auto &r) {
    r.symbol = new_symbol;

    r.creator = creator;
    r.logo = logo;
    r.name = name;
    r.description = description;
    r.inviter_reward = inviter_reward;
    r.invited_reward = invited_reward;
  });

  SEND_INLINE_ACTION(*this,                            // Account
                     netlink,                          // Action
                     {creator, eosio::name{"active"}}, // Permission
                     // cmm_asset, new_user, inviter
                     {cmm_asset, creator, creator});

  // Notify creator
  require_recipient(creator);
}

void bespiral::update(eosio::asset cmm_asset, std::string logo, std::string name,
                      std::string description, eosio::asset inviter_reward, eosio::asset invited_reward) {
  communities community(_self, _self.value);
  const auto &cmm = community.get(cmm_asset.symbol.raw(), "can't find any community with given asset");

  require_auth(cmm.creator);

  // Validates string fields
  eosio_assert(logo.size() <= 256, "logo has more than 256 bytes");
  eosio_assert(name.size() <= 256, "name has more than 256 bytes");
  eosio_assert(description.size() <= 256, "description has more than 256 bytes");

  community.modify(cmm, _self, [&](auto &row) {
    row.logo = logo;
    row.name = name;
    row.description = description;
    row.inviter_reward = inviter_reward;
    row.invited_reward = invited_reward;
  });
}

void bespiral::netlink(eosio::asset cmm_asset, eosio::name inviter, eosio::name new_user) {
  eosio_assert(is_account(new_user), "new user account doesn't exists");

  // Check for inviter auth, otherwise check for backend's auth
  if (has_auth(inviter)) {
    require_auth(inviter);
  } else {
    // ATTENTION: `bespiral` account is configured on the backend and hardcoded here. You may have trouble with this
    require_auth(eosio::name{"bespiral"});
  }

  // Validates community
  eosio::symbol cmm_symbol = cmm_asset.symbol;
  communities community(_self, _self.value);
  const auto &cmm = community.get(cmm_symbol.raw(), "can't find any community with given asset");

  // Validates existent link
  auto id = gen_uuid(cmm_symbol.raw(), new_user.value);
  networks network(_self, _self.value);
  auto existing_netlink = network.find(id);
  if (existing_netlink != network.end())
    return; // Skip if user already in the network

  // Validates inviter if not the creator
  if (cmm.creator != inviter) {
    auto inviter_id = gen_uuid(cmm.symbol.raw(), inviter.value);
    auto itr_inviter = network.find(inviter_id);
    eosio_assert(itr_inviter != network.end(), "unknown inviter");
  }

  network.emplace(_self, [&](auto &r) {
    r.id = id;
    r.community = cmm_symbol;
    r.invited_user = new_user;
    r.invited_by = inviter;
  });

  // Notify user
  require_recipient(new_user);

  // Skip rewards if inviter and invited is the same, may happen during community creation
  if (inviter == new_user)
    return;

  // Send inviter reward
  if (cmm.inviter_reward.amount > 0) {
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
  if (cmm.invited_reward.amount > 0) {
    std::string memo_invited = "Welcome to " + cmm.name + "!";
    eosio::action invited_reward = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                                 currency_account,                                                 // Account
                                                 eosio::name{"issue"},                                             // Action
                                                 // to, quantity, memo
                                                 std::make_tuple(new_user, cmm.invited_reward, memo_invited));
    invited_reward.send();
    require_recipient(new_user);
  } else {
    eosio::action init_account = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                               currency_account,                                                 // Account
                                               eosio::name{"initacc"},                                           // Action
                                               std::make_tuple(cmm.invited_reward.symbol, new_user));
    init_account.send();
  }
}

void bespiral::newobjective(eosio::asset cmm_asset, std::string description, eosio::name creator) {
  require_auth(creator);

  eosio::symbol community_symbol = cmm_asset.symbol;
  eosio_assert(community_symbol.is_valid(), "Invalid symbol name for community");
  eosio_assert(description.length() <= 256, "Invalid length for description, must be less than 256 characters");

  // Check if community exists
  communities community(_self, _self.value);
  const auto &cmm = community.get(community_symbol.raw(), "Can't find community with given community_id");

  // Check if creator belongs to the community
  networks network(_self, _self.value);
  auto creator_id = gen_uuid(cmm.symbol.raw(), creator.value);
  auto itr_creator = network.find(creator_id);
  eosio_assert(itr_creator != network.end(), "Creator doesn't belong to the community");

  // Insert new objective
  objectives objective(_self, _self.value);
  objective.emplace(_self, [&](auto &o) {
                             o.id = get_available_id("objectives");
                             o.description = description;
                             o.community = community_symbol;
                             o.creator = creator;
                           });
}

void bespiral::updobjective(std::uint64_t objective_id, std::string description, eosio::name editor) {
  require_auth(editor);

  eosio_assert(description.length() <= 256, "Invalid length for description, must be less than 256 characters");

  // Find objective
  objectives objective(_self, _self.value);
  const auto &found_objective = objective.get(objective_id, "Can't find objective with given ID");

  // Find community
  communities community(_self, _self.value);
  const auto &cmm = community.get(found_objective.community.raw(), "Can't find community with given community_id");

  // Check if editor belongs to the community
  networks network(_self, _self.value);
  auto editor_id = gen_uuid(found_objective.community.raw(), editor.value);
  auto itr_editor = network.find(editor_id);
  eosio_assert(itr_editor != network.end(), "Editor doesn't belong to the community");

  // Validate Auth can be either the community creator or the objective creator
  eosio_assert(found_objective.creator == editor || cmm.creator == editor, "You must be either the creator of the objective or the community creator to edit");

  objective.modify(found_objective, _self, [&](auto &row) {
                                             row.description = description;
                                           });
}

void bespiral::upsertaction(std::uint64_t action_id, std::uint64_t objective_id,
                            std::string description, eosio::asset reward,
                            eosio::asset verifier_reward, std::uint64_t deadline,
                            std::uint64_t usages, std::uint64_t usages_left,
                            std::uint64_t verifications, std::string verification_type,
                            std::string validators_str, std::uint8_t is_completed,
                            eosio::name creator) {
  // Validate creator
  eosio_assert(is_account(creator), "invalid account for creator");
  require_auth(creator);

  // Validates that the objective exists
  objectives objective(_self, _self.value);
  auto itr_obj = objective.find(objective_id);
  eosio_assert(itr_obj != objective.end(), "Can't find objective with given objective_id");
  auto &obj = *itr_obj;

  // Validate community
  communities community(_self, _self.value);
  auto itr_cmm = community.find(obj.community.raw());
  eosio_assert(itr_cmm != community.end(), "Can't find community with given objective_id");
  auto &cmm = *itr_cmm;

  // Creator must belong to the community
  networks network(_self, _self.value);
  auto creator_id = gen_uuid(cmm.symbol.raw(), creator.value);
  auto itr_creator = network.find(creator_id);
  eosio_assert(itr_creator != network.end(), "Creator doesn't belong to the community");

  // Validate assets
  eosio_assert(reward.is_valid(), "invalid reward");
  eosio_assert(reward.amount >= 0, "reward must be greater than or equal to 0");
  eosio_assert(reward.symbol == obj.community, "reward must be a community token");

  eosio_assert(verifier_reward.is_valid(), "invalid verifier_reward");
  eosio_assert(verifier_reward.amount >= 0, "verifier reward must be greater than or equal to 0");
  eosio_assert(verifier_reward.symbol == obj.community, "verifier_reward must be a community token");

  // Validate description
  eosio_assert(description.length() <= 256, "Invalid length for description, must be less or equal than 256 chars");

  // Validate deadline
  if (deadline > 0) {
    eosio_assert(now() < deadline, "Deadline must be somewhere in the future");
  }

  // Validate usages
  if (usages > 0) {
    eosio_assert(usages <= 1000, "You can have a maximum of 1000 uses");
  }

  // Validate verification type
  eosio_assert(verification_type == "claimable" || verification_type == "automatic", "verification type must be either 'claimable' or 'automatic'");

  // Validate that if we have verifications, it need to be at least three and it must be odd
  if (verifications > 0) {
    eosio_assert(verifications >= 3 && ((verifications & 1) != 0), "You need at least three validators and it must be an odd number");
  }

  // ========================================= End validation, start upsert

  // Find action
  actions action(_self, _self.value);
  auto itr_act = action.find(action_id);

  if (action_id == 0) {
    // Get last used action id and update table_index table
    action_id = get_available_id("actions");

    action.emplace(_self, [&](auto &a) {
                            a.id = action_id;
                            a.objective_id = objective_id;
                            a.description = description;
                            a.reward = reward;
                            a.verifier_reward = verifier_reward;
                            a.deadline = deadline;
                            a.usages = usages;
                            a.usages_left = usages;
                            a.verifications = verifications;
                            a.verification_type = verification_type;
                            a.is_completed = 0;
                            a.creator = creator;
                          });
  } else {
    action.modify(itr_act, _self, [&](auto& a) {
                                    a.description = description;
                                    a.reward = reward;
                                    a.verifier_reward = verifier_reward;
                                    a.deadline = deadline;
                                    a.usages = usages;
                                    a.usages_left = usages_left;
                                    a.verifications = verifications;
                                    a.verification_type = verification_type;
                                    a.is_completed = is_completed;
                                  });
  }

  if (verification_type == "claimable") {
    // Validate list of validators
    std::vector<std::string> strs = split(validators_str, "-");
    eosio_assert(strs.size() >= verifications, "You cannot have a bigger number of verifications than accounts in the validator list");

    // Ensure list of validators in unique
    sort(strs.begin(), strs.end());
    auto strs_it = std::unique(strs.begin(), strs.end());
    eosio_assert(strs_it == strs.end(), "You cannot add a validator more than once to an action");

    // Make sure we have at least 2 verifiers
    eosio_assert(strs.size() >= 2, "You need at least two verifiers in a claimable action");

    // Define validators table, scoped by action
    validators validator(_self, action_id);

    // Clean up existing validators if action already exists

    // for (validator;itr_vals != validator.end();) {
    for (auto itr_vals = validator.begin();itr_vals != validator.end();) {
      eosio::print_f("Test Table : {%, %}\n", itr_vals->action_id);
      itr_vals = validator.erase(itr_vals);
    }

    std::vector<std::string> validator_v = split(validators_str, "-");
    for (auto i : validator_v) {
      eosio::name acc = eosio::name{i};
      eosio_assert((bool)acc, "account from validator list cannot be empty");
      eosio_assert(is_account(acc), "account from validator list don't exist");

      // Must belong to the community
      auto validator_id = gen_uuid(cmm.symbol.raw(), acc.value);
      auto itr_validator = network.find(validator_id);
      eosio_assert(itr_validator != network.end(), "one of the validators doesn't belong to the community");

      // Add list of validators
      validator.emplace(_self, [&](auto &v) {
                                 v.id = validator.available_primary_key();
                                 v.action_id = action_id;
                                 v.validator = acc;
                               });
    };
  }
}

/// @abi action
/// Verify an automatic action
void bespiral::verifyaction(std::uint64_t action_id, eosio::name maker, eosio::name verifier) {
  // Validates verifier
  eosio_assert(is_account(verifier), "invalid account for verifier");
  eosio_assert(is_account(maker), "invalid account for maker");
  require_auth(verifier);

  // Validates if action exists
  actions action(_self, _self.value);
  auto itr_objact = action.find(action_id);
  eosio_assert(itr_objact != action.end(), "Can't find action with given action_id");
  auto &objact = *itr_objact;

  // Validates verifier belongs to the action community
  objectives objective(_self, _self.value);
  auto itr_obj = objective.find(objact.objective_id);
  eosio_assert(itr_obj != objective.end(), "Can't find objective with given action_id");
  auto &obj = *itr_obj;

  communities community(_self, _self.value);
  auto itr_cmm = community.find(obj.community.raw());
  eosio_assert(itr_cmm != community.end(), "Can't find community with given action_id");
  auto &cmm = *itr_cmm;

  networks network(_self, _self.value);
  auto verifier_id = gen_uuid(cmm.symbol.raw(), verifier.value);
  auto itr_network = network.find(verifier_id);
  eosio_assert(itr_network != network.end(), "Verifier doesn't belong to the community");

  // Validates if maker belongs to the action community
  auto maker_id = gen_uuid(cmm.symbol.raw(), maker.value);
  auto itr_maker_network = network.find(maker_id);
  eosio_assert(itr_maker_network != network.end(), "Maker doesn't belong to the community");

  // Validate if the action type is `automatic`
  eosio_assert(objact.verification_type == "automatic", "Can't verify actions that aren't automatic, you'll need to open a claim");

  eosio_assert(objact.is_completed == false, "This action is already completed");

  if (objact.usages > 0) {
    eosio_assert(objact.usages_left >= 1, "There are no usages left for this action");
  }

  // change status of verification
  action.modify(itr_objact, _self, [&](auto &a) {
                                     a.usages_left = a.usages_left - 1;

                                     if (a.usages_left - 1 <= 0) {
                                       a.is_completed = 1;
                                     }
                                   });

  // Find Token
  // bespiral_tokens tokens(currency_account, currency_account.value);
  bespiral_tokens tokens(currency_account, cmm.symbol.code().raw());
  const auto &token = tokens.get(cmm.symbol.code().raw(), "Can't find token configurations on bespiral token contract");

  if (objact.reward.amount > 0) {
    // Reward Action Claimer
    std::string memo_action = "Thanks for doing an action for your community";
    eosio::action reward_action = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                                currency_account,                                                 // Account
                                                eosio::name{"issue"},                                             // Action
                                                // to, quantity, memo
                                                std::make_tuple(maker, objact.reward, memo_action));
    reward_action.send();
  }

  // Don't reward verifier for automatic verifications
}

/// @abi action
/// Start a new claim on an action
void bespiral::claimaction(std::uint64_t action_id, eosio::name maker) {
  // Validate maker
  eosio_assert(is_account(maker), "invalid account for maker");
  require_auth(maker);

  // Validates if action exists
  actions action(_self, _self.value);
  auto itr_objact = action.find(action_id);
  eosio_assert(itr_objact != action.end(), "Can't find action with given action_id");
  auto &objact = *itr_objact;

  // Check if action is completed, have usages left or the deadline has been met
  eosio_assert(objact.is_completed == false, "This is action is already completed, can't open claim");
  if (objact.deadline > 0) {
      eosio_assert(objact.deadline > now(), "Deadline exceeded");
  }

  if (objact.usages > 0) {
    eosio_assert(objact.usages_left >= 1, "There are no usages left for this action");
  }

  // Check if the action is claimable
  eosio_assert(objact.verification_type == "claimable", "You can only open claims in claimable actions");

  // Validates maker belongs to the action community
  objectives objective(_self, _self.value);
  auto itr_obj = objective.find(objact.objective_id);
  eosio_assert(itr_obj != objective.end(), "Can't find objective with given action_id");
  auto &obj = *itr_obj;

  communities community(_self, _self.value);
  auto itr_cmm = community.find(obj.community.raw());
  eosio_assert(itr_cmm != community.end(), "Can't find community with given action_id");
  auto &cmm = *itr_cmm;

  networks network(_self, _self.value);
  auto maker_id = gen_uuid(cmm.symbol.raw(), maker.value);
  auto itr_network = network.find(maker_id);
  eosio_assert(itr_network != network.end(), "Maker doesn't belong to the community");


  // Get last used claim id and update item_index table
  uint64_t claim_id;
  claim_id = get_available_id("claims");

  // Emplace new claim
  // claimsnew claim_table(_self, _self.value);
  claims claim_table(_self, _self.value);
  claim_table.emplace(_self, [&](auto &c ) {
                                   c.id = claim_id;
                                   c.action_id = action_id;
                                   c.claimer = maker;
                                   c.status = "pending";
                                 });
}

/// @abi action
/// Send a vote to a given claim
void bespiral::verifyclaim(std::uint64_t claim_id, eosio::name verifier, std::uint8_t vote) {
  // Validates verifier belongs to the action community
  claims claim_table(_self, _self.value);
  // claimsnew claim_table(_self, _self.value);
  auto itr_clm = claim_table.find(claim_id);
  eosio_assert(itr_clm != claim_table.end(), "Can't find claim with given claim_id");
  auto &claim = *itr_clm;

  // Check if claim is already verified
  eosio_assert(claim.status == "pending", "Can't vote on already verified claim");

  // Validates if action exists
  actions action(_self, _self.value);
  auto itr_objact = action.find(claim.action_id);
  eosio_assert(itr_objact != action.end(), "Can't find action with given claim_id");
  auto &objact = *itr_objact;

  // Check if user belongs to the action_validator list
  validators validator(_self, objact.id);
  std::uint64_t validator_count = 0;
  for (auto itr_validators = validator.begin(); itr_validators != validator.end();) {
    if ((*itr_validators).validator == verifier) {
      validator_count++;
    }
    itr_validators++;
  }
  eosio_assert(validator_count > 0, "Verifier is not in the action validator list");

  // Check if action is completed, have usages left or the deadline has been met
  eosio_assert(objact.is_completed == false, "This is action is already completed, can't verify claim");

  if (objact.deadline > 0) {
    eosio_assert(objact.deadline > now(), "Deadline exceeded");
  }

  if (objact.usages > 0) {
    eosio_assert(objact.usages_left >= 1, "There are no usages left for this action");
  }

  // Get check index
  checks check(_self, _self.value);

  // Assert that verifier hasn't done this previously
  auto check_by_claim = check.get_index<eosio::name{"byclaim"}>();
  auto itr_check_claim = check_by_claim.find(claim_id);

  if(itr_check_claim != check_by_claim.end()) {
    for (; itr_check_claim != check_by_claim.end(); itr_check_claim++) {
      auto check_claim = *itr_check_claim;
      eosio_assert(check_claim.validator != verifier, "The verifier cannot check the same claim more than once");
    }
  }

  // Add new check
  check.emplace(_self, [&](auto &c) {
                         c.id = check.available_primary_key();
                         c.claim_id = claim.id;
                         c.validator = verifier;
                         c.is_verified = vote;
                       });

  if (objact.verifier_reward.amount > 0) {
    // Send verification reward
    std::string memo_verification = "Thanks for verifying an action for your community";
    eosio::action verification_reward = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                                      currency_account,                                                 // Account
                                                      eosio::name{"issue"},                                             // Action
                                                      // to, quantity, memo
                                                      std::make_tuple(verifier, objact.verifier_reward, memo_verification));
    verification_reward.send();
  }

  // We always update the claim status, at each vote
  // In order to know if its approved or rejected, we will have to count all existing checks, to see if we already have all needed
  // Just check `objact.verifications <= check counter`

  // Then we will have to count the positive and negative votes...
  // If more than half was positive, its approved
  // else its rejected

  // If we don't have yet the number of votes necessary, then its pending

  // At every vote we will have to update the claim status
  std::uint64_t positive_votes = 0;
  std::uint64_t negative_votes = 0;
  auto itr_check = check_by_claim.find(claim.id);
  for(;itr_check != check_by_claim.end();) {
    if ((*itr_check).is_verified == 1) {
      positive_votes++;
    } else {
      negative_votes++;
    }
    itr_check++;
  }

  std::string status = "pending";
  if (positive_votes + negative_votes >= objact.verifications) {
    if (positive_votes > negative_votes) {
      status = "approved";
    } else {
      status = "rejected";
    }
  }

  claim_table.modify(itr_clm, _self, [&](auto &c) {
                                       c.status = status;
                                     });

  if (status == "approved") {
    if (objact.reward.amount > 0) {
      // Send reward
      std::string memo_action = "Thanks for doing an action for your community";
      eosio::action reward_action = eosio::action(eosio::permission_level{currency_account, eosio::name{"active"}}, // Permission
                                                  currency_account,                                                 // Account
                                                  eosio::name{"issue"},                                             // Action
                                                  // to, quantity, memo
                                                  std::make_tuple(claim.claimer, objact.reward, memo_action));
      reward_action.send();
    }
  }

  // Check if action can be completed. Current claim must be either "approved" or "rejected"
  if (status != "pending") {
    if (!objact.is_completed && objact.usages > 0) {
      action.modify(itr_objact, _self, [&](auto &a) {
                                         a.usages_left = objact.usages_left - 1;
                                         a.is_completed = (objact.usages_left - 1) == 0 ? 0 : 1;
                                       });
    }

  }
}

void bespiral::createsale(eosio::name from, std::string title, std::string description,
                          eosio::asset quantity, std::string image,
                          std::uint8_t track_stock, std::uint64_t units) {
  // Validate user
  require_auth(from);

  // Validate quantity
  eosio_assert(quantity.is_valid(), "Quantity is invalid");
  eosio_assert(quantity.amount >= 0, "Invalid amount of quantity, must be greater than or equal to 0");

  // Check if stock is tracked
  if (track_stock >= 1) {
    eosio_assert(units > 0, "Invalid number of units, must use a positive value");
  } else {
    // Discard units value if not tracking stock
    units = 0;
  }

  // Validate Strings
  eosio_assert(title.length() <= 256, "Invalid length for title, must be less than 256 characters");
  eosio_assert(description.length() <= 256, "Invalid length for description, must be less than 256 characters");
  eosio_assert(image.length() <= 256, "Invalid length for image, must be less than 256 characters");

  // Validate user belongs to community
  auto from_id = gen_uuid(quantity.symbol.raw(), from.value);
  networks network(_self, _self.value);
  const auto &netlink = network.get(from_id, "'from' account doesn't belong to the community");

  // Get last used objective id and update item_index table
  uint64_t sale_id;
  sale_id = get_available_id("sales");

  // Insert new sale
  sales sale(_self, _self.value);
  sale.emplace(_self, [&](auto &s) {
    s.id = sale_id;
    s.creator = from;
    s.community = netlink.community;
    s.title = title;
    s.description = description;
    s.image = image;
    s.track_stock = track_stock;
    s.quantity = quantity;
    s.units = units;
  });
}

void bespiral::updatesale(std::uint64_t sale_id, std::string title,
                          std::string description, eosio::asset quantity,
                          std::string image, std::uint8_t track_stock, std::uint64_t units) {
    // Find sale
  sales sale(_self, _self.value);
  const auto &found_sale = sale.get(sale_id, "Can't find any sale with given sale_id");

  // Validate user
  require_auth(found_sale.creator);


  // Validate quantity
  eosio_assert(quantity.is_valid(), "Quantity is invalid");
  eosio_assert(quantity.amount >= 0, "Invalid amount of quantity, must use a positive value");

  // Check if stock is tracked
  if (found_sale.track_stock >= 1 && track_stock == 1) {
    eosio_assert(units >= 0, "Invalid number of units, must be greater than or equal to 0");
  } else {
    // Discard units value if not tracking stock
    units = 0;
  }

  // Validate Strings
  eosio_assert(title.length() <= 256, "Invalid length for title, must be less than 256 characters");
  eosio_assert(description.length() <= 256, "Invalid length for description, must be less than 256 characters");
  eosio_assert(image.length() <= 256, "Invalid length for image, must be less than 256 characters");

  // Validate user belongs to community
  auto id = gen_uuid(quantity.symbol.raw(), found_sale.creator.value);
  networks network(_self, _self.value);
  const auto &netlink = network.get(id, "This account doesn't belong to the community");

  // Update sale
  sale.modify(found_sale, _self, [&](auto &s) {
    s.title = title;
    s.description = description;
    s.image = image;
    s.quantity = quantity;
    s.units = units;
    s.track_stock = track_stock;
  });
}

void bespiral::deletesale(std::uint64_t sale_id) {
  // Find sale
  sales sale(_self, _self.value);
  auto itr_sale = sale.find(sale_id);
  eosio_assert(itr_sale != sale.end(), "Can't find any sale with the given sale_id");
  const auto &found_sale = *itr_sale;

  // Validate user
  require_auth(found_sale.creator);

  // Remove sale
  sale.erase(itr_sale);
}

void bespiral::reactsale(std::uint64_t sale_id, eosio::name from, std::string type) {
  // Validate user
  require_auth(from);

  // Find sale
  sales sale(_self, _self.value);
  const auto &found_sale = sale.get(sale_id, "Can't find any sale with given sale_id");

  // Validate user is not the sale creator
  eosio_assert(from != found_sale.creator, "Can't react to your own sale");

  // Validate user belongs to sale's community
  auto from_id = gen_uuid(found_sale.community.raw(), from.value);
  networks network(_self, _self.value);
  auto itr_network = network.find(from_id);
  eosio_assert(itr_network != network.end(), "This account can't react to a sale from a community it doesn't belong");

  // Validate vote type
  eosio_assert(
          type == "thumbsup"
          || type == "thumbsdown"
          || type == "none"
          , "React type must be some of: 'thumbsup', 'thumbsdown' or 'none'");
}

// to = sale creator
void bespiral::transfersale(std::uint64_t sale_id, eosio::name from, eosio::name to, eosio::asset quantity, std::uint64_t units) {
  // Validate user
  require_auth(from);

  // Validate 'to' account
  eosio_assert(is_account(to), "The sale creator (to) account doesn't exists");

  // Validate accounts are different
  eosio_assert(from != to, "Can't sale for yourself");

  // Find sale
  sales sale(_self, _self.value);
  const auto &found_sale = sale.get(sale_id, "Can't find any sale with given sale_id");

  if (found_sale.track_stock == 1) {
    // Validate units
    eosio_assert(units > 0, "Invalid number of units, must be greater than 0");

    // Validate sale has that amount of units available
    eosio_assert(found_sale.units >= units, "Sale doesn't have that many units available");

    // Check amount depending on quantity
    const auto found_sale_sub_total = found_sale.quantity.amount * units;
    const auto from_total_offered = quantity.amount * units;
    eosio_assert(from_total_offered == found_sale_sub_total, "Amount offered doesn't correspond to expected value");
  } else {
    // Without trackStock
    eosio_assert(quantity == found_sale.quantity, "Quantity must be the same as the sale price");
  }

  // Validate 'from' user belongs to sale community
  auto from_id = gen_uuid(found_sale.community.raw(), from.value);
  networks network(_self, _self.value);
  const auto &netlink = network.get(from_id, "You can't use transfersale to this sale if you aren't part of the community");

  // Validate 'to' user is the sale creator
  eosio_assert(found_sale.creator == to, "Sale creator and sale doesn't match");

  // Update sale
  if (found_sale.track_stock == 1) {
    sale.modify(found_sale, _self, [&](auto &s){
                                     s.units -= units;
                                   });
  }
}

// set chain indices
void bespiral::setindices(std::uint64_t sale_id, std::uint64_t objective_id, std::uint64_t action_id, std::uint64_t claim_id) {
  require_auth(_self);
	indexes default_indexes;
	auto current_indexes = curr_indexes.get_or_create(_self, default_indexes);

	current_indexes.last_used_sale_id = sale_id;
	current_indexes.last_used_objective_id = objective_id;
	current_indexes.last_used_action_id = action_id;
	current_indexes.last_used_claim_id = claim_id;

	curr_indexes.set(current_indexes, _self);
}

void bespiral::deleteobj(std::uint64_t id) {
  require_auth(_self);

  objectives objective(_self, _self.value);
  auto x = objective.find(id);
  eosio_assert(x != objective.end(), "Cant find objective with given di");
  objective.erase(x);
}

void bespiral::deleteact(std::uint64_t id) {
  require_auth(_self);

  actions action(_self, _self.value);
  auto found_action = action.find(id);
  eosio_assert(found_action != action.end(), "Cant find action with given id");
  action.erase(found_action);
}

void bespiral::migrate(std::uint64_t id, std::uint64_t increment) {
  // require_auth(_self);

  // communities community_table(_self, _self.value);
  // new_communities new_communities_table(_self, _self.value);

  // for (auto itr = community_table.begin(); itr != community_table.end();) {
  //   auto &community = *itr;

  //   new_communities_table.emplace(_self, [&](auto &r) {
  //                                        r.symbol = community.symbol;

  //                                        r.creator = community.creator;
  //                                        r.logo = community.logo;
  //                                        r.name = community.name;
  //                                        r.description = community.description;
  //                                        r.inviter_reward = community.inviter_reward;
  //                                        r.invited_reward = community.invited_reward;
  //                                        r.has_objectives = 1;
  //                                        r.has_shop = 1;
  //                                      });

  //   itr++;
  // }
}

void bespiral::clean(std::string t) {
  // Clean up the old claims table after the migration
  require_auth(_self);

  if (t == "claim") {
    claims claim_table(_self, _self.value);
    for(auto itr = claim_table.begin(); itr != claim_table.end();) {
      itr = claim_table.erase(itr);
    }
  }

  if (t == "community") {
    communities communities_table(_self, _self.value);
    for (auto itr = communities_table.begin(); itr != communities_table.end();) {
      itr = communities_table.erase(itr);
    }
  }
}

void bespiral::migrateafter(std::uint64_t claim_id, std::uint64_t increment) {
  // require_auth(_self);
  // communities communities_table(_self, _self.value);
  // new_communities new_communities_table(_self, _self.value);

  // for (auto itr = new_communities_table.begin(); itr != new_communities_table.end();) {
  //   auto &new_community = *itr;

  //   communities_table.emplace(_self, [&](auto &r) {
  //                                          r.symbol = new_community.symbol;

  //                                          r.creator = new_community.creator;
  //                                          r.logo = new_community.logo;
  //                                          r.name = new_community.name;
  //                                          r.description = new_community.description;
  //                                          r.inviter_reward = new_community.inviter_reward;
  //                                          r.invited_reward = new_community.invited_reward;
  //                                          r.has_objectives = new_community.has_objectives;
  //                                          r.has_shop = new_community.has_shop;
  //                                        });

  //   itr++;
  // }
}

// Get available key
uint64_t bespiral::get_available_id(std::string table) {
  eosio_assert(table == "actions" || table == "objectives" || table == "sales" || table == "claims", "Table index not available");

  // Init indexes table
  indexes default_indexes;
  auto current_indexes = curr_indexes.get_or_create(_self, default_indexes);

  uint64_t id = 1;

  if(table == "actions") {
      id = current_indexes.last_used_action_id + 1;
      current_indexes.last_used_action_id = id;
      curr_indexes.set(current_indexes, _self);
   } else if (table == "objectives") {
      id = current_indexes.last_used_objective_id + 1;
      current_indexes.last_used_objective_id = id;
      curr_indexes.set(current_indexes, _self);
   } else if(table == "sales") {
      id = current_indexes.last_used_sale_id + 1;
      current_indexes.last_used_sale_id = id;
      curr_indexes.set(current_indexes, _self);
   } else if(table == "claims") {
      id = current_indexes.last_used_claim_id + 1;
      current_indexes.last_used_claim_id = id;
      curr_indexes.set(current_indexes, _self);
  }

  return id;
}


EOSIO_DISPATCH(bespiral,
               (create)(update)(netlink) // Basic community
               (newobjective)(updobjective)(upsertaction) // Objectives and Actions
               (verifyaction) (claimaction)(verifyclaim) // Verifications and Claims
               (createsale)(updatesale) (deletesale)(reactsale)(transfersale) // Shop
               (setindices)(deleteobj)(deleteact) // Admin actions
               (migrate)(clean)(migrateafter) // Temporary migration actions
               );
