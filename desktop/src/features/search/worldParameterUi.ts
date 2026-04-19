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
    label: "原版星体",
    description: "单星体开局，沿用旧参数页第一组世界。",
  },
  {
    id: "classicCluster",
    label: "经典星群",
    description: "经典星群布局，对应旧参数页第二组世界。",
  },
  {
    id: "moonletCluster",
    label: "卫星星群",
    description: "多卫星布局，对应旧参数页第三组世界。",
  },
];

const MIXING_PACKAGE_ORDER = ["DLC2_ID", "DLC3_ID", "DLC4_ID"] as const;

type MixingPackagePath = (typeof MIXING_PACKAGE_ORDER)[number];

const MIXING_PACKAGE_PREFIX: Record<MixingPackagePath, string[]> = {
  DLC2_ID: ["dlc2::worldMixing/", "dlc2::subworldMixing/"],
  DLC3_ID: ["dlc3::worldMixing/", "dlc3::subworldMixing/"],
  DLC4_ID: ["dlc4::worldMixing/", "dlc4::subworldMixing/"],
};

export function classifyWorld(code: string): WorldCategory {
  if (code.includes("-A-")) {
    return "baseAsteroid";
  }
  if (code.startsWith("V-") && code.includes("-C-")) {
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
    grouped[classifyWorld(world.code)].push(world);
  }
  return grouped;
}

export function getCategoryForWorld(
  worlds: readonly WorldOption[],
  worldType: number
): WorldCategory {
  const world = worlds.find((item) => item.id === worldType);
  return world ? classifyWorld(world.code) : "classicCluster";
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
