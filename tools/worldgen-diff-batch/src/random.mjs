import {
  buildCanonicalCoordinate,
  encodeMixingLevels,
  groupMixingSlots,
  groupWorldsByCategory,
  sanitizeMixingLevelsForWorld,
} from "./common.mjs";

export function createMulberry32(seed) {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let value = Math.imul(state ^ (state >>> 15), 1 | state);
    value ^= value + Math.imul(value ^ (value >>> 7), 61 | value);
    return ((value ^ (value >>> 14)) >>> 0) / 4294967296;
  };
}

function pickOne(items, random) {
  if (!items.length) {
    throw new Error("随机池为空");
  }
  return items[Math.floor(random() * items.length)];
}

function weightedLevel(random, weights) {
  const total = weights.reduce((sum, item) => sum + item.weight, 0);
  const roll = random() * total;
  let current = 0;
  for (const item of weights) {
    current += item.weight;
    if (roll <= current) {
      return item.value;
    }
  }
  return weights.at(-1).value;
}

function buildRandomMixingLevels(catalog, random, worldCode) {
  const levels = new Array(catalog.mixingSlots.length).fill(0);
  const groups = groupMixingSlots(catalog.mixingSlots);

  for (const group of groups) {
    const packageLevel = weightedLevel(random, [
      { value: 0, weight: 0.45 },
      { value: 1, weight: 0.4 },
      { value: 2, weight: 0.15 },
    ]);

    levels[group.packageSlot.slot] = packageLevel;
    if (packageLevel === 0) {
      continue;
    }

    let enabledChildren = 0;
    for (const child of group.children) {
      const level = weightedLevel(random, [
        { value: 0, weight: 0.5 },
        { value: 1, weight: 0.35 },
        { value: 2, weight: 0.15 },
      ]);
      levels[child.slot] = level;
      if (level !== 0) {
        enabledChildren += 1;
      }
    }

    // slot 内部的生态子项也要被覆盖；若包已开启但子槽位全关，优先补一个子项。
    if (group.children.length > 0 && enabledChildren === 0 && random() < 0.7) {
      const child = pickOne(group.children, random);
      levels[child.slot] = random() < 0.75 ? 1 : 2;
    }
  }

  return sanitizeMixingLevelsForWorld(levels, worldCode);
}

export function generateRandomCoordinate(catalog, random) {
  const worldGroups = groupWorldsByCategory(catalog.worlds);
  const categories = Object.entries(worldGroups)
    .filter(([, worlds]) => worlds.length > 0)
    .map(([category]) => category);
  const category = pickOne(categories, random);
  const world = pickOne(worldGroups[category], random);
  const levels = buildRandomMixingLevels(catalog, random, world.code);
  const mixing = encodeMixingLevels(levels);
  const seed = Math.floor(random() * 2147483647);
  return {
    category,
    worldType: world.id,
    worldCode: world.code,
    seed,
    mixing,
    mixingLevels: levels,
    coord: buildCanonicalCoordinate(world.code, seed, mixing),
  };
}
