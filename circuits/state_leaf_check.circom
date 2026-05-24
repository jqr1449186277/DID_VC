pragma circom 2.1.9;

include "./common/poseidon_merkle.circom";

/// Debug / unit-test circuit.
/// Purpose:
///   1. Check the external code and circuit compute the same cid.
///   2. Check the external code and circuit compute the same leaf.
/// This circuit is intentionally small and should be the first one compiled
/// and tested before AuthMembership.
template StateLeafCheck() {
    signal input sid;
    signal input rho;
    signal input pkNormHash;
    signal input pkRecHash;
    signal input ver;
    signal input active;

    signal output cid;
    signal output leaf;

    component idc = IdentityCommitment();
    idc.sid <== sid;
    idc.rho <== rho;
    cid <== idc.cid;

    component leafH = StateLeafHash();
    leafH.cid <== cid;
    leafH.pkNormHash <== pkNormHash;
    leafH.pkRecHash <== pkRecHash;
    leafH.ver <== ver;
    leafH.active <== active;
    leaf <== leafH.leaf;
}

component main {public [sid, rho, pkNormHash, pkRecHash, ver, active]} = StateLeafCheck();
