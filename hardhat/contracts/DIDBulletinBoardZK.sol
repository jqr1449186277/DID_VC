// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/// @title DIDBulletinBoardZK
/// @notice Local-chain bulletin board for the ZK anonymous-authentication experiment.
/// @dev This contract keeps identity records on-chain and anchors the current active-root.
///      Merkle-tree maintenance stays off-chain in bb_service_zk.js.
contract DIDBulletinBoardZK {
    struct Record {
        bytes32 cid;          // commitment / DID pointer used by the ZK leaf
        address owner;        // normal key controller for regular update transactions
        address recovery;     // recovery controller for recovery-rotate transactions
        uint64 version;       // monotonic version across update / recovery flows
        bool active;          // experiment keeps only active records in the root
        bytes32 pkNormHash;   // field/hash form of the normal public key
        bytes32 pkRecHash;    // field/hash form of the recovery public key
    }

    mapping(bytes32 => Record) public records;
    mapping(bytes32 => bytes32) public ttssVkSetHashOf;
    mapping(bytes32 => bytes32) public ttssMetaHashOf;
    mapping(bytes32 => uint64) public ttssMetaVersionOf;
    mapping(bytes32 => uint64) public ttssMetaEpochOf;
    mapping(bytes32 => bytes32) public lastTraceAccusedSetHashOf;
    mapping(bytes32 => bytes32) public lastTraceProofHashOf;
    mapping(bytes32 => bytes32) public lastTraceDigestOf;
    mapping(bytes32 => uint64) public lastTraceVersionOf;
    mapping(bytes32 => uint64) public lastTraceEpochOf;

    bytes32 public activeRoot;
    uint64 public rootEpoch;
    address public immutable admin;
    address public rootUpdater;

    event RegisteredZK(
        bytes32 indexed idHash,
        bytes32 cid,
        bytes32 pkNormHash,
        bytes32 pkRecHash,
        address owner,
        address recovery
    );

    event UpdatedZK(
        bytes32 indexed idHash,
        bytes32 newCid,
        bytes32 pkNormHash,
        bytes32 pkRecHash,
        uint64 newVersion
    );

    event RecoveredZK(
        bytes32 indexed idHash,
        bytes32 newCid,
        bytes32 pkNormHash,
        bytes32 pkRecHash,
        uint64 newVersion,
        address newOwner,
        address newRecovery
    );

    event RootUpdated(bytes32 indexed newRoot, uint64 indexed newEpoch);
    event RootUpdaterChanged(address indexed oldUpdater, address indexed newUpdater);
    event TTSSMetaUpdated(
        bytes32 indexed idHash,
        uint64 indexed ver,
        uint64 indexed epoch,
        bytes32 vkSetHash,
        bytes32 metaHash
    );
    event TraceResultPublished(
        bytes32 indexed idHash,
        uint64 indexed ver,
        uint64 indexed epoch,
        bytes32 accusedSetHash,
        bytes32 proofHash,
        bytes32 traceDigest
    );

    error AlreadyRegistered();
    error Inactive();
    error NotOwner();
    error NotRecovery();
    error BadAddress();
    error BadVersion();
    error NotRootUpdater();

    constructor(address initialRootUpdater) {
        admin = msg.sender;
        rootUpdater = initialRootUpdater == address(0) ? msg.sender : initialRootUpdater;
    }

    modifier onlyAdmin() {
        require(msg.sender == admin, "not admin");
        _;
    }

    modifier onlyRootUpdater() {
        if (msg.sender != rootUpdater && msg.sender != admin) revert NotRootUpdater();
        _;
    }

    function setRootUpdater(address newUpdater) external onlyAdmin {
        if (newUpdater == address(0)) revert BadAddress();
        address old = rootUpdater;
        rootUpdater = newUpdater;
        emit RootUpdaterChanged(old, newUpdater);
    }

    /// @notice Initial registration with explicit normal / recovery controllers.
    /// @dev Version starts at 0 for compatibility with the existing did-e2e flows.
    function registerZK(
        bytes32 idHash,
        bytes32 cid,
        bytes32 pkNormHash,
        bytes32 pkRecHash,
        address owner,
        address recovery
    ) external {
        Record storage r = records[idHash];
        if (r.recovery != address(0) || r.owner != address(0) || r.active) revert AlreadyRegistered();
        if (owner == address(0) || recovery == address(0)) revert BadAddress();

        records[idHash] = Record({
            cid: cid,
            owner: owner,
            recovery: recovery,
            version: 0,
            active: true,
            pkNormHash: pkNormHash,
            pkRecHash: pkRecHash
        });

        emit RegisteredZK(idHash, cid, pkNormHash, pkRecHash, owner, recovery);
    }

    /// @notice Regular update on the normal-key path.
    /// @dev Owner address remains unchanged on the regular path.
    function applyUpdateZK(
        bytes32 idHash,
        bytes32 newCid,
        bytes32 newPkNormHash,
        bytes32 newPkRecHash,
        uint64 newVersion
    ) external {
        Record storage r = records[idHash];
        if (!r.active) revert Inactive();
        if (msg.sender != r.owner) revert NotOwner();
        if (newVersion != r.version + 1) revert BadVersion();

        r.cid = newCid;
        r.pkNormHash = newPkNormHash;
        r.pkRecHash = newPkRecHash;
        r.version = newVersion;

        emit UpdatedZK(idHash, newCid, newPkNormHash, newPkRecHash, newVersion);
    }

    /// @notice Recovery path that refreshes both the normal controller and the recovery controller.
    /// @dev This is a deliberate refinement over the sketch in the design note: recovery must be able
    ///      to rotate BOTH keys, otherwise it cannot actually reclaim identity control after leakage.
    function applyRecoveryRotateZK(
        bytes32 idHash,
        bytes32 newCid,
        bytes32 newPkNormHash,
        bytes32 newPkRecHash,
        uint64 newVersion,
        address newOwner,
        address newRecovery
    ) external {
        Record storage r = records[idHash];
        if (!r.active) revert Inactive();
        if (msg.sender != r.recovery) revert NotRecovery();
        if (newVersion != r.version + 1) revert BadVersion();
        if (newOwner == address(0) || newRecovery == address(0)) revert BadAddress();

        r.cid = newCid;
        r.pkNormHash = newPkNormHash;
        r.pkRecHash = newPkRecHash;
        r.owner = newOwner;
        r.recovery = newRecovery;
        r.version = newVersion;

        emit RecoveredZK(idHash, newCid, newPkNormHash, newPkRecHash, newVersion, newOwner, newRecovery);
    }

    /// @notice Anchor a new active-root computed off-chain.
    /// @dev The service should monotonically increase rootEpoch. Setting the same root is allowed
    ///      only if the epoch still moves forward, which is useful during debugging / forced refresh.
    function setActiveRoot(bytes32 newRoot, uint64 newEpoch) external onlyRootUpdater {
        if (newEpoch <= rootEpoch) revert BadVersion();
        activeRoot = newRoot;
        rootEpoch = newEpoch;
        emit RootUpdated(newRoot, newEpoch);
    }



    function setTTSSMeta(
        bytes32 idHash,
        uint64 ver,
        uint64 epoch,
        bytes32 vkSetHash,
        bytes32 metaHash
    ) external onlyRootUpdater {
        Record storage r = records[idHash];
        if (!r.active) revert Inactive();
        if (ver != r.version) revert BadVersion();
        if (epoch != rootEpoch) revert BadVersion();
        ttssVkSetHashOf[idHash] = vkSetHash;
        ttssMetaHashOf[idHash] = metaHash;
        ttssMetaVersionOf[idHash] = ver;
        ttssMetaEpochOf[idHash] = epoch;
        emit TTSSMetaUpdated(idHash, ver, epoch, vkSetHash, metaHash);
    }

    function publishTraceResult(
        bytes32 idHash,
        uint64 ver,
        uint64 epoch,
        bytes32 accusedSetHash,
        bytes32 proofHash,
        bytes32 traceDigest
    ) external onlyRootUpdater {
        Record storage r = records[idHash];
        if (!r.active) revert Inactive();
        if (ver != r.version) revert BadVersion();
        if (epoch != rootEpoch) revert BadVersion();
        lastTraceAccusedSetHashOf[idHash] = accusedSetHash;
        lastTraceProofHashOf[idHash] = proofHash;
        lastTraceDigestOf[idHash] = traceDigest;
        lastTraceVersionOf[idHash] = ver;
        lastTraceEpochOf[idHash] = epoch;
        emit TraceResultPublished(idHash, ver, epoch, accusedSetHash, proofHash, traceDigest);
    }

    function getTTSSMeta(bytes32 idHash)
        external
        view
        returns (bytes32 vkSetHash, bytes32 metaHash, uint64 ver, uint64 epoch)
    {
        return (
            ttssVkSetHashOf[idHash],
            ttssMetaHashOf[idHash],
            ttssMetaVersionOf[idHash],
            ttssMetaEpochOf[idHash]
        );
    }

    function getTraceAnchor(bytes32 idHash)
        external
        view
        returns (
            bytes32 accusedSetHash,
            bytes32 proofHash,
            bytes32 traceDigest,
            uint64 ver,
            uint64 epoch
        )
    {
        return (
            lastTraceAccusedSetHashOf[idHash],
            lastTraceProofHashOf[idHash],
            lastTraceDigestOf[idHash],
            lastTraceVersionOf[idHash],
            lastTraceEpochOf[idHash]
        );
    }

    /// @notice Lightweight helper used by the service/debug tooling to inspect the current record version.
    function currentVersionOf(bytes32 idHash) external view returns (uint64) {
        return records[idHash].version;
    }

    function getRecord(bytes32 idHash)
        external
        view
        returns (
            bytes32 cid,
            address owner,
            address recovery,
            uint64 version,
            bool active,
            bytes32 pkNormHash,
            bytes32 pkRecHash
        )
    {
        Record storage r = records[idHash];
        return (r.cid, r.owner, r.recovery, r.version, r.active, r.pkNormHash, r.pkRecHash);
    }
}
