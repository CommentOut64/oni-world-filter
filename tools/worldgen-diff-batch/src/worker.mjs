import fs from "node:fs";
import path from "node:path";
import { parentPort, workerData } from "node:worker_threads";
import { pathToFileURL } from "node:url";

import { parseCanonicalCoordinate, writeJsonFile } from "./common.mjs";
import { compareResults, normalizeExternalResult, normalizeProjectResult } from "./normalize.mjs";
import { createMulberry32, generateRandomCoordinate } from "./random.mjs";
import { SidecarClient } from "./sidecar-client.mjs";

async function loadExternalWorldgen(externalRoot) {
  const moduleUrl = pathToFileURL(path.join(externalRoot, "index.js")).href;
  const mod = await import(moduleUrl);
  const wasmBytes = fs.readFileSync(path.join(externalRoot, "serial", "oni_wasm_bg.wasm"));
  await mod.default({ module_or_path: wasmBytes, threads: "serial" });
  return mod.worldgen;
}

async function fetchProjectPreviews(sidecar, parsed) {
  const primary = await sidecar.preview(parsed.worldType, parsed.seed, parsed.mixing, "primary");
  let secondary = null;
  if (primary.preview?.summary?.hasSecondaryPreview) {
    secondary = await sidecar.preview(parsed.worldType, parsed.seed, parsed.mixing, "secondary");
  }
  return { primary, secondary };
}

async function writeDiffSnapshot(outputDir, index, project, external, diff, error) {
  const snapshotDir = path.join(outputDir, "diffs", `${String(index).padStart(6, "0")}-${project?.coord ?? external?.coord ?? "error"}`);
  if (project) {
    writeJsonFile(path.join(snapshotDir, "project.json"), project);
  }
  if (external) {
    writeJsonFile(path.join(snapshotDir, "external.json"), external);
  }
  writeJsonFile(path.join(snapshotDir, "diff.json"), error ? { error: error.message, diff } : diff);
  return snapshotDir;
}

async function run() {
  const sidecar = new SidecarClient(workerData.sidecarPath);
  const worldgen = await loadExternalWorldgen(workerData.externalRoot);
  const random = createMulberry32(workerData.seed);

  try {
    for (let localIndex = 0; localIndex < workerData.count; localIndex += 1) {
      const globalIndex = workerData.startIndex + localIndex;
      const startedAt = Date.now();
      let spec = null;
      let project = null;
      let external = null;
      let diff = null;

      try {
        spec = generateRandomCoordinate(workerData.catalog, random);
        const parsed = parseCanonicalCoordinate(spec.coord, workerData.catalog.worlds);
        const previews = await fetchProjectPreviews(sidecar, parsed);
        project = normalizeProjectResult(previews, workerData.catalog, spec);
        const externalMap = worldgen.generate(spec.coord);
        external = normalizeExternalResult(externalMap, project, spec);
        worldgen.clear();
        diff = compareResults(project, external);

        let diffPath = null;
        if (!diff.same) {
          diffPath = await writeDiffSnapshot(workerData.outputDir, globalIndex, project, external, diff, null);
        }

        parentPort.postMessage({
          type: "result",
          summary: {
            index: globalIndex,
            coord: spec.coord,
            worldType: spec.worldType,
            worldCode: spec.worldCode,
            category: spec.category,
            seed: spec.seed,
            mixing: spec.mixing,
            mixingLevels: spec.mixingLevels,
            same: diff.same,
            diffKinds: diff.diffKinds,
            elapsedMs: Date.now() - startedAt,
            diffPath,
          },
        });
      } catch (error) {
        const fallbackSpec = spec ?? {
          coord: `worker-${workerData.workerIndex}-task-${globalIndex}`,
          worldType: -1,
          worldCode: "",
          category: "error",
          seed: -1,
          mixing: -1,
          mixingLevels: [],
        };
        const fallbackProject = project ?? {
          source: "project",
          coord: fallbackSpec.coord,
          error: error.message,
        };
        const fallbackExternal = external ?? {
          source: "external",
          coord: fallbackSpec.coord,
          error: error.message,
        };
        diff = diff ?? {
          coord: fallbackSpec.coord,
          same: false,
          diffKinds: ["execution_error"],
        };
        const diffPath = await writeDiffSnapshot(
          workerData.outputDir,
          globalIndex,
          fallbackProject,
          fallbackExternal,
          diff,
          error
        );
        parentPort.postMessage({
          type: "result",
          summary: {
            index: globalIndex,
            coord: fallbackSpec.coord,
            worldType: fallbackSpec.worldType,
            worldCode: fallbackSpec.worldCode,
            category: fallbackSpec.category,
            seed: fallbackSpec.seed,
            mixing: fallbackSpec.mixing,
            mixingLevels: fallbackSpec.mixingLevels,
            same: false,
            diffKinds: ["execution_error"],
            elapsedMs: Date.now() - startedAt,
            diffPath,
            error: error.message,
          },
        });
      }
    }
  } finally {
    await sidecar.close();
  }

  parentPort.postMessage({ type: "done", workerIndex: workerData.workerIndex });
}

run().catch((error) => {
  parentPort.postMessage({
    type: "fatal",
    workerIndex: workerData.workerIndex,
    error: error.message,
  });
});
