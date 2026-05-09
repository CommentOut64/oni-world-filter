import type { GeyserOption } from "./contracts.ts";
import { formatGeyserNameByKey } from "./displayResolvers.ts";

export type GeyserCategory = "spring" | "vent" | "volcano" | "metalVolcano" | "facility";

export interface GeyserAvailabilityMap {
  readonly [key: string]: string | null | undefined;
}

interface ResolvedGeyserSortItem {
  geyserKey: string | null;
  name: string;
  disabled: boolean;
  stableKey: string;
}

const GEYSER_CATEGORY_ORDER: Record<GeyserCategory, number> = {
  spring: 0,
  vent: 1,
  volcano: 2,
  metalVolcano: 3,
  facility: 4,
};

const GEYSER_CATEGORY_BY_KEY: Partial<Record<string, GeyserCategory>> = {
  steam: "vent",
  hot_steam: "vent",
  hot_water: "spring",
  slush_water: "spring",
  filthy_water: "spring",
  slush_salt_water: "spring",
  salt_water: "spring",
  small_volcano: "volcano",
  big_volcano: "volcano",
  liquid_co2: "spring",
  hot_co2: "vent",
  hot_hydrogen: "vent",
  hot_po2: "vent",
  slimy_po2: "vent",
  chlorine_gas: "vent",
  methane: "vent",
  molten_copper: "metalVolcano",
  molten_iron: "metalVolcano",
  molten_gold: "metalVolcano",
  molten_aluminum: "metalVolcano",
  molten_cobalt: "metalVolcano",
  oil_drip: "spring",
  liquid_sulfur: "spring",
  chlorine_gas_cool: "vent",
  molten_tungsten: "metalVolcano",
  molten_niobium: "metalVolcano",
  printing_pod: "facility",
  oil_reservoir: "spring",
  warp_sender: "facility",
  warp_receiver: "facility",
  warp_portal: "facility",
  cryo_tank: "facility",
};

function getGeyserCategory(key: string): GeyserCategory {
  return GEYSER_CATEGORY_BY_KEY[key] ?? "facility";
}

function compareResolvedGeyserSortItems(
  left: ResolvedGeyserSortItem,
  right: ResolvedGeyserSortItem
): number {
  const leftDisabled = left.disabled ? 1 : 0;
  const rightDisabled = right.disabled ? 1 : 0;
  if (leftDisabled !== rightDisabled) {
    return leftDisabled - rightDisabled;
  }

  const leftCategory = GEYSER_CATEGORY_ORDER[getGeyserCategory(left.geyserKey ?? "")];
  const rightCategory = GEYSER_CATEGORY_ORDER[getGeyserCategory(right.geyserKey ?? "")];
  if (leftCategory !== rightCategory) {
    return leftCategory - rightCategory;
  }

  const nameOrder = left.name.localeCompare(right.name, "zh-Hans");
  if (nameOrder !== 0) {
    return nameOrder;
  }

  return left.stableKey.localeCompare(right.stableKey);
}

export function compareGeyserKeys(
  leftKey: string,
  rightKey: string,
  disabledKeys?: ReadonlySet<string>
): number {
  return compareResolvedGeyserSortItems(
    {
      geyserKey: leftKey,
      name: formatGeyserNameByKey(leftKey),
      disabled: disabledKeys?.has(leftKey) ?? false,
      stableKey: leftKey,
    },
    {
      geyserKey: rightKey,
      name: formatGeyserNameByKey(rightKey),
      disabled: disabledKeys?.has(rightKey) ?? false,
      stableKey: rightKey,
    }
  );
}

export function sortGeyserOptions(
  geysers: readonly GeyserOption[],
  disabledKeys?: ReadonlySet<string>
): GeyserOption[] {
  return [...geysers].sort((left, right) => compareGeyserKeys(left.key, right.key, disabledKeys));
}

export function sortGeyserOptionsByAvailability(
  geysers: readonly GeyserOption[],
  availability: GeyserAvailabilityMap
): GeyserOption[] {
  return [...geysers].sort((left, right) =>
    compareResolvedGeyserSortItems(
      {
        geyserKey: left.key,
        name: formatGeyserNameByKey(left.key),
        disabled: availability[left.key] !== null && availability[left.key] !== undefined,
        stableKey: left.key,
      },
      {
        geyserKey: right.key,
        name: formatGeyserNameByKey(right.key),
        disabled: availability[right.key] !== null && availability[right.key] !== undefined,
        stableKey: right.key,
      }
    )
  );
}

export function sortResolvedGeyserItems<T>(
  items: readonly T[],
  resolve: (item: T, index: number) => ResolvedGeyserSortItem
): T[] {
  return items
    .map((item, index) => ({
      item,
      resolved: resolve(item, index),
    }))
    .sort((left, right) => compareResolvedGeyserSortItems(left.resolved, right.resolved))
    .map((entry) => entry.item);
}
