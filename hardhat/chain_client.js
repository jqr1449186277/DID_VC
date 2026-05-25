"use strict";

const ABI = [
  "function records(bytes32) view returns (bytes32 cid, address owner, address recovery, uint64 version, bool active, bytes32 pkNormHash, bytes32 pkRecHash)",
  "function activeRoot() view returns (bytes32)",
  "function rootEpoch() view returns (uint64)",
  "function rootUpdater() view returns (address)",
  "function ttssVkSetHashOf(bytes32) view returns (bytes32)",
  "function ttssMetaHashOf(bytes32) view returns (bytes32)",
  "function ttssMetaVersionOf(bytes32) view returns (uint64)",
  "function ttssMetaEpochOf(bytes32) view returns (uint64)",
  "function lastTraceAccusedSetHashOf(bytes32) view returns (bytes32)",
  "function lastTraceProofHashOf(bytes32) view returns (bytes32)",
  "function lastTraceDigestOf(bytes32) view returns (bytes32)",
  "function lastTraceVersionOf(bytes32) view returns (uint64)",
  "function lastTraceEpochOf(bytes32) view returns (uint64)",
  "function getTTSSMeta(bytes32 idHash) view returns (bytes32 vkSetHash, bytes32 metaHash, uint64 ver, uint64 epoch)",
  "function getTraceAnchor(bytes32 idHash) view returns (bytes32 accusedSetHash, bytes32 proofHash, bytes32 traceDigest, uint64 ver, uint64 epoch)",
  "function setTTSSMeta(bytes32 idHash, uint64 ver, uint64 epoch, bytes32 vkSetHash, bytes32 metaHash)",
  "function publishTraceResult(bytes32 idHash, uint64 ver, uint64 epoch, bytes32 accusedSetHash, bytes32 proofHash, bytes32 traceDigest)",
  "function registerZK(bytes32 idHash, bytes32 cid, bytes32 pkNormHash, bytes32 pkRecHash, address owner, address recovery)",
  "function applyUpdateZK(bytes32 idHash, bytes32 newCid, bytes32 pkNormHash, bytes32 pkRecHash, uint64 newVersion)",
  "function applyRecoveryRotateZK(bytes32 idHash, bytes32 newCid, bytes32 pkNormHash, bytes32 pkRecHash, uint64 newVersion, address newOwner, address newRecovery)",
  "function setActiveRoot(bytes32 newRoot, uint64 newEpoch)",
  "event RegisteredZK(bytes32 indexed idHash, bytes32 cid, bytes32 pkNormHash, bytes32 pkRecHash, address owner, address recovery)",
  "event TTSSMetaUpdated(bytes32 indexed idHash, uint64 indexed ver, uint64 indexed epoch, bytes32 vkSetHash, bytes32 metaHash)",
  "event TraceResultPublished(bytes32 indexed idHash, uint64 indexed ver, uint64 indexed epoch, bytes32 accusedSetHash, bytes32 proofHash, bytes32 traceDigest)",
  "event UpdatedZK(bytes32 indexed idHash, bytes32 newCid, bytes32 pkNormHash, bytes32 pkRecHash, uint64 newVersion)",
  "event RecoveredZK(bytes32 indexed idHash, bytes32 newCid, bytes32 pkNormHash, bytes32 pkRecHash, uint64 newVersion, address newOwner, address newRecovery)",
  "event RootUpdated(bytes32 indexed newRoot, uint64 indexed newEpoch)"
];

function loadEthers() {
  try {
    return require("ethers");
  } catch {
    console.error("[bb_service_zk] missing dependency: ethers. Run: npm i ethers");
    process.exit(1);
  }
}

function parseEthersError(e) {
  if (!e) return "unknown";
  const short = e.shortMessage || e.reason || e.message || String(e);
  return String(short).replace(/\s+/g, " ").trim().slice(0, 320);
}

function createChainClient({ rpcUrl, contractAddress, pollMs, rootUpdaterPrivkey }) {
  const ethersPkg = loadEthers();
  const JsonRpcProvider = ethersPkg.JsonRpcProvider || (ethersPkg.providers && ethersPkg.providers.JsonRpcProvider);
  const WebSocketProvider = ethersPkg.WebSocketProvider || (ethersPkg.providers && ethersPkg.providers.WebSocketProvider);
  const Wallet = ethersPkg.Wallet;
  const Contract = ethersPkg.Contract;
  const getAddress = ethersPkg.getAddress || (ethersPkg.utils && ethersPkg.utils.getAddress);
  const isHexString = ethersPkg.isHexString || (ethersPkg.utils && ethersPkg.utils.isHexString);
  const parseEther = ethersPkg.parseEther || (ethersPkg.utils && ethersPkg.utils.parseEther);
  const idFn = ethersPkg.id || (ethersPkg.utils && ethersPkg.utils.id);

  let provider;
  if (/^wss?:\/\//.test(rpcUrl)) {
    provider = new WebSocketProvider(rpcUrl);
    console.log(`[bb_service_zk] WebSocketProvider ${rpcUrl}`);
  } else {
    provider = new JsonRpcProvider(rpcUrl);
    provider.pollingInterval = pollMs;
    console.log(`[bb_service_zk] JsonRpcProvider ${rpcUrl}, polling=${pollMs}ms`);
  }

  const contract = new Contract(contractAddress, ABI, provider);
  const rootUpdaterWallet = new Wallet(rootUpdaterPrivkey, provider);
  const rootUpdaterContract = contract.connect(rootUpdaterWallet);

  return {
    ethersPkg,
    Wallet,
    getAddress,
    isHexString,
    parseEther,
    idFn,
    provider,
    contract,
    rootUpdaterWallet,
    rootUpdaterContract,
  };
}

module.exports = {
  ABI,
  createChainClient,
  parseEthersError,
};
