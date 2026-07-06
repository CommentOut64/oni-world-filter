import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { Worker } from "node:worker_threads";

import { ensureDir, parseCliArgs, timestampForDir, writeJsonFile } from "./common.mjs";
import { loadCatalogFromSidecar } from "./sidecar-client.mjs";

function splitWork(total, workers) {
  const chunks = [];
  let startIndex = 0;
  const base = Math.floor(total / workers);
  let remainder = total % workers;
  for (let workerIndex = 0; workerIndex < workers; workerIndex += 1) {
    const count = base + (remainder > 0 ? 1 : 0);
    if (remainder > 0) {
      remainder -= 1;
    }
    chunks.push({ workerIndex, startIndex, count });
    startIndex += count;
  }
  return chunks.filter((chunk) => chunk.count > 0);
}

async function main() {
  const options = parseCliArgs(process.argv.slice(2));
  if (!fs.existsSync(options.sidecarPath)) {
    throw new Error(`未找到 sidecar: ${options.sidecarPath}`);
  }
  if (!fs.existsSync(options.externalRoot)) {
    throw new Error(`未找到外部 worldgen 包目录: ${options.externalRoot}`);
  }

  ensureDir(options.outputDir);
  ensureDir(path.join(options.outputDir, "diffs"));

  const rawCatalog = await loadCatalogFromSidecar(options.sidecarPath);
  writeJsonFile(path.join(options.outputDir, "catalog.json"), rawCatalog);

  const workerCount = Math.max(
    1,
    Math.min(options.maxCoords, options.workers > 0 ? options.workers : os.availableParallelism())
  );

  const summaryPath = path.join(options.outputDir, "summary.ndjson");
  const summaryStream = fs.createWriteStream(summaryPath, { encoding: "utf8" });
  const startedAt = Date.now();
  const aggregate = {
    maxCoords: options.maxCoords,
    workers: workerCount,
    seed: options.seed,
    outputDir: options.outputDir,
    sidecarPath: options.sidecarPath,
    externalRoot: options.externalRoot,
    sameCount: 0,
    diffCount: 0,
    errorCount: 0,
  };

  console.log(`输出目录: ${options.outputDir}`);
  console.log(`worker 数: ${workerCount}`);
  console.log(`最大坐标数: ${options.maxCoords}`);

  const chunks = splitWork(options.maxCoords, workerCount);
  const completions = chunks.map((chunk) => {
    return new Promise((resolve, reject) => {
      const worker = new Worker(new URL("./worker.mjs", import.meta.url), {
        workerData: {
          ...chunk,
          seed: options.seed + chunk.workerIndex * 9973,
          sidecarPath: options.sidecarPath,
          externalRoot: options.externalRoot,
          outputDir: options.outputDir,
          catalog: rawCatalog,
        },
      });

      worker.on("message", (message) => {
        if (message.type === "result") {
          summaryStream.write(`${JSON.stringify(message.summary)}\n`);
          if (message.summary.same) {
            aggregate.sameCount += 1;
          } else {
            aggregate.diffCount += 1;
          }
          if (message.summary.error) {
            aggregate.errorCount += 1;
          }
          const processed = aggregate.sameCount + aggregate.diffCount;
          console.log(
            `[${processed}/${options.maxCoords}] ${message.summary.coord} ${message.summary.same ? "SAME" : "DIFF"} ${
              message.summary.diffKinds.join(",") || "-"
            }`
          );
          return;
        }
        if (message.type === "fatal") {
          reject(new Error(`worker#${message.workerIndex} fatal: ${message.error}`));
          return;
        }
        if (message.type === "done") {
          resolve();
        }
      });

      worker.once("error", reject);
      worker.once("exit", (code) => {
        if (code !== 0) {
          reject(new Error(`worker#${chunk.workerIndex} 退出码异常: ${code}`));
        }
      });
    });
  });

  try {
    await Promise.all(completions);
  } finally {
    summaryStream.end();
  }

  aggregate.elapsedMs = Date.now() - startedAt;
  aggregate.finishedAt = timestampForDir(new Date());
  writeJsonFile(path.join(options.outputDir, "summary.json"), aggregate);

  console.log("完成统计:");
  console.log(JSON.stringify(aggregate, null, 2));
}

main().catch((error) => {
  console.error(error.message);
  process.exitCode = 1;
});
