//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.
    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.
    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <test/jtx.h>
#include <casinocoin/app/paths/Flow.h>
#include <casinocoin/app/paths/impl/Steps.h>
#include <casinocoin/basics/contract.h>
#include <casinocoin/core/Config.h>
#include <casinocoin/ledger/ApplyViewImpl.h>
#include <casinocoin/ledger/PaymentSandbox.h>
#include <casinocoin/ledger/Sandbox.h>
#include <test/jtx/PathSet.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/protocol/JsonFields.h>

namespace casinocoin {
namespace test {

bool getnoCasinocoinFlag (jtx::Env const& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    Currency const& cur)
{
    if (auto sle = env.le (keylet::line (src, dst, cur)))
    {
        auto const flag = (src.id() > dst.id()) ? lsfHighNoCasinocoin : lsfLowNoCasinocoin;
        return sle->isFlag (flag);
    }
    Throw<std::runtime_error> ("No line in getTrustFlag");
    return false; // silence warning
}

jtx::PrettyAmount
cscMinusFee (jtx::Env const& env, std::int64_t cscAmount)
{
    using namespace jtx;
    auto feeDrops = env.current ()->fees ().base;
    return drops (
        dropsPerCSC<std::int64_t>::value * cscAmount - feeDrops);
};

struct Flow_test : public beast::unit_test::suite
{
    static bool hasFeature(uint256 const& feat, std::initializer_list<uint256> args)
    {
        for(auto const& f : args)
            if (f == feat)
                return true;
        return false;
    }

    void testDirectStep (std::initializer_list<uint256> fs)
    {
        testcase ("Direct Step");

        using namespace jtx;
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        auto const dan = Account ("dan");
        auto const erin = Account ("erin");
        auto const USDA = alice["USD"];
        auto const USDB = bob["USD"];
        auto const USDC = carol["USD"];
        auto const USDD = dan["USD"];
        auto const gw = Account ("gw");
        auto const USD = gw["USD"];
        {
            // Pay USD, trivial path
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, gw);
            env.trust (USD (1000), alice, bob);
            env (pay (gw, alice, USD (100)));
            env (pay (alice, bob, USD (10)), paths (USD));
            env.require (balance (bob, USD (10)));
        }
        {
            // CSC transfer
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob);
            env (pay (alice, bob, CSC (100)));
            env.require (balance (bob, CSC (10000 + 100)));
            env.require (balance (alice, cscMinusFee (env, 10000 - 100)));
        }
        {
            // Partial payments
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, gw);
            env.trust (USD (1000), alice, bob);
            env (pay (gw, alice, USD (100)));
            env (pay (alice, bob, USD (110)), paths (USD),
                ter (tecPATH_PARTIAL));
            env.require (balance (bob, USD (0)));
            env (pay (alice, bob, USD (110)), paths (USD),
                txflags (tfPartialPayment));
            env.require (balance (bob, USD (100)));
        }
        {
            // Pay by rippling through accounts, use path finder
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, dan);
            env.trust (USDA (10), bob);
            env.trust (USDB (10), carol);
            env.trust (USDC (10), dan);
            env (pay (alice, dan, USDC (10)), paths (USDA));
            env.require (
                balance (bob, USDA (10)),
                balance (carol, USDB (10)),
                balance (dan, USDC (10)));
        }
        {
            // Pay by rippling through accounts, specify path
            // and charge a transfer fee
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, dan);
            env.trust (USDA (10), bob);
            env.trust (USDB (10), alice, carol);
            env.trust (USDC (10), dan);
            env (rate (bob, 1.1));

            // alice will redeem to bob; a transfer fee will be charged
            env (pay (bob, alice, USDB(6)));
            env (pay (alice, dan, USDC (5)), path (bob, carol),
                sendmax (USDA (6)), txflags (tfNoCasinocoinDirect));
            env.require (balance (dan, USDC (5)));
            env.require (balance (alice, USDB (0.5)));
        }
        {
            // Pay by rippling through accounts, specify path and transfer fee
            // Test that the transfer fee is not charged when alice issues
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, dan);
            env.trust (USDA (10), bob);
            env.trust (USDB (10), alice, carol);
            env.trust (USDC (10), dan);
            env (rate (bob, 1.1));

            env (pay (alice, dan, USDC (5)), path (bob, carol),
                 sendmax (USDA (6)), txflags (tfNoCasinocoinDirect));
            env.require (balance (dan, USDC (5)));
            env.require (balance (bob, USDA (5)));
        }
        {
            // test best quality path is taken
            // Paths: A->B->D->E ; A->C->D->E
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, dan, erin);
            env.trust (USDA (10), bob, carol);
            env.trust (USDB (10), dan);
            env.trust (USDC (10), alice, dan);
            env.trust (USDD (20), erin);
            env (rate (bob, 1));
            env (rate (carol, 1.1));

            // Pay alice so she redeems to carol and a transfer fee is charged
            env (pay (carol, alice, USDC(10)));
            env (pay (alice, erin, USDD (5)), path (carol, dan),
                path (bob, dan), txflags (tfNoCasinocoinDirect));

            env.require (balance (erin, USDD (5)));
            env.require (balance (dan, USDB (5)));
            env.require (balance (dan, USDC (0)));
        }
        {
            // Limit quality
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol);
            env.trust (USDA (10), bob);
            env.trust (USDB (10), carol);

            env (pay (alice, carol, USDB (5)), sendmax (USDA (4)),
                txflags (tfLimitQuality | tfPartialPayment), ter (tecPATH_DRY));
            env.require (balance (carol, USDB (0)));

            env (pay (alice, carol, USDB (5)), sendmax (USDA (4)),
                txflags (tfPartialPayment));
            env.require (balance (carol, USDB (4)));
        }
    }

    void testLineQuality (std::initializer_list<uint256> fs)
    {
        testcase ("Line Quality");

        using namespace jtx;
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        auto const dan = Account ("dan");
        auto const USDA = alice["USD"];
        auto const USDB = bob["USD"];
        auto const USDC = carol["USD"];
        auto const USDD = dan["USD"];

        //   Dan -> Bob -> Alice -> Carol; vary bobDanQIn and bobAliceQOut
        for (auto bobDanQIn : {80, 100, 120})
            for (auto bobAliceQOut : {80, 100, 120})
            {
                if (!hasFeature(featureFlow, fs) && bobDanQIn < 100 &&
                    bobAliceQOut < 100)
                    continue;  // Bug in flow v1
                Env env(*this, features(fs));
                env.fund(CSC(10000), alice, bob, carol, dan);
                env(trust(bob, USDD(100)), qualityInPercent(bobDanQIn));
                env(trust(bob, USDA(100)), qualityOutPercent(bobAliceQOut));
                env(trust(carol, USDA(100)));

                env(pay(alice, bob, USDA(100)));
                env.require(balance(bob, USDA(100)));
                env(pay(dan, carol, USDA(10)),
                    path(bob), sendmax(USDD(100)), txflags(tfNoCasinocoinDirect));
                env.require(balance(bob, USDA(90)));
                if (bobAliceQOut > bobDanQIn)
                    env.require(balance(
                        bob,
                        USDD(10.0 * double(bobAliceQOut) / double(bobDanQIn))));
                else
                    env.require(balance(bob, USDD(10)));
                env.require(balance(carol, USDA(10)));
            }

        // bob -> alice -> carol; vary carolAliceQIn
        for (auto carolAliceQIn : {80, 100, 120})
        {
            Env env(*this, features(fs));
            env.fund(CSC(10000), alice, bob, carol);
            env(trust(bob, USDA(10)));
            env(trust(carol, USDA(10)), qualityInPercent(carolAliceQIn));

            env(pay(alice, bob, USDA(10)));
            env.require(balance(bob, USDA(10)));
            env(pay(bob, carol, USDA(5)), sendmax(USDA(10)));
            auto const effectiveQ =
                carolAliceQIn > 100 ? 1.0 : carolAliceQIn / 100.0;
            env.require(balance(bob, USDA(10.0 - 5.0 / effectiveQ)));
        }

        // bob -> alice -> carol; bobAliceQOut varies.
        for (auto bobAliceQOut : {80, 100, 120})
        {
            Env env(*this, features(fs));
            env.fund(CSC(10000), alice, bob, carol);
            env(trust(bob, USDA(10)), qualityOutPercent(bobAliceQOut));
            env(trust(carol, USDA(10)));

            env(pay(alice, bob, USDA(10)));
            env.require(balance(bob, USDA(10)));
            env(pay(bob, carol, USDA(5)), sendmax(USDA(5)));
            env.require(balance(carol, USDA(5)));
            env.require(balance(bob, USDA(10 - 5)));
        }
    }

    void testBookStep (std::initializer_list<uint256> fs)
    {
        testcase ("Book Step");

        using namespace jtx;

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        auto const EUR = gw["EUR"];
        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");

        {
            // simple IOU/IOU offer
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);

            env (pay (gw, alice, BTC (50)));
            env (pay (gw, bob, USD (50)));

            env (offer (bob, BTC (50), USD (50)));

            env (pay (alice, carol, USD (50)), path (~USD), sendmax (BTC (50)));

            env.require (balance (alice, BTC (0)));
            env.require (balance (bob, BTC (50)));
            env.require (balance (bob, USD (0)));
            env.require (balance (carol, USD (50)));
            BEAST_EXPECT(!isOffer (env, bob, BTC (50), USD (50)));
        }
        {
            // simple IOU/CSC CSC/IOU offer
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);

            env (pay (gw, alice, BTC (50)));
            env (pay (gw, bob, USD (50)));

            env (offer (bob, BTC (50), CSC (50)));
            env (offer (bob, CSC (50), USD (50)));

            env (pay (alice, carol, USD (50)), path (~CSC, ~USD),
                sendmax (BTC (50)));

            env.require (balance (alice, BTC (0)));
            env.require (balance (bob, BTC (50)));
            env.require (balance (bob, USD (0)));
            env.require (balance (carol, USD (50)));
            BEAST_EXPECT(!isOffer (env, bob, CSC (50), USD (50)));
            BEAST_EXPECT(!isOffer (env, bob, BTC (50), CSC (50)));
        }
        {
            // simple CSC -> USD through offer and sendmax
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);

            env (pay (gw, bob, USD (50)));

            env (offer (bob, CSC (50), USD (50)));

            env (pay (alice, carol, USD (50)), path (~USD),
                 sendmax (CSC (50)));

            env.require (balance (alice, cscMinusFee (env, 10000 - 50)));
            env.require (balance (bob, cscMinusFee (env, 10000 + 50)));
            env.require (balance (bob, USD (0)));
            env.require (balance (carol, USD (50)));
            BEAST_EXPECT(!isOffer (env, bob, CSC (50), USD (50)));
        }
        {
            // simple USD -> CSC through offer and sendmax
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);

            env (pay (gw, alice, USD (50)));

            env (offer (bob, USD (50), CSC (50)));

            env (pay (alice, carol, CSC (50)), path (~CSC),
                 sendmax (USD (50)));

            env.require (balance (alice, USD (0)));
            env.require (balance (bob, cscMinusFee (env, 10000 - 50)));
            env.require (balance (bob, USD (50)));
            env.require (balance (carol, CSC (10000 + 50)));
            BEAST_EXPECT(!isOffer (env, bob, USD (50), CSC (50)));
        }
        {
            // test unfunded offers are removed when payment succeeds
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);
            env.trust (EUR (1000), alice, bob, carol);

            env (pay (gw, alice, BTC (60)));
            env (pay (gw, bob, USD (50)));
            env (pay (gw, bob, EUR (50)));

            env (offer (bob, BTC (50), USD (50)));
            env (offer (bob, BTC (60), EUR (50)));
            env (offer (bob, EUR (50), USD (50)));

            // unfund offer
            env (pay (bob, gw, EUR (50)));
            BEAST_EXPECT(isOffer (env, bob, BTC (50), USD (50)));
            BEAST_EXPECT(isOffer (env, bob, BTC (60), EUR (50)));
            BEAST_EXPECT(isOffer (env, bob, EUR (50), USD (50)));

            env (pay (alice, carol, USD (50)),
                path (~USD), path (~EUR, ~USD),
                sendmax (BTC (60)));

            env.require (balance (alice, BTC (10)));
            env.require (balance (bob, BTC (50)));
            env.require (balance (bob, USD (0)));
            env.require (balance (bob, EUR (0)));
            env.require (balance (carol, USD (50)));
            // used in the payment
            BEAST_EXPECT(!isOffer (env, bob, BTC (50), USD (50)));
            // found unfunded
            BEAST_EXPECT(!isOffer (env, bob, BTC (60), EUR (50)));
            // unfunded, but should not yet be found unfunded
            BEAST_EXPECT(isOffer (env, bob, EUR (50), USD (50)));
        }
        {
            // test unfunded offers are returned when the payment fails.
            // bob makes two offers: a funded 50 USD for 50 BTC and an unfunded 50
            // EUR for 60 BTC. alice pays carol 61 USD with 61 BTC. alice only
            // has 60 BTC, so the payment will fail. The payment uses two paths:
            // one through bob's funded offer and one through his unfunded
            // offer. When the payment fails `flow` should return the unfunded
            // offer. This test is intentionally similar to the one that removes
            // unfunded offers when the payment succeeds.
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);
            env.trust (EUR (1000), alice, bob, carol);

            env (pay (gw, alice, BTC (60)));
            env (pay (gw, bob, USD (50)));
            env (pay (gw, bob, EUR (50)));

            env (offer (bob, BTC (50), USD (50)));
            env (offer (bob, BTC (60), EUR (50)));
            env (offer (bob, EUR (50), USD (50)));

            // unfund offer
            env (pay (bob, gw, EUR (50)));
            BEAST_EXPECT(isOffer (env, bob, BTC (50), USD (50)));
            BEAST_EXPECT(isOffer (env, bob, BTC (60), EUR (50)));

            auto flowJournal = env.app ().logs ().journal ("Flow");
            auto const flowResult = [&]
            {
                STAmount deliver (USD (51));
                STAmount smax (BTC (61));
                PaymentSandbox sb (env.current ().get (), tapNONE);
                STPathSet paths;
                auto IPE = [](Issue const& iss) {
                    return STPathElement(
                        STPathElement::typeCurrency | STPathElement::typeIssuer,
                        cscAccount(),
                        iss.currency,
                        iss.account);
                };
                {

                    // BTC -> USD
                    STPath p1 ({IPE (USD.issue ())});
                    paths.push_back (p1);
                    // BTC -> EUR -> USD
                    STPath p2 ({IPE (EUR.issue ()), IPE (USD.issue ())});
                    paths.push_back (p2);
                }

                return flow (sb, deliver, alice, carol, paths, false, false,
                    true, false, boost::none, smax, flowJournal);
            }();

            BEAST_EXPECT(flowResult.removableOffers.size () == 1);
            env.app ().openLedger ().modify (
                [&](OpenView& view, beast::Journal j)
                {
                    if (flowResult.removableOffers.empty())
                        return false;
                    Sandbox sb (&view, tapNONE);
                    for (auto const& o : flowResult.removableOffers)
                        if (auto ok = sb.peek (keylet::offer (o)))
                            offerDelete (sb, ok, flowJournal);
                    sb.apply (view);
                    return true;
                });

            // used in payment, but since payment failed should be untouched
            BEAST_EXPECT(isOffer (env, bob, BTC (50), USD (50)));
            // found unfunded
            BEAST_EXPECT(!isOffer (env, bob, BTC (60), EUR (50)));
        }
        {
            // Do not produce more in the forward pass than the reverse pass
            // This test uses a path that whose reverse pass will compute a
            // 0.5 USD input required for a 1 EUR output. It sets a sendmax of
            // 0.4 USD, so the payment engine will need to do a forward pass.
            // Without limits, the 0.4 USD would produce 1000 EUR in the forward
            // pass. This test checks that the payment produces 1 EUR, as expected.

            Env env (*this, features (fs));

            auto const closeTime = STAmountSO::soTime2 +
                100 * env.closed ()->info ().closeTimeResolution;
            env.close (closeTime);

            env.fund (CSC (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (EUR (1000), alice, bob, carol);

            env (pay (gw, alice, USD (1000)));
            env (pay (gw, bob, EUR (1000)));

            env (offer (bob, USD (1), drops (2)), txflags (tfPassive));
            env (offer (bob, drops (1), EUR (1000)), txflags (tfPassive));

            env (pay (alice, carol, EUR (1)), path (~CSC, ~EUR),
                sendmax (USD (0.4)), txflags (tfNoCasinocoinDirect|tfPartialPayment));

            env.require (balance (carol, EUR (1)));
            env.require (balance (bob, USD (0.4)));
            env.require (balance (bob, EUR (999)));
        }
    }

    void testTransferRate (std::initializer_list<uint256> fs)
    {
        testcase ("Transfer Rate");

        using namespace jtx;

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        auto const EUR = gw["EUR"];
        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");


        {
            // Simple payment through a gateway with a
            // transfer rate
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob, carol);
            env (pay (gw, alice, USD (50)));
            env.require (balance (alice, USD (50)));
            env (pay (alice, bob, USD (40)), sendmax (USD (50)));
            env.require (balance (bob, USD (40)), balance (alice, USD (0)));
        }
        {
            // transfer rate is not charged when issuer is src or dst
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob, carol);
            env (pay (gw, alice, USD (50)));
            env.require (balance (alice, USD (50)));
            env (pay (alice, gw, USD (40)), sendmax (USD (40)));
            env.require (balance (alice, USD (10)));
        }
        {
            // transfer fee on an offer
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob, carol);
            env (pay (gw, bob, USD (65)));

            env (offer (bob, CSC (50), USD (50)));

            env (pay (alice, carol, USD (50)), path (~USD), sendmax (CSC (50)));
            env.require (
                balance (alice, cscMinusFee (env, 10000 - 50)),
                balance (bob, USD (2.5)), // owner pays transfer fee
                balance (carol, USD (50)));
        }

        {
            // Transfer fee two consecutive offers
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob, carol);
            env.trust (EUR (1000), alice, bob, carol);
            env (pay (gw, bob, USD (50)));
            env (pay (gw, bob, EUR (50)));

            env (offer (bob, CSC (50), USD (50)));
            env (offer (bob, USD (50), EUR (50)));

            env (pay (alice, carol, EUR (40)), path (~USD, ~EUR), sendmax (CSC (40)));
            env.require (
                balance (alice,  cscMinusFee (env, 10000 - 40)),
                balance (bob, USD (40)),
                balance (bob, EUR (0)),
                balance (carol, EUR (40)));
        }

        {
            // First pass through a strand redeems, second pass issues, no offers
            // limiting step is not an endpoint
            Env env (*this, features(fs));
            auto const USDA = alice["USD"];
            auto const USDB = bob["USD"];

            env.fund (CSC (10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob, carol);
            env.trust (USDA (1000), bob);
            env.trust (USDB (1000), gw);
            env (pay (gw, bob, USD (50)));
            // alice -> bob -> gw -> carol. $50 should have transfer fee; $10, no fee
            env (pay (alice, carol, USD(50)), path (bob), sendmax (USDA(60)));
            env.require (
                balance (bob, USD (-10)),
                balance (bob, USDA (60)),
                balance (carol, USD (50)));
        }
        {
            // First pass through a strand redeems, second pass issues, through an offer
            // limiting step is not an endpoint
            Env env (*this, features(fs));
            auto const USDA = alice["USD"];
            auto const USDB = bob["USD"];
            Account const dan ("dan");

            env.fund (CSC (10000), alice, bob, carol, dan, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob, carol, dan);
            env.trust (EUR (1000), carol, dan);
            env.trust (USDA (1000), bob);
            env.trust (USDB (1000), gw);
            env (pay (gw, bob, USD (50)));
            env (pay (gw, dan, EUR (100)));
            env (offer (dan, USD (100), EUR (100)));
            // alice -> bob -> gw -> carol. $50 should have transfer fee; $10, no fee
            env (pay (alice, carol, EUR (50)), path (bob, gw, ~EUR),
                sendmax (USDA (60)), txflags (tfNoCasinocoinDirect));
            env.require (
                balance (bob, USD (-10)),
                balance (bob, USDA (60)),
                balance (dan, USD (50)),
                balance (dan, EUR (37.5)),
                balance (carol, EUR (50)));
        }

        {
            // Offer where the owner is also the issuer, owner pays fee
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob);
            env (offer (gw, CSC (100), USD (100)));
            env (pay (alice, bob, USD (100)),
                 sendmax (CSC (100)));
            env.require (
                balance (alice, cscMinusFee(env, 10000-100)),
                balance (bob, USD (100)));
        }
        if (!hasFeature(featureOwnerPaysFee, fs))
        {
            // Offer where the owner is also the issuer, sender pays fee
            Env env (*this, features(fs));

            env.fund (CSC (10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob);
            env (offer (gw, CSC (125), USD (125)));
            env (pay (alice, bob, USD (100)),
                 sendmax (CSC (200)));
            env.require (
                balance (alice, cscMinusFee(env, 10000-125)),
                balance (bob, USD (100)));
        }
    }

    void
    testFalseDry(std::initializer_list<uint256> fs)
    {
        testcase ("falseDryChanges");

        using namespace jtx;

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];
        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");

        Env env (*this, features (fs));

        auto const closeTime = fix1141Time() +
                100 * env.closed ()->info ().closeTimeResolution;
        env.close (closeTime);

        env.fund (CSC(10000), alice, carol, gw);
        env.fund (reserve(env, 5), bob);
        env.trust (USD (1000), alice, bob, carol);
        env.trust (EUR (1000), alice, bob, carol);


        env (pay (gw, alice, EUR (50)));
        env (pay (gw, bob, USD (50)));

        // Bob has _just_ slightly less than 50 csc available
        // If his owner count changes, he will have more liquidity.
        // This is one error case to test (when Flow is used).
        // Computing the incomming csc to the CSC/USD offer will require two
        // recursive calls to the EUR/CSC offer. The second call will return
        // tecPATH_DRY, but the entire path should not be marked as dry. This
        // is the second error case to test (when flowV1 is used).
        env (offer (bob, EUR (50), CSC (50)));
        env (offer (bob, CSC (50), USD (50)));

        env (pay (alice, carol, USD (1000000)), path (~CSC, ~USD),
            sendmax (EUR (500)),
            txflags (tfNoCasinocoinDirect | tfPartialPayment));

        auto const carolUSD = env.balance(carol, USD).value();
        BEAST_EXPECT(carolUSD > USD (0) && carolUSD < USD (50));
    }

    void
    testLimitQuality ()
    {
        // Single path with two offers and limit quality. The quality limit is
        // such that the first offer should be taken but the second should not.
        // The total amount delivered should be the sum of the two offers and
        // sendMax should be more than the first offer.
        testcase ("limitQuality");
        using namespace jtx;

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");

        auto const timeDelta = Env{*this}.closed ()->info ().closeTimeResolution;

        for(auto const& d: {-timeDelta*100, +timeDelta*100}){
            auto const closeTime = fix1141Time () + d;
            Env env (*this);
            env.close (closeTime);

            env.fund (CSC(10000), alice, bob, carol, gw);

            env.trust (USD(100), alice, bob, carol);
            env (pay (gw, bob, USD (100)));
            env (offer (bob, CSC (50), USD (50)));
            env (offer (bob, CSC (100), USD (50)));

            auto expectedResult =
                closeTime < fix1141Time () ? tecPATH_DRY : tesSUCCESS;
            env (pay (alice, carol, USD (100)), path (~USD), sendmax (CSC (100)),
                txflags (tfNoCasinocoinDirect | tfPartialPayment | tfLimitQuality),
                ter (expectedResult));

            if (expectedResult == tesSUCCESS)
                env.require (balance (carol, USD (50)));
        }
    }

    // Helper function that returns the reserve on an account based on
    // the passed in number of owners.
    static CSCAmount reserve(jtx::Env& env, std::uint32_t count)
    {
        return env.current()->fees().accountReserve (count);
    }

    // Helper function that returns the Offers on an account.
    static std::vector<std::shared_ptr<SLE const>>
    offersOnAccount (jtx::Env& env, jtx::Account account)
    {
        std::vector<std::shared_ptr<SLE const>> result;
        forEachItem (*env.current (), account,
            [&env, &result](std::shared_ptr<SLE const> const& sle)
            {
                if (sle->getType() == ltOFFER)
                     result.push_back (sle);
            });
        return result;
    }

    void
    testSelfPayment1(std::initializer_list<uint256> fs)
    {
        testcase ("Self-payment 1");

        // In this test case the new flow code mis-computes the amount
        // of money to move.  Fortunately the new code's re-execute
        // check catches the problem and throws out the transaction.
        //
        // The old payment code handles the payment correctly.
        using namespace jtx;

        auto const gw1 = Account ("gw1");
        auto const gw2 = Account ("gw2");
        auto const alice = Account ("alice");
        auto const USD = gw1["USD"];
        auto const EUR = gw2["EUR"];

        Env env (*this, features (fs));

        auto const closeTime =
            fix1141Time () + 100 * env.closed ()->info ().closeTimeResolution;
        env.close (closeTime);

        env.fund (CSC (1000000), gw1, gw2);
        env.close ();

        // The fee that's charged for transactions.
        auto const f = env.current ()->fees ().base;

        env.fund (reserve (env, 3) + f * 4, alice);
        env.close ();

        env (trust (alice, USD (2000)));
        env (trust (alice, EUR (2000)));
        env.close ();

        env (pay (gw1, alice, USD (1)));
        env (pay (gw2, alice, EUR (1000)));
        env.close ();

        env (offer (alice, USD (500), EUR (600)));
        env.close ();

        env.require (owners (alice, 3));
        env.require (balance (alice, USD (1)));
        env.require (balance (alice, EUR (1000)));

        auto aliceOffers = offersOnAccount (env, alice);
        BEAST_EXPECT(aliceOffers.size () == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == EUR (600));
            BEAST_EXPECT(offer[sfTakerPays] == USD (500));
        }

        env (pay (alice, alice, EUR (600)), sendmax (USD (500)),
            txflags (tfPartialPayment));
        env.close ();

        env.require (owners (alice, 3));
        env.require (balance (alice, USD (1)));
        env.require (balance (alice, EUR (1000)));
        aliceOffers = offersOnAccount (env, alice);
        BEAST_EXPECT(aliceOffers.size () == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == EUR (598.8));
            BEAST_EXPECT(offer[sfTakerPays] == USD (499));
        }
    }

    void
    testSelfPayment2(std::initializer_list<uint256> fs)
    {
        testcase ("Self-payment 2");

        // In this case the difference between the old payment code and
        // the new is the values left behind in the offer.  Not saying either
        // ios ring, they are just different.
        using namespace jtx;

        auto const gw1 = Account ("gw1");
        auto const gw2 = Account ("gw2");
        auto const alice = Account ("alice");
        auto const USD = gw1["USD"];
        auto const EUR = gw2["EUR"];

        Env env (*this, features (fs));

        auto const closeTime =
            fix1141Time () + 100 * env.closed ()->info ().closeTimeResolution;
        env.close (closeTime);

        env.fund (CSC (1000000), gw1, gw2);
        env.close ();

        // The fee that's charged for transactions.
        auto const f = env.current ()->fees ().base;

        env.fund (reserve (env, 3) + f * 4, alice);
        env.close ();

        env (trust (alice, USD (506)));
        env (trust (alice, EUR (606)));
        env.close ();

        env (pay (gw1, alice, USD (500)));
        env (pay (gw2, alice, EUR (600)));
        env.close ();

        env (offer (alice, USD (500), EUR (600)));
        env.close ();

        env.require (owners (alice, 3));
        env.require (balance (alice, USD (500)));
        env.require (balance (alice, EUR (600)));

        auto aliceOffers = offersOnAccount (env, alice);
        BEAST_EXPECT(aliceOffers.size () == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == EUR (600));
            BEAST_EXPECT(offer[sfTakerPays] == USD (500));
        }

        env (pay (alice, alice, EUR (60)), sendmax (USD (50)),
            txflags (tfPartialPayment));
        env.close ();

        env.require (owners (alice, 3));
        env.require (balance (alice, USD (500)));
        env.require (balance (alice, EUR (600)));
        aliceOffers = offersOnAccount (env, alice);
        BEAST_EXPECT(aliceOffers.size () == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == EUR (594));
            BEAST_EXPECT(offer[sfTakerPays] == USD (495));
        }
    }
    void testSelfFundedCSCEndpoint (bool consumeOffer, std::initializer_list<uint256> fs)
    {
        // Test that the deferred credit table is not bypassed for
        // CSCEndpointSteps. If the account in the first step is sending CSC and
        // that account also owns an offer that receives CSC, it should not be
        // possible for that step to use the CSC received in the offer as part
        // of the payment.
        testcase("Self funded CSCEndpoint");

        using namespace jtx;

        Env env(*this, features(fs));

        // Need new behavior from `accountHolds`
        auto const closeTime = fix1141Time() +
            env.closed()->info().closeTimeResolution;
        env.close(closeTime);

        auto const alice = Account("alice");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(CSC(10000), alice, gw);
        env(trust(alice, USD(20)));
        env(pay(gw, alice, USD(10)));
        env(offer(alice, CSC(50000), USD(10)));

        // Consuming the offer changes the owner count, which could also cause
        // liquidity to decrease in the forward pass
        auto const toSend = consumeOffer ? USD(10) : USD(9);
        env(pay(alice, alice, toSend), path(~USD), sendmax(CSC(20000)),
            txflags(tfPartialPayment | tfNoCasinocoinDirect));
    }

    void testUnfundedOffer (bool withFix, std::initializer_list<uint256> fs)
    {
        testcase(std::string("Unfunded Offer ") +
            (withFix ? "with fix" : "without fix"));

        using namespace jtx;
        {
            // Test reverse
            Env env(*this, features(fs));
            auto closeTime = fix1298Time();
            if (withFix)
                closeTime += env.closed()->info().closeTimeResolution;
            else
                closeTime -= env.closed()->info().closeTimeResolution;
            env.close(closeTime);

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const USD = gw["USD"];

            env.fund(CSC(100000), alice, bob, gw);
            env(trust(bob, USD(20)));

            STAmount tinyAmt1{USD.issue(), 9000000000000000ll, -17, false,
                false, STAmount::unchecked{}};
            STAmount tinyAmt3{USD.issue(), 9000000000000003ll, -17, false,
                false, STAmount::unchecked{}};

            env(offer(gw, drops(9000000000), tinyAmt3));
            env(pay(alice, bob, tinyAmt1), path(~USD),
                sendmax(drops(9000000000)), txflags(tfNoCasinocoinDirect));

            if (withFix)
                BEAST_EXPECT(!isOffer(env, gw, CSC(0), USD(0)));
            else
                BEAST_EXPECT(isOffer(env, gw, CSC(0), USD(0)));
        }
        {
            // Test forward
            Env env(*this, features(fs));
            auto closeTime = fix1298Time();
            if (withFix)
                closeTime += env.closed()->info().closeTimeResolution;
            else
                closeTime -= env.closed()->info().closeTimeResolution;
            env.close(closeTime);

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const USD = gw["USD"];

            env.fund(CSC(100000), alice, bob, gw);
            env(trust(alice, USD(20)));

            STAmount tinyAmt1{USD.issue(), 9000000000000000ll, -17, false,
                false, STAmount::unchecked{}};
            STAmount tinyAmt3{USD.issue(), 9000000000000003ll, -17, false,
                false, STAmount::unchecked{}};

            env(pay(gw, alice, tinyAmt1));

            env(offer(gw, tinyAmt3, drops(9000000000)));
            env(pay(alice, bob, drops(9000000000)), path(~CSC),
                sendmax(USD(1)), txflags(tfNoCasinocoinDirect));

            if (withFix)
                BEAST_EXPECT(!isOffer(env, gw, USD(0), CSC(0)));
            else
                BEAST_EXPECT(isOffer(env, gw, USD(0), CSC(0)));
        }
    }

    void
    testReexecuteDirectStep(std::initializer_list<uint256> fs)
    {
        testcase("ReexecuteDirectStep");

        using namespace jtx;
        Env env(*this, features(fs));

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        auto const usdC = USD.currency;

        env.fund(CSC(10000), alice, bob, gw);
        // Need to be past this time to see the bug
        env.close(fix1274Time() +
            100 * env.closed()->info().closeTimeResolution);
        env(trust(alice, USD(100)));
        env.close();

        BEAST_EXPECT(!getnoCasinocoinFlag(env, gw, alice, usdC));

        env(pay(
            gw, alice,
            // 12.55....
            STAmount{USD.issue(), std::uint64_t(1255555555555555ull), -14, false}));

        env(offer(gw,
            // 5.0...
            STAmount{
                USD.issue(), std::uint64_t(5000000000000000ull), -15, false},
            CSC(1000)));

        env(offer(gw,
            // .555...
            STAmount{
                USD.issue(), std::uint64_t(5555555555555555ull), -16, false},
            CSC(10)));

        env(offer(gw,
            // 4.44....
            STAmount{
                USD.issue(), std::uint64_t(4444444444444444ull), -15, false},
            CSC(.1)));

        env(offer(alice,
            // 17
            STAmount{
                USD.issue(), std::uint64_t(1700000000000000ull), -14, false},
            CSC(.001)));

        env(pay(alice, bob, CSC(10000)), path(~CSC), sendmax(USD(100)),
            txflags(tfPartialPayment | tfNoCasinocoinDirect));
    }

    void
    testRIPD1443(bool withFix)
    {
        testcase("ripd1443");

        using namespace jtx;
        Env env(*this, features(featureFlow));
        auto const timeDelta = env.closed ()->info ().closeTimeResolution;
        auto const d = withFix ? timeDelta*100 : -timeDelta*100;
        auto closeTime = fix1443Time() + d;
        env.close(closeTime);

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");

        env.fund(CSC(100000000), alice, noCasinocoin(bob), carol, gw);
        env.trust(gw["USD"](10000), alice, carol);
        env(trust(bob, gw["USD"](10000), tfSetNoCasinocoin));
        env.trust(gw["USD"](10000), bob);
        env.close();

        // set no ripple between bob and the gateway

        env(pay(gw, alice, gw["USD"](1000)));
        env.close();

        env(offer(alice, bob["USD"](1000), CSC(1)));
        env.close();

        env(pay(alice, alice, CSC(1)), path(gw, bob, ~CSC),
            sendmax(gw["USD"](1000)), txflags(tfNoCasinocoinDirect),
            ter(withFix ? tecPATH_DRY : tesSUCCESS));
        env.close();

        if (withFix)
        {
            env.trust(bob["USD"](10000), alice);
            env(pay(bob, alice, bob["USD"](1000)));
        }

        env(offer(alice, CSC(1000), bob["USD"](1000)));
        env.close();

        env(pay (carol, carol, gw["USD"](1000)), path(~bob["USD"], gw),
            sendmax(CSC(100000)), txflags(tfNoCasinocoinDirect),
            ter(withFix ? tecPATH_DRY : tesSUCCESS));
        env.close();

        pass();
    }

    void
    testRIPD1449(bool withFix)
    {
        testcase("ripd1449");

        using namespace jtx;
        Env env(*this, features(featureFlow));
        auto const timeDelta = env.closed ()->info ().closeTimeResolution;
        auto const d = withFix ? timeDelta*100 : -timeDelta*100;
        auto closeTime = fix1449Time() + d;
        env.close(closeTime);

        // pay alice -> csc -> USD/bob -> bob -> gw -> alice
        // set no ripple on bob's side of the bob/gw trust line
        // carol has the bob/USD and makes an offer, bob has USD/gw

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(CSC(100000000), alice, bob, carol, gw);
        env.trust(USD(10000), alice, carol);
        env(trust(bob, USD(10000), tfSetNoCasinocoin));
        env.trust(USD(10000), bob);
        env.trust(bob["USD"](10000), carol);
        env.close();

        env(pay(bob, carol, bob["USD"](1000)));
        env(pay(gw, bob, USD(1000)));
        env.close();

        env(offer(carol, CSC(1), bob["USD"](1000)));
        env.close();

        env(pay(alice, alice, USD(1000)), path(~bob["USD"], bob, gw),
            sendmax(CSC(1)), txflags(tfNoCasinocoinDirect),
            ter(withFix ? tecPATH_DRY : tesSUCCESS));
        env.close();
    }

    void
    testSelfPayLowQualityOffer (std::initializer_list<uint256> fs)
    {
        // The new payment code used to assert if an offer was made for more
        // CSC than the offering account held.  This unit test reproduces
        // that failing case.
        testcase ("Self crossing low quality offer");

        using namespace jtx;

        Env env(*this, features (fs));

        auto const ann = Account("ann");
        auto const gw = Account("gateway");
        auto const CTB = gw["CTB"];

        auto const fee = env.current ()->fees ().base;
        env.fund (reserve(env, 2) + drops (9999640) + (fee), ann);
        env.fund (reserve(env, 2) + (fee*4), gw);
        env.close();

        env (rate(gw, 1.002));
        env (trust(ann, CTB(10)));
        env.close();

        env (pay(gw, ann, CTB(2.856)));
        env.close();

        env (offer(ann, drops(365611702030), CTB(5.713)));
        env.close();

        // This payment caused the assert.
        env (pay(ann, ann, CTB(0.687)),
             sendmax (drops(20000000000)), txflags (tfPartialPayment));
    }

    void
    testEmptyStrand(std::initializer_list<uint256> fs)
    {
        testcase("Empty Strand");
        using namespace jtx;

        auto const alice = Account("alice");

        Env env(*this, features(fs));

        env.fund(CSC(10000), alice);

        env(pay(alice, alice,
                alice["USD"](100)),
            path(~alice["USD"]),
            ter(temBAD_PATH));
    }

    void run() override
    {
        testLimitQuality();
        testRIPD1443(true);
        testRIPD1443(false);
        testRIPD1449(true);
        testRIPD1449(false);

        auto testWithFeats = [this](auto&&... fs)
        {
            testLineQuality({fs...});
            testFalseDry({fs...});
            if (!sizeof...(fs))
                return;
            testDirectStep({fs...});
            testBookStep({fs...});
            testDirectStep({featureOwnerPaysFee, fs...});
            testBookStep({featureOwnerPaysFee, fs...});
            testTransferRate({featureOwnerPaysFee, fs...});
            testSelfPayment1({fs...});
            testSelfPayment2({fs...});
            testSelfFundedCSCEndpoint(false, {fs...});
            testSelfFundedCSCEndpoint(true, {fs...});
            testUnfundedOffer(true, {fs...});
            testUnfundedOffer(false,  {fs...});
            testReexecuteDirectStep({fix1368, fs...});
            testSelfPayLowQualityOffer({fs...});
        };
        testWithFeats();
        testWithFeats(featureFlow);
        testWithFeats(featureFlow, fix1373);
        testWithFeats(featureFlow, fix1373, featureFlowCross);
        testEmptyStrand({featureFlow, fix1373, featureFlowCross});
    }
};

BEAST_DEFINE_TESTSUITE(Flow,app,casinocoin);

} // test
} // casinocoin
