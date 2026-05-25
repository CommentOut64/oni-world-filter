import fs from "node:fs/promises";
import path from "node:path";
import process from "node:process";

import { parseDocument } from "yaml";

function fail(message) {
  throw new Error(message);
}

function parseArgs(argv) {
  const result = {
    cleanTarget: false,
  };

  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    switch (arg) {
      case "--source":
        result.source = argv[++index];
        break;
      case "--target":
        result.target = argv[++index];
        break;
      case "--asubworlds":
        result.asubworlds = argv[++index];
        break;
      case "--clean-target":
        result.cleanTarget = true;
        break;
      default:
        fail(`未知参数: ${arg}`);
    }
  }

  if (!result.source || !result.target || !result.asubworlds) {
    fail("用法: node scripts/import-dlc5-assets.mjs --source <dlc5-root> --target <asset/dlc/dlc5> --asubworlds <asset/Asubworlds.json> [--clean-target]");
  }

  return result;
}

async function exists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

async function* walkFiles(rootPath) {
  const entries = await fs.readdir(rootPath, { withFileTypes: true });
  entries.sort((left, right) => left.name.localeCompare(right.name));
  for (const entry of entries) {
    const fullPath = path.join(rootPath, entry.name);
    if (entry.isDirectory()) {
      yield* walkFiles(fullPath);
      continue;
    }
    if (entry.isFile()) {
      yield fullPath;
    }
  }
}

function toPosixRelative(rootPath, fullPath) {
  return path.relative(rootPath, fullPath).split(path.sep).join("/");
}

function convertYamlToJsonText(rawYaml, sourcePath) {
  const document = parseDocument(rawYaml, {
    merge: true,
    prettyErrors: true,
    uniqueKeys: false,
  });

  if (document.errors.length > 0) {
    const details = document.errors.map((error) => error.message).join("; ");
    fail(`YAML 解析失败: ${sourcePath} -> ${details}`);
  }

  const payload = document.toJS({
    mapAsMap: false,
    maxAliasCount: 10000,
  });
  return `${JSON.stringify(payload, null, 2)}\n`;
}

async function convertTree(sourceRoot, targetRoot, cleanTarget) {
  if (!(await exists(sourceRoot))) {
    fail(`DLC5 源目录不存在: ${sourceRoot}`);
  }

  if (cleanTarget) {
    await fs.rm(targetRoot, { recursive: true, force: true });
  }
  await fs.mkdir(targetRoot, { recursive: true });

  const summary = {
    yamlFileCount: 0,
    jsonFileCount: 0,
    converted: [],
    subworldIds: [],
  };

  for await (const sourcePath of walkFiles(sourceRoot)) {
    if (!sourcePath.toLowerCase().endsWith(".yaml")) {
      continue;
    }

    const relativePath = toPosixRelative(sourceRoot, sourcePath);
    const targetRelativePath = relativePath.replace(/\.yaml$/i, ".json");
    const targetPath = path.join(targetRoot, ...targetRelativePath.split("/"));
    const rawYaml = await fs.readFile(sourcePath, "utf8");
    const jsonText = convertYamlToJsonText(rawYaml, sourcePath);

    await fs.mkdir(path.dirname(targetPath), { recursive: true });
    await fs.writeFile(targetPath, jsonText, "utf8");

    summary.yamlFileCount += 1;
    summary.jsonFileCount += 1;
    summary.converted.push(targetRelativePath);

    if (relativePath.startsWith("worldgen/subworlds/")) {
      const subworldId = `dlc5::${relativePath.slice("worldgen/".length).replace(/\.yaml$/i, "")}`;
      summary.subworldIds.push(subworldId);
    }
  }

  summary.subworldIds.sort((left, right) => left.localeCompare(right));
  return summary;
}

function mergeUniqueEntries(targetList, entries) {
  const existing = new Set(targetList);
  for (const entry of entries) {
    if (existing.has(entry)) {
      continue;
    }
    targetList.push(entry);
    existing.add(entry);
  }
}

async function updateAsubworlds(asubworldsPath, dlc5SubworldIds) {
  const raw = await fs.readFile(asubworldsPath, "utf8");
  const data = JSON.parse(raw);
  if (!Array.isArray(data.VANILLA) || !Array.isArray(data.SPACEOUT)) {
    fail(`Asubworlds.json 结构异常: ${asubworldsPath}`);
  }

  mergeUniqueEntries(data.VANILLA, dlc5SubworldIds);
  mergeUniqueEntries(data.SPACEOUT, dlc5SubworldIds);
  await fs.writeFile(asubworldsPath, `${JSON.stringify(data, null, 2)}\n`, "utf8");

  return {
    vanillaCount: data.VANILLA.length,
    spaceoutCount: data.SPACEOUT.length,
    injectedCount: dlc5SubworldIds.length,
  };
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const conversion = await convertTree(options.source, options.target, options.cleanTarget);
  const asubworlds = await updateAsubworlds(options.asubworlds, conversion.subworldIds);

  const summary = {
    source: path.resolve(options.source),
    target: path.resolve(options.target),
    asubworlds: path.resolve(options.asubworlds),
    yamlFileCount: conversion.yamlFileCount,
    jsonFileCount: conversion.jsonFileCount,
    dlc5SubworldCount: conversion.subworldIds.length,
    vanillaCount: asubworlds.vanillaCount,
    spaceoutCount: asubworlds.spaceoutCount,
  };

  console.log(JSON.stringify(summary, null, 2));
}

main().catch((error) => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
});
