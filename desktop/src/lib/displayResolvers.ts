import type { GeyserOption, GeyserSummary, MixingSlotMeta, WorldOption } from "./contracts.ts";
import {
  GEYSER_DISPLAY_NAMES,
  MIXING_SLOT_DISPLAY_NAMES,
  PLAYER_ZONE_TYPE_DISPLAY_NAMES,
  WORLD_DISPLAY_NAMES,
  ZONE_TYPE_DISPLAY_NAMES,
  type DisplayName,
} from "./displayNames.ts";

export function formatDisplayName(name: DisplayName): string {
  return name.zh;
}

export function formatWorldNameByCode(code: string): string {
  const displayName = WORLD_DISPLAY_NAMES[code];
  return displayName ? formatDisplayName(displayName) : code;
}

export function formatWorldName(world: WorldOption): string {
  return formatWorldNameByCode(world.code);
}

export function formatGeyserNameByKey(key: string): string {
  const displayName = GEYSER_DISPLAY_NAMES[key];
  return displayName ? formatDisplayName(displayName) : key;
}

export function geyserKeyFromType(type: number, geysers: readonly GeyserOption[]): string | null {
  return geysers.find((item) => item.id === type)?.key ?? null;
}

export function formatGeyserNameByType(type: number, geysers: readonly GeyserOption[]): string {
  const key = geyserKeyFromType(type, geysers);
  return key ? formatGeyserNameByKey(key) : `type#${type}`;
}

export function formatGeyserNameFromSummary(
  summary: GeyserSummary,
  geysers: readonly GeyserOption[]
): string {
  if (summary.id) {
    return formatGeyserNameByKey(summary.id);
  }
  return formatGeyserNameByType(summary.type, geysers);
}

export function formatMixingSlotName(slot: MixingSlotMeta): string {
  const displayName = MIXING_SLOT_DISPLAY_NAMES[slot.path];
  if (displayName) {
    return formatDisplayName(displayName);
  }
  if (slot.name.trim().length > 0) {
    return slot.name;
  }
  if (slot.path.trim().length > 0) {
    return slot.path;
  }
  return `slot#${slot.slot}`;
}

export function formatZoneTypeName(zoneType: number): string {
  const displayName = ZONE_TYPE_DISPLAY_NAMES[zoneType];
  return displayName ? formatDisplayName(displayName) : `zone#${zoneType}`;
}

export function formatPlayerBiomeNameByZoneType(zoneType: number): string | null {
  const displayName = PLAYER_ZONE_TYPE_DISPLAY_NAMES[zoneType];
  return displayName ? formatDisplayName(displayName) : null;
}
