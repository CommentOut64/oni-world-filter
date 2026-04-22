import type { MixingSlotMeta, WorldOption } from "../../lib/contracts";

export type WorldCategory = "baseAsteroid" | "classicCluster" | "moonletCluster";

export interface WorldCategoryOption {
  id: WorldCategory;
  label: string;
  description: string;
}

export interface MixingPackageGroup<TSlot extends MixingSlotMeta = MixingSlotMeta> {
  packageSlot: TSlot;
  children: TSlot[];
}

export type MixingUiMode = "off" | "normal" | "guaranteed";

export const WORLD_CATEGORY_OPTIONS: readonly WorldCategoryOption[] = [
    {
        id: "baseAsteroid",
        label: "原版",
        description: "未加装眼冒金星DLC的原版",
    },
    {
        id: "classicCluster",
        label: "经典",
        description: "加装了眼冒金星DLC后的经典模式",
    },
    {
        id: "moonletCluster",
        label: "眼冒金星",
        description: "加装了眼冒金星DLC后的新模式",
    },
];

const HIDDEN_WORLD_CODES = new Set(["PRES-A-", "V-PRES-C-"]);
const BASE_ASTEROID_WORLD_CODES = new Set([
    "SNDST-A-",
    "OCAN-A-",
    "S-FRZ-",
    "LUSH-A-",
    "FRST-A-",
    "VOLCA-",
    "BAD-A-",
    "HTFST-A-",
    "OASIS-A-",
    "CER-A-",
    "CERS-A-",
    "PRE-A-",
    "PRES-A-",
]);
const CLASSIC_CLUSTER_WORLD_CODES = new Set([
    "V-SNDST-C-",
    "V-OCAN-C-",
    "V-SWMP-C-",
    "V-SFRZ-C-",
    "V-LUSH-C-",
    "V-FRST-C-",
    "V-VOLCA-C-",
    "V-BAD-C-",
    "V-HTFST-C-",
    "V-OASIS-C-",
    "V-CER-C-",
    "V-CERS-C-",
    "V-PRE-C-",
    "V-PRES-C-",
]);

const MIXING_PACKAGE_ORDER = ["DLC2_ID", "DLC3_ID", "DLC4_ID"] as const;

type MixingPackagePath = (typeof MIXING_PACKAGE_ORDER)[number];

const MIXING_PACKAGE_PREFIX: Record<MixingPackagePath, string[]> = {
  DLC2_ID: ["dlc2::worldMixing/", "dlc2::subworldMixing/"],
  DLC3_ID: ["dlc3::worldMixing/", "dlc3::subworldMixing/"],
  DLC4_ID: ["dlc4::worldMixing/", "dlc4::subworldMixing/"],
};

export function classifyWorld(code: string): WorldCategory {
    // 使用完整映射表，避免 S-FRZ / VOLCA 这类不满足简单前缀规则的世界被误分。
    if (BASE_ASTEROID_WORLD_CODES.has(code)) {
        return "baseAsteroid";
    }
    if (CLASSIC_CLUSTER_WORLD_CODES.has(code)) {
        return "classicCluster";
    }
    return "moonletCluster";
}

export function groupWorldsByCategory(
  worlds: readonly WorldOption[]
): Record<WorldCategory, WorldOption[]> {
  const grouped: Record<WorldCategory, WorldOption[]> = {
    baseAsteroid: [],
    classicCluster: [],
    moonletCluster: [],
  };
  for (const world of worlds) {
    // 历史遗留命名异常的世界先从前端候选中隐藏，后端枚举保持不变。
    if (HIDDEN_WORLD_CODES.has(world.code)) {
      continue;
    }
    grouped[classifyWorld(world.code)].push(world);
  }
  return grouped;
}

export function findCategoryForWorld(
  worlds: readonly WorldOption[],
  worldType: number
): WorldCategory | null {
  if (!Number.isFinite(worldType)) {
    return null;
  }
  const world = worlds.find(
    (item) => item.id === worldType && !HIDDEN_WORLD_CODES.has(item.code)
  );
  return world ? classifyWorld(world.code) : null;
}

export function getCategoryForWorld(
  worlds: readonly WorldOption[],
  worldType: number
): WorldCategory {
  return findCategoryForWorld(worlds, worldType) ?? "classicCluster";
}

export function isWorldTypeVisibleInCategory(
  worlds: readonly WorldOption[],
  worldType: number,
  category: WorldCategory
): boolean {
  return findCategoryForWorld(worlds, worldType) === category;
}

function isChildOfPackage(path: string, packagePath: MixingPackagePath): boolean {
  return MIXING_PACKAGE_PREFIX[packagePath].some((prefix) => path.startsWith(prefix));
}

export function groupMixingSlots<TSlot extends MixingSlotMeta>(
  slots: readonly TSlot[]
): MixingPackageGroup<TSlot>[] {
  const groups: MixingPackageGroup<TSlot>[] = [];

  for (const packagePath of MIXING_PACKAGE_ORDER) {
    const packageSlot = slots.find((item) => item.path === packagePath);
    if (!packageSlot) {
      continue;
    }
    const children = slots
      .filter((item) => item.path !== packagePath && isChildOfPackage(item.path, packagePath))
      .sort((lhs, rhs) => lhs.slot - rhs.slot);
    groups.push({
      packageSlot,
      children,
    });
  }

  return groups;
}

export function levelToUiMode(level: number): MixingUiMode {
  if (level === 2) {
    return "guaranteed";
  }
  if (level <= 0 || !Number.isFinite(level)) {
    return "off";
  }
  return "normal";
}

export function uiModeToLevel(mode: MixingUiMode): number {
  switch (mode) {
    case "guaranteed":
      return 2;
    case "normal":
      return 1;
    default:
      return 0;
  }
}

export function isSlotEnabled(level: number): boolean {
  return levelToUiMode(level) !== "off";
}

export function getPackageMode(
  levels: readonly number[],
  group: MixingPackageGroup
): MixingUiMode {
  return levelToUiMode(levels[group.packageSlot.slot] ?? 0);
}

export function applyPackageMode(
  levels: readonly number[],
  group: MixingPackageGroup,
  mode: MixingUiMode
): number[] {
  const nextLevels = [...levels];
  nextLevels[group.packageSlot.slot] = uiModeToLevel(mode);

  if (mode === "off") {
    for (const child of group.children) {
      nextLevels[child.slot] = 0;
    }
  }

  return nextLevels;
}

export function applyChildMode(
  levels: readonly number[],
  slot: number,
  mode: MixingUiMode
): number[] {
  const nextLevels = [...levels];
  if (slot < 0 || slot >= nextLevels.length) {
    return nextLevels;
  }
  nextLevels[slot] = uiModeToLevel(mode);
  return nextLevels;
}
