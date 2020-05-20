#!/usr/bin/env bash

# Make sure we don't use any unset variables
set -ux

cleos='cleos -u https://eosio.cambiatus.io'

LUCCA_KEY='EOS6UzXrw93HKhugfRewVNpU5aM9hSUSmcwWtWgecDgYi6nwEHMuu'
TEST_KEY='EOS8LtuSpUvAPWEJkzea1tAzzeWsWSrTpEsCmwacFbFxXz4Xjn5R4'

function create_eos_account () {
    $cleos system newaccount --stake-net "10.0000 EOS" --stake-cpu "10.0000 EOS" --buy-ram "10.0000 EOS" eosio $1 $2
}

function create_test_users() {
    echo "======== Creating Test Accounts"
    create_eos_account 'claimcreator' $TEST_KEY $TEST_KEY
    create_eos_account 'claimclaimer' $TEST_KEY $TEST_KEY
    create_eos_account 'claimverif1' $TEST_KEY $TEST_KEY
    create_eos_account 'claimverif2' $TEST_KEY $TEST_KEY
    create_eos_account 'claimverif3' $TEST_KEY $TEST_KEY
    create_eos_account 'claimverif4' $TEST_KEY $TEST_KEY
    create_eos_account 'claimverif5' $TEST_KEY $TEST_KEY
}

function create_test_community() {
    echo "======== Creating Test Community / Actions"
    $cleos push action bes.cmm create '["0 CLM", "claimcreator", "", "Claimers", "", "0 CLM", "0 CLM"]' -p claimcreator
    $cleos push action bes.token create '["claimcreator", "21000000 CLM", "-1000 CLM", "mcc"]' -p claimcreator

    $cleos push action bes.cmm netlink '["0 CLM", "claimcreator", "claimclaimer"]' -p claimcreator
    $cleos push action bes.cmm netlink '["0 CLM", "claimcreator", "claimverif1"]' -p claimcreator
    $cleos push action bes.cmm netlink '["0 CLM", "claimcreator", "claimverif2"]' -p claimcreator
    $cleos push action bes.cmm netlink '["0 CLM", "claimcreator", "claimverif3"]' -p claimcreator
    $cleos push action bes.cmm netlink '["0 CLM", "claimcreator", "claimverif4"]' -p claimcreator
    $cleos push action bes.cmm netlink '["0 CLM", "claimcreator", "claimverif5"]' -p claimcreator
    $cleos push action bes.cmm netlink '["0 CLM", "claimcreator", "lucca"]' -p claimcreator

    # as 18 may 2020, this insert yielded an ID of 64
    $cleos push action bes.cmm newobjective '["0 CLM", "Test claims", "claimcreator"]' -p claimcreator
}

function create_actions() {
    OBJECTIVE_ID=64

    $cleos push action bes.cmm upsertaction '[0, '$OBJECTIVE_ID', "Claim with 3 verifications", "1 CLM", "0 CLM", 0, 0, 0, 3, "claimable", "claimverif1-claimverif2-claimverif3-claimverif4", 0, "claimcreator"]' -p claimcreator
    $cleos push action bes.cmm upsertaction '[0, '$OBJECTIVE_ID', "Claim with 4 verifications", "1 CLM", "0 CLM", 0, 0, 0, 5, "claimable", "claimverif1-claimverif2-claimverif3-claimverif4-claimverif5", 0, "claimcreator"]' -p claimcreator
}

function claim_actions() {
    echo "======== Claiming actions"
    FIRST_ID=137
    SECOND_ID=138
    # We will do this as 3 batches, all with the same scenarios but the actions will have different rules

    ######### First Action, requires 3 approvals
    # First claim will be accepted by all
    FIRST_CLAIM=159
    # $cleos push action bes.cmm claimaction '['$FIRST_ID', claimclaimer]' -p claimclaimer

    # $cleos push action bes.cmm verifyclaim '['$FIRST_CLAIM', claimverif1, 1]' -p claimverif1
    # $cleos push action bes.cmm verifyclaim '['$FIRST_CLAIM', claimverif2, 1]' -p claimverif2
    # $cleos push action bes.cmm verifyclaim '['$FIRST_CLAIM', claimverif3, 1]' -p claimverif3

    # Second claim will be rejected by all
    SECOND_CLAIM=160
    # $cleos push action bes.cmm claimaction '['$FIRST_ID', claimclaimer]' -p claimclaimer

    # $cleos push action bes.cmm verifyclaim '['$SECOND_CLAIM', claimverif1, 0]' -p claimverif1
    # $cleos push action bes.cmm verifyclaim '['$SECOND_CLAIM', claimverif2, 0]' -p claimverif2
    # $cleos push action bes.cmm verifyclaim '['$SECOND_CLAIM', claimverif3, 0]' -p claimverif3


    # Thrid will be accepted by the majority
    THIRD_CLAIM=161
    # $cleos push action bes.cmm claimaction '['$FIRST_ID', claimclaimer]' -p claimclaimer

    # $cleos push action bes.cmm verifyclaim '['$THIRD_CLAIM', claimverif1, 1]' -p claimverif1
    # $cleos push action bes.cmm verifyclaim '['$THIRD_CLAIM', claimverif2, 1]' -p claimverif2
    # $cleos push action bes.cmm verifyclaim '['$THIRD_CLAIM', claimverif3, 0]' -p claimverif3

    # should fail, claim not pending
    # $cleos push action bes.cmm verifyclaim '['$THIRD_CLAIM', claimverif3, 1]' -p claimverif4

    # Forth will be rejected by the majority
    FORTH_CLAIM=162
    # $cleos push action bes.cmm claimaction '['$FIRST_ID', claimclaimer]' -p claimclaimer

    # $cleos push action bes.cmm verifyclaim '['$FORTH_CLAIM', claimverif1, 1]' -p claimverif1
    # $cleos push action bes.cmm verifyclaim '['$FORTH_CLAIM', claimverif2, 0]' -p claimverif2
    # $cleos push action bes.cmm verifyclaim '['$FORTH_CLAIM', claimverif3, 0]' -p claimverif3

    # should fail, claim not pending anymore
    # $cleos push action bes.cmm verifyclaim '['$FORTH_CLAIM', claimverif2, 0]' -p claimverif2

    ######### Second Action, requires 5 approvals
    # First claim will be accepted by all
    FIFTH_CLAIM=163
    # $cleos push action bes.cmm claimaction '['$SECOND_ID', claimclaimer]' -p claimclaimer

    # $cleos push action bes.cmm verifyclaim '['$FIFTH_CLAIM', claimverif1, 1]' -p claimverif1
    # $cleos push action bes.cmm verifyclaim '['$FIFTH_CLAIM', claimverif2, 1]' -p claimverif2
    # $cleos push action bes.cmm verifyclaim '['$FIFTH_CLAIM', claimverif3, 1]' -p claimverif3
    # $cleos push action bes.cmm verifyclaim '['$FIFTH_CLAIM', claimverif4, 1]' -p claimverif4
    # $cleos push action bes.cmm verifyclaim '['$FIFTH_CLAIM', claimverif5, 1]' -p claimverif5

    # Second claim will be rejeceted by all
    SIXTH_CLAIM=164
    # $cleos push action bes.cmm claimaction '['$SECOND_ID', claimclaimer]' -p claimclaimer

    # $cleos push action bes.cmm verifyclaim '['$SIXTH_CLAIM', claimverif1, 0]' -p claimverif1
    # $cleos push action bes.cmm verifyclaim '['$SIXTH_CLAIM', claimverif2, 0]' -p claimverif2
    # $cleos push action bes.cmm verifyclaim '['$SIXTH_CLAIM', claimverif3, 0]' -p claimverif3
    # $cleos push action bes.cmm verifyclaim '['$SIXTH_CLAIM', claimverif4, 0]' -p claimverif4
    # $cleos push action bes.cmm verifyclaim '['$SIXTH_CLAIM', claimverif5, 0]' -p claimverif5


    # Thrid will be accepted by the majority
    # SEVENTH_CLAIM=165
    # $cleos push action bes.cmm claimaction '['$SECOND_ID', claimclaimer]' -p claimclaimer

    # $cleos push action bes.cmm verifyclaim '['$SEVENTH_CLAIM', claimverif1, 0]' -p claimverif1
    # $cleos push action bes.cmm verifyclaim '['$SEVENTH_CLAIM', claimverif2, 0]' -p claimverif2
    # $cleos push action bes.cmm verifyclaim '['$SEVENTH_CLAIM', claimverif3, 1]' -p claimverif3
    # $cleos push action bes.cmm verifyclaim '['$SEVENTH_CLAIM', claimverif4, 1]' -p claimverif4
    # $cleos push action bes.cmm verifyclaim '['$SEVENTH_CLAIM', claimverif5, 1]' -p claimverif5


    # Forth will be rejeceted by the majority
    EIGHTH_CLAIM=166
    $cleos push action bes.cmm claimaction '['$SECOND_ID', claimclaimer]' -p claimclaimer

    $cleos push action bes.cmm verifyclaim '['$EIGHTH_CLAIM', claimverif1, 0]' -p claimverif1
    $cleos push action bes.cmm verifyclaim '['$EIGHTH_CLAIM', claimverif2, 0]' -p claimverif2
    $cleos push action bes.cmm verifyclaim '['$EIGHTH_CLAIM', claimverif3, 0]' -p claimverif3
    $cleos push action bes.cmm verifyclaim '['$EIGHTH_CLAIM', claimverif4, 1]' -p claimverif4
    $cleos push action bes.cmm verifyclaim '['$EIGHTH_CLAIM', claimverif5, 1]' -p claimverif5


}

function vote_claim() {
    echo "======== Verify claims"
    $cleos push action bes.cmm verifyclaim '[, "claimverif1", true]' -p claimverif1
}

# create_test_users
# create_test_community
# create_actions
# claim_actions
