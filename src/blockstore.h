#ifndef BITCOIN_BLOCKSTORE_H
#define BITCOIN_BLOCKSTORE_H

// This API is considered stable ONLY for existing bitcoin codebases,
// any futher uses are not yet supported.
// This API is subject to change dramatically overnight, do not
// depend on it for anything.

#include <boost/signals2/signal.hpp>
#include <queue>

#include "sync.h"
#include "uint256.h"

class CBlock;
class CTxDB;
class CBlockIndex;
class CHub;

class CBlockStoreSignalTable
{
public:
    CCriticalSection cs_sigCommitBlock;
    boost::signals2::signal<void (const CBlock&)> sigCommitBlock;

    CCriticalSection cs_sigAskForBlocks;
    boost::signals2::signal<void (const uint256, const uint256)> sigAskForBlocks;
};

class CBlockStore
{
private:
    CBlockStoreSignalTable sigtable;

    void CallbackCommitBlock(const CBlock &block) { LOCK(sigtable.cs_sigCommitBlock); sigtable.sigCommitBlock(block); }

    void CallbackAskForBlocks(const uint256 hashEnd, const uint256 hashOriginator)  { LOCK(sigtable.cs_sigAskForBlocks); sigtable.sigAskForBlocks(hashEnd, hashOriginator); }

    bool Reorganize(CTxDB& txdb, CBlockIndex* pindexNew);
    bool DisconnectBlock(CBlock& block, CTxDB& txdb, CBlockIndex* pindex);
public:  // HACK to merge with CheckNewBlock
    bool ConnectBlock(CBlock& block, CTxDB& txdb, CBlockIndex* pindex, bool fJustCheck=false);
private:
    bool SetBestChainInner(CBlock& block, CTxDB& txdb, CBlockIndex *pindexNew);
    bool SetBestChain(CBlock& block, CTxDB& txdb, CBlockIndex* pindexNew);
    bool AddToBlockIndex(CBlock& block, unsigned int nFile, unsigned int nBlockPos);
    bool AcceptBlock(CBlock& block);
public:
    bool LoadBlockIndex(bool fReadOnly=false);

//Register methods
    // Register a handler (of the form void f(const CBlock& block)) to be called after every block commit
    void RegisterCommitBlock(boost::function<void (const CBlock&)> func) { LOCK(sigtable.cs_sigCommitBlock); sigtable.sigCommitBlock.connect(func); }

    // Register a handler (of the form void f(const uint256 hashEnd, const uint256 hashOriginator)) to be called when we need to ask for blocks up to hashEnd
    //   Should always start from the best block (GetBestBlockIndex())
    //   The receiver should check if it has a peer which is known to have a block with hash hashOriginator and if it does, it should
    //    send the block query to that node.
    void RegisterAskForBlocks(boost::function<void (const uint256, const uint256)> func) { LOCK(sigtable.cs_sigAskForBlocks); sigtable.sigAskForBlocks.connect(func); }

//Blockchain access methods
    // Emit methods will verify the object, commit it to memory/disk and then place it in queue to
    //   be handled by listeners
    bool EmitBlock(CBlock& block);
};

#endif
