import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const ROLE_PRIMARY = "primary";
export const ROLE_SECONDARY = "secondary";

export const ROOT_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
export const DEFAULT_SIDECAR_PATH = path.join(ROOT_DIR, "src-tauri", "binaries", "oni-sidecar.exe");
export const DEFAULT_EXTERNAL_ROOT = path.join(
  ROOT_DIR,
  "llmdoc",
  "external",
  "oxygen-not-included-worldgen",
  "node_modules",
  "@tigin-backwards",
  "oxygen-not-included-worldgen"
);

export function parseCliArgs(argv) {
  const options = {
    maxCoords: 100,
    workers: 0,
    seed: 20260705,
    outputDir: "",
    sidecarPath: DEFAULT_SIDECAR_PATH,
    externalRoot: DEFAULT_EXTERNAL_ROOT,
  };

  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    const next = argv[index + 1];
    switch (arg) {
      case "--max-coords":
        options.maxCoords = Number.parseInt(next, 10);
        index += 1;
        break;
      case "--workers":
        options.workers = Number.parseInt(next, 10);
        index += 1;
        break;
      case "--seed":
        options.seed = Number.parseInt(next, 10);
        index += 1;
        break;
      case "--output-dir":
        options.outputDir = next;
        index += 1;
        break;
      case "--sidecar":
        options.sidecarPath = path.resolve(next);
        index += 1;
        break;
      case "--external-root":
        options.externalRoot = path.resolve(next);
        index += 1;
        break;
      default:
        throw new Error(`未知参数: ${arg}`);
    }
  }

  if (!Number.isInteger(options.maxCoords) || options.maxCoords <= 0) {
    throw new Error("maxCoords 必须是大于 0 的整数");
  }
  if (!Number.isInteger(options.workers) || options.workers < 0) {
    throw new Error("workers 必须是大于等于 0 的整数");
  }
  if (!Number.isInteger(options.seed) || options.seed < 0) {
    throw new Error("seed 必须是大于等于 0 的整数");
  }

  options.outputDir = options.outputDir
    ? path.resolve(options.outputDir)
    : path.join(path.resolve(ROOT_DIR, "tools", "worldgen-diff-batch"), "runs", timestampForDir(new Date()));
  return options;
}

export function timestampForDir(date) {
  const pad = (value) => String(value).padStart(2, "0");
  return [
    date.getFullYear(),
    pad(date.getMonth() + 1),
    pad(date.getDate()),
  ].join("") + "-" + [
    pad(date.getHours()),
    pad(date.getMinutes()),
    pad(date.getSeconds()),
  ].join("");
}

export function ensureDir(dirPath) {
  fs.mkdirSync(dirPath, { recursive: true });
}

export function writeJsonFile(filePath, value) {
  ensureDir(path.dirname(filePath));
  fs.writeFileSync(filePath, `${JSON.stringify(value, null, 2)}\n`, "utf8");
}

export function classifyWorldCode(code) {
  if (code.startsWith("V-") && code.includes("-C-")) {
    return "classicCluster";
  }
  if (code.includes("-C-")) {
    return "moonletCluster";
  }
  return "baseAsteroid";
}

export function groupWorldsByCategory(worlds) {
  const groups = {
    baseAsteroid: [],
    classicCluster: [],
    moonletCluster: [],
  };
  for (const world of worlds) {
    groups[classifyWorldCode(world.code)].push(world);
  }
  return groups;
}

export function groupMixingSlots(mixingSlots) {
  const groups = [];
  let current = null;
  for (const slot of [...mixingSlots].sort((left, right) => left.slot - right.slot)) {
    if (slot.type === "dlc" || current === null) {
      current = { packageSlot: slot, children: [] };
      groups.push(current);
      continue;
    }
    current.children.push(slot);
  }
  return groups;
}

export function sanitizeMixingLevelsForWorld(levels, worldCode) {
  const next = [...levels];
  const disableRange = (start, endExclusive) => {
    for (let index = start; index < endExclusive && index < next.length; index += 1) {
      next[index] = 0;
    }
  };

  if (worldCode.includes("CER")) {
    disableRange(0, 5);
  } else if (worldCode.includes("PRE")) {
    disableRange(6, 11);
  } else if (worldCode.includes("AQU")) {
    disableRange(11, 17);
  }

  return next;
}

export function encodeMixingLevels(levels) {
  let value = 0n;
  for (const level of levels) {
    value = value * 5n + BigInt(level);
  }
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) {
    throw new Error("mixing 超出 JavaScript 安全整数范围");
  }
  return Number(value);
}

export function toLittleEndianBase36(input) {
  if (input === 0) {
    return "0";
  }
  const dict = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  let value = input;
  let result = "";
  while (value > 0) {
    result += dict[value % 36];
    value = Math.floor(value / 36);
  }
  return result;
}

export function fromLittleEndianBase36(input) {
  const dict = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  let value = 0;
  for (let index = input.length - 1; index >= 0; index -= 1) {
    const digit = dict.indexOf(input[index]);
    if (digit < 0) {
      throw new Error(`非法 base36 mixing 字符: ${input[index]}`);
    }
    value = value * 36 + digit;
  }
  return value;
}

export function buildCanonicalCoordinate(prefix, seed, mixing) {
  return `${prefix}${seed}-0-D3-${toLittleEndianBase36(mixing)}`;
}

export function parseCanonicalCoordinate(coord, worlds) {
  const sortedWorlds = [...worlds].sort((left, right) => right.code.length - left.code.length);
  const matched = sortedWorlds.find((world) => coord.startsWith(world.code));
  if (!matched) {
    throw new Error(`无法解析坐标前缀: ${coord}`);
  }
  const rest = coord.slice(matched.code.length);
  const match = /^(\d+)-0-D3-([0-9A-Z]+)$/.exec(rest);
  if (!match) {
    throw new Error(`不支持的 canonical 坐标格式: ${coord}`);
  }
  return {
    coord,
    worldType: matched.id,
    worldCode: matched.code,
    seed: Number.parseInt(match[1], 10),
    mixing: fromLittleEndianBase36(match[2]),
    mixingBase36: match[2],
  };
}

export function buildTraitIdMap(traits) {
  const map = new Map();
  for (let index = 0; index < traits.length; index += 1) {
    map.set(index, traits[index].id);
  }
  return map;
}

export function buildGeyserTypeMap(geysers) {
  const map = new Map();
  for (const geyser of geysers) {
    map.set(geyser.id, geyser.key);
  }
  return map;
}

export function sortStrings(items) {
  return [...items].sort((left, right) => left.localeCompare(right));
}

export function sortEntities(items) {
  return [...items].sort((left, right) => {
    if (left.worldY !== right.worldY) {
      return left.worldY - right.worldY;
    }
    if (left.worldX !== right.worldX) {
      return left.worldX - right.worldX;
    }
    return left.kind.localeCompare(right.kind);
  });
}

export function unique(values) {
  return [...new Set(values)];
}

export function entityKey(entity) {
  return `${entity.kind}@${entity.worldX},${entity.worldY}`;
}

export function positionKey(entity) {
  return `${entity.worldX},${entity.worldY}`;
}

export function diffSortedSets(leftItems, rightItems) {
  const left = new Set(leftItems);
  const right = new Set(rightItems);
  return {
    onlyLeft: sortStrings([...left].filter((item) => !right.has(item))),
    onlyRight: sortStrings([...right].filter((item) => !left.has(item))),
  };
}
