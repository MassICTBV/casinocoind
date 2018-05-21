//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/casinocoin/casinocoind
    Copyright (c) 2018 CasinoCoin Foundation

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

//==============================================================================
/*
    2018-05-15  jrojek          Created
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/ledger/OpenLedger.h>
#include <casinocoin/app/main/Application.h>
#include <casinocoin/app/misc/NetworkOPs.h>
#include <casinocoin/app/misc/CRNPerformance.h>
#include <casinocoin/app/misc/Transaction.h>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <mutex>

namespace casinocoin {

class CRNPerformanceImpl final
    : public CRNPerformance
{
public:
    CRNPerformanceImpl (
        NetworkOPs& networkOps,
        beast::Journal journal);

    Json::Value getJson () override;
    void submit (std::shared_ptr<ReadView const> const& lastClosedLedger,
                 SecretKey const& crnSecret,
                Application& app) override;

protected:
    NetworkOPs& networkOps;
    beast::Journal j_;
};

CRNPerformanceImpl::CRNPerformanceImpl(
        NetworkOPs& networkOps,
        beast::Journal journal)
    : networkOps(networkOps)
    , j_(journal)
{

}

Json::Value CRNPerformanceImpl::getJson()
{
    Json::Value ret;
    return ret;
}

void CRNPerformanceImpl::submit(std::shared_ptr<ReadView const> const& lastClosedLedger, SecretKey const& crnSecret, Application& app)
{
    // LCL must be flag ledger
//    assert ((lastClosedLedger->info().seq % 256) == 0);
    auto const ledgerSeq = lastClosedLedger->info().seq + 1;

    std::array<NetworkOPs::StateAccounting::Counters, 5> counters = networkOps.getServerAccountingInfo();
    std::uint32_t latencyAvg = 123;


    PublicKey crnPubKey = derivePublicKey(KeyType::secp256k1, crnSecret);
    AccountID crnAccountID = calcAccountID(crnPubKey);

    JLOG(j_.info()) << "preparing CRNReport tx for account " << toBase58(crnAccountID);

    std::shared_ptr<SLE const> sle = cachedRead(*lastClosedLedger,
        keylet::account(crnAccountID).key, ltACCOUNT_ROOT);
    if (!sle)
    {
        JLOG(j_.warn()) << "cannot get account " << toBase58(crnAccountID) << " for CRNReport :(";
        return;
    }
    auto accSeq = (*sle)[sfSequence];
    auto const queued = app.getTxQ().getAccountTxs(crnAccountID,
        *lastClosedLedger);
    // If the account has any txs in the TxQ, skip those sequence
    // numbers (accounting for possible gaps).
    for(auto const& tx : queued)
    {
        if (tx.first == accSeq)
            ++accSeq;
        else if (tx.first > accSeq)
            break;
    }

    std::shared_ptr<STTx> crnReportTx = std::make_shared<STTx>(ttCRN_REPORT,
        [crnAccountID,crnPubKey,ledgerSeq,accSeq,counters,latencyAvg](auto& obj)
        {
            obj[sfAccount] = crnAccountID;
            obj[sfFee] = beast::zero;
            obj[sfLedgerSequence] = ledgerSeq;
            obj[sfSequence] = accSeq;
            obj.setFieldVL(sfSigningPubKey, crnPubKey.slice());
//            obj[sfCRN_IPAddress] = STBlob();
//            obj[sfCRN_DomainName] = STBlob();
            obj[sfCRN_LatencyAvg] = latencyAvg;

            STArray connStats (sfCRN_ConnectionStats);
            for (NetworkOPs::StateAccounting::Counters const& counter : counters)
            {
                connStats.push_back(STObject(sfCRN_ConnectionStat));
                auto& entry = connStats.back();
                entry.emplace_back (STUInt32 (sfTransitions, counter.transitions));
                entry.emplace_back (STUInt32 (sfTime, (counter.dur.count() / 1000 / 1000)/*seconds precision is enough*/));
            }
            obj.setFieldArray(sfCRN_ConnectionStats, connStats);
        });

    crnReportTx->sign(crnPubKey, crnSecret);
    uint256 txID = crnReportTx->getTransactionID ();

    Transaction::pointer tpTrans;
    {
        std::string reason;
        tpTrans = std::make_shared<Transaction>(
            crnReportTx, reason, app);
        if (tpTrans->getStatus () != NEW)
        {
            JLOG(j_.error()) <<
                "Unable to construct transaction: " << reason;
            return;
        }
    }

    JLOG(j_.info()) <<
        "CRNReport tx for account: " << toBase58(crnAccountID) << "sent to processing with txID: " << txID;

    networkOps.processTransaction(tpTrans, true, true, NetworkOPs::FailHard::no);
}


std::unique_ptr<CRNPerformance> make_CRNPerformance(
    NetworkOPs& networkOps,
    beast::Journal journal)
{
    return std::make_unique<CRNPerformanceImpl> (
                networkOps,
                journal);
}

}