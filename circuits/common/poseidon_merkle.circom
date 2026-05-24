pragma circom 2.1.6;

include "circomlib/circuits/poseidon.circom";
include "circomlib/circuits/bitify.circom";

/*
 * ForceBool
 * 约束输入必须为布尔位 {0,1}
 */
template ForceBool() {
    signal input in;
    in * (in - 1) === 0;
}

/*
 * RangeCheck64
 * 约束输入落在 [0, 2^64)
 */
template RangeCheck64() {
    signal input in;

    component n2b = Num2Bits(64);
    n2b.in <== in;
}

/*
 * IdentityCommitment
 * cid = Poseidon(sid, rho)
 */
template IdentityCommitment() {
    signal input sid;
    signal input rho;
    signal output cid;

    component h = Poseidon(2);
    h.inputs[0] <== sid;
    h.inputs[1] <== rho;

    cid <== h.out;
}

/*
 * StateLeafHash
 * leaf = Poseidon(cid, pkNormHash, pkRecHash, ver, active)
 *
 * 与实验设计文档一致：
 *   cid        : 身份承诺
 *   pkNormHash : 正常公钥哈希（字段元素）
 *   pkRecHash  : 恢复公钥哈希（字段元素）
 *   ver        : 版本号（64 bit）
 *   active     : 活跃标志（布尔）
 */
template StateLeafHash() {
    signal input cid;
    signal input pkNormHash;
    signal input pkRecHash;
    signal input ver;
    signal input active;

    signal output leaf;

    component rcVer = RangeCheck64();
    rcVer.in <== ver;

    component fb = ForceBool();
    fb.in <== active;

    component h = Poseidon(5);
    h.inputs[0] <== cid;
    h.inputs[1] <== pkNormHash;
    h.inputs[2] <== pkRecHash;
    h.inputs[3] <== ver;
    h.inputs[4] <== active;

    leaf <== h.out;
}

/*
 * SessionBindingHash
 * bindHash = Poseidon(ctxHash, sessPkHash, epoch)
 *
 * 作用：
 *   将认证证明绑定到当前会话上下文和会话公钥。
 *
 * 注意：
 *   按实验设计文档，bindHash 只是“会话绑定思路”的一个中间量，
 *   nullifier 不应定义为 Poseidon(cid, bindHash)，
 *   而应单独回到文档口径：Poseidon(cid, ctxHash, epoch)。
 */
template SessionBindingHash() {
    signal input ctxHash;
    signal input sessPkHash;
    signal input epoch;

    signal output bindHash;

    component rcEpoch = RangeCheck64();
    rcEpoch.in <== epoch;

    component h = Poseidon(3);
    h.inputs[0] <== ctxHash;
    h.inputs[1] <== sessPkHash;
    h.inputs[2] <== epoch;

    bindHash <== h.out;
}

/*
 * AuthNullifier
 * nullifier = Poseidon(cid, ctxHash, epoch)
 *
 * 与《DID实验设计文档》10.2 节模板一致：
 *   可选公开输出：
 *     nullifier = Poseidon(Cid, ctxHash, epoch)
 *
 * 说明：
 *   - nullifier 绑定“身份承诺 + 上下文 + epoch”
 *   - sessPkHash 仍作为公开输入进入证明 statement，
 *     但不再额外折叠进 nullifier
 */
template AuthNullifier() {
    signal input cid;
    signal input ctxHash;
    signal input epoch;
    signal output nullifier;

    component rcEpoch = RangeCheck64();
    rcEpoch.in <== epoch;

    component h = Poseidon(3);
    h.inputs[0] <== cid;
    h.inputs[1] <== ctxHash;
    h.inputs[2] <== epoch;

    nullifier <== h.out;
}

/*
 * PoseidonMerkleProof(depth)
 *
 * 输入：
 *   leaf              : 叶子
 *   root              : 根
 *   pathElements[i]   : 第 i 层兄弟节点
 *   pathIndex[i]      : 第 i 层方向位
 *                       约定：
 *                         0 -> 当前节点在左，兄弟在右
 *                         1 -> 当前节点在右，兄弟在左
 *
 * 设计目标：
 *   用 Poseidon(2) 自底向上重建根。
 *
 * 注意：
 *   这里显式避免了如下不允许的非二次写法：
 *     (1-b)*a + b*c
 *   改用一次乘法的 selector 形式：
 *     delta = b * (sib - cur)
 *     left  = cur + delta
 *     right = sib - delta
 *
 * 正确性：
 *   若 b=0:
 *     delta=0, left=cur, right=sib
 *   若 b=1:
 *     delta=sib-cur, left=sib, right=cur
 */
template PoseidonMerkleProof(depth) {
    signal input leaf;
    signal input root;
    signal input pathElements[depth];
    signal input pathIndex[depth];

    signal output computedRoot;

    signal hashes[depth + 1];
    signal swapDelta[depth];

    component levelHash[depth];
    component idxBool[depth];

    hashes[0] <== leaf;

    for (var i = 0; i < depth; i++) {
        idxBool[i] = ForceBool();
        idxBool[i].in <== pathIndex[i];

        /*
         * delta = pathIndex[i] * (pathElements[i] - hashes[i])
         *
         * pathIndex = 0:
         *   delta = 0
         *   left  = hashes[i]
         *   right = pathElements[i]
         *
         * pathIndex = 1:
         *   delta = pathElements[i] - hashes[i]
         *   left  = pathElements[i]
         *   right = hashes[i]
         */
        swapDelta[i] <== pathIndex[i] * (pathElements[i] - hashes[i]);

        levelHash[i] = Poseidon(2);
        levelHash[i].inputs[0] <== hashes[i] + swapDelta[i];
        levelHash[i].inputs[1] <== pathElements[i] - swapDelta[i];

        hashes[i + 1] <== levelHash[i].out;
    }

    computedRoot <== hashes[depth];
    computedRoot === root;
}