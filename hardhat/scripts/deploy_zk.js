const fs = require("fs");
const path = require("path");
const hre = require("hardhat");

async function main() {
  const { ethers } = hre;

  const CONTRACT_FQN =
    process.env.DIDZK_CONTRACT_FQN ||
    "contracts/DIDBulletinBoardZK.sol:DIDBulletinBoardZK";

  const [deployer] = await ethers.getSigners();

  const rootUpdater =
    process.env.DIDZK_ROOT_UPDATER && process.env.DIDZK_ROOT_UPDATER.trim() !== ""
      ? process.env.DIDZK_ROOT_UPDATER.trim()
      : deployer.address;

  console.log(`Deployer: ${deployer.address}`);
  console.log(`RootUpdater: ${rootUpdater}`);
  console.log(`ContractFQN: ${CONTRACT_FQN}`);

  const Factory = await ethers.getContractFactory(CONTRACT_FQN);
  const contract = await Factory.deploy(rootUpdater);
  await contract.waitForDeployment();

  const address = await contract.getAddress();
  const network = await ethers.provider.getNetwork();

  const deployment = {
    contractFqn: CONTRACT_FQN,
    contractName: "DIDBulletinBoardZK",
    address,
    deployer: deployer.address,
    rootUpdater,
    chainId: Number(network.chainId),
    deployedAt: new Date().toISOString(),
    txHash: contract.deploymentTransaction()
      ? contract.deploymentTransaction().hash
      : null,
  };

  // Primary human-readable output
  console.log(`DIDBulletinBoardZK deployed to: ${address}`);

  // IMPORTANT: start_local_stack_with_verifier.sh parses exactly this line:
  //   DIDBulletinBoardZK = 0x...
  console.log(`DIDBulletinBoardZK = ${address}`);

  // Additional compatibility lines
  console.log(`DIDBulletinBoardZK=${address}`);
  console.log(`DIDBulletinBoardZK_ADDRESS=${address}`);
  console.log(`CONTRACT_ADDRESS=${address}`);
  console.log(`DIDZK_CONTRACT_ADDRESS=${address}`);
  console.log(`Address: ${address}`);
  console.log(`DIDZK_ROOT_UPDATER=${rootUpdater}`);
  console.log(`DIDZK_CONTRACT_FQN=${CONTRACT_FQN}`);

  const outFile =
    process.env.DIDZK_DEPLOY_OUT && process.env.DIDZK_DEPLOY_OUT.trim() !== ""
      ? process.env.DIDZK_DEPLOY_OUT.trim()
      : null;

  if (outFile) {
    fs.mkdirSync(path.dirname(outFile), { recursive: true });
    fs.writeFileSync(outFile, JSON.stringify(deployment, null, 2));
    console.log(`Wrote deployment JSON: ${outFile}`);
  }

  // Final compact JSON line for optional tooling
  console.log(JSON.stringify(deployment));
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
