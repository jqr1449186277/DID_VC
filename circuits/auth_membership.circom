pragma circom 2.1.9;

include "./common/poseidon_merkle.circom";

/// Anonymous membership authentication circuit.
///
/// Public inputs:
///   - root: anchored active root from DIDBulletinBoardZK / bb_service_zk
///   - ctxHash: application / relying-party context hash
///   - sessPkHash: hash of the ephemeral session public key
///   - epoch: freshness tag or root epoch
///
/// Private witness:
///   - sid, rho: opening of the local identity commitment cid = Poseidon(sid, rho)
///   - pkNormHash, pkRecHash, ver: leaf payload bound into the active root
///   - pathElements, pathIndex: Merkle authentication path under root
///
/// Public outputs:
///   - nullifier: document-aligned nullifier = Poseidon(cid, ctxHash, epoch)
///   - bindHash: exposed only for debugging / transcript inspection
///   - leaf: optional debugging output; can be removed in production if desired
///
/// 与《DID实验设计文档》一致：
///   cid        = Poseidon(sid, rho)
///   leaf       = Poseidon(cid, pkNormHash, pkRecHash, ver, 1)
///   nullifier  = Poseidon(cid, ctxHash, epoch)
///
/// 说明：
///   sessPkHash 仍然作为公开输入进入 statement，用于“电路内绑定 sessPkHash，
///   电路外再做普通签名”的实验思路；但它不再折叠进 nullifier。
template AuthMembership(depth) {
    signal input root;
    signal input ctxHash;
    signal input sessPkHash;
    signal input epoch;

    signal input sid;
    signal input rho;
    signal input pkNormHash;
    signal input pkRecHash;
    signal input ver;
    signal input pathElements[depth];
    signal input pathIndex[depth];

    signal output nullifier;
    signal output bindHash;
    signal output leaf;

    component idc = IdentityCommitment();
    idc.sid <== sid;
    idc.rho <== rho;

    signal cid;
    cid <== idc.cid;

    component leafH = StateLeafHash();
    leafH.cid <== cid;
    leafH.pkNormHash <== pkNormHash;
    leafH.pkRecHash <== pkRecHash;
    leafH.ver <== ver;
    leafH.active <== 1;

    leaf <== leafH.leaf;

    component merkle = PoseidonMerkleProof(depth);
    merkle.leaf <== leaf;
    merkle.root <== root;
    for (var i = 0; i < depth; i++) {
        merkle.pathElements[i] <== pathElements[i];
        merkle.pathIndex[i] <== pathIndex[i];
    }

    // 保留 bindHash 作为调试输出：
    // bindHash = Poseidon(ctxHash, sessPkHash, epoch)
    component bind = SessionBindingHash();
    bind.ctxHash <== ctxHash;
    bind.sessPkHash <== sessPkHash;
    bind.epoch <== epoch;

    bindHash <== bind.bindHash;

    // 按实验设计文档恢复 nullifier 定义：
    // nullifier = Poseidon(cid, ctxHash, epoch)
    component nf = AuthNullifier();
    nf.cid <== cid;
    nf.ctxHash <== ctxHash;
    nf.epoch <== epoch;

    nullifier <== nf.nullifier;
}

/// Default depth = 20 for the local-chain experiment.
component main {public [root, ctxHash, sessPkHash, epoch]} = AuthMembership(20);
