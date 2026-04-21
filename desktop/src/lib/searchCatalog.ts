import type { SearchCatalog } from "./contracts";
import { FALLBACK_MIXING_SLOT_PATHS, MIXING_SLOT_DISPLAY_NAMES } from "./displayNames.ts";

const FALLBACK_WORLD_CODES = [
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
  "SNDST-C-",
  "PRE-C-",
  "CER-C-",
  "FRST-C-",
  "SWMP-C-",
  "M-SWMP-C-",
  "M-BAD-C-",
  "M-FRZ-C-",
  "M-FLIP-C-",
  "M-RAD-C-",
  "M-CERS-C-",
] as const;

const FALLBACK_GEYSER_IDS = [
  "steam",
  "hot_steam",
  "hot_water",
  "slush_water",
  "filthy_water",
  "slush_salt_water",
  "salt_water",
  "small_volcano",
  "big_volcano",
  "liquid_co2",
  "hot_co2",
  "hot_hydrogen",
  "hot_po2",
  "slimy_po2",
  "chlorine_gas",
  "methane",
  "molten_copper",
  "molten_iron",
  "molten_gold",
  "molten_aluminum",
  "molten_cobalt",
  "oil_drip",
  "liquid_sulfur",
  "chlorine_gas_cool",
  "molten_tungsten",
  "molten_niobium",
  "printing_pod",
  "oil_reservoir",
  "warp_sender",
  "warp_receiver",
  "warp_portal",
  "cryo_tank",
] as const;

export const FALLBACK_MIXING_SLOTS = FALLBACK_MIXING_SLOT_PATHS.map((path, slot) => {
  const displayName = MIXING_SLOT_DISPLAY_NAMES[path];
  return {
    slot,
    path,
    type:
      path === "DLC2_ID" || path === "DLC3_ID" || path === "DLC4_ID"
        ? "dlc"
        : path.includes("worldMixing/")
          ? "world"
          : "subworld",
    name: displayName.en,
    description: formatFallbackMixingDescription(displayName.zh, displayName.en),
  };
});

function formatFallbackMixingDescription(zh: string, en: string): string {
  return `${zh}（${en}）`;
}

export const EMPTY_SEARCH_CATALOG: SearchCatalog = {
  worlds: [],
  geysers: [],
  traits: [],
  mixingSlots: [],
  parameterSpecs: [],
};

export const FALLBACK_SEARCH_CATALOG: SearchCatalog = {
  worlds: FALLBACK_WORLD_CODES.map((code, id) => ({ id, code })),
  geysers: FALLBACK_GEYSER_IDS.map((key, id) => ({ id, key })),
  traits: [],  
  mixingSlots: FALLBACK_MIXING_SLOTS,
  parameterSpecs: [],
};

export function normalizeSearchCatalog(catalog: Partial<SearchCatalog> | null | undefined): SearchCatalog {
  return {
    worlds: catalog?.worlds?.length ? catalog.worlds : FALLBACK_SEARCH_CATALOG.worlds,
    geysers: catalog?.geysers?.length ? catalog.geysers : FALLBACK_SEARCH_CATALOG.geysers,
    traits: catalog?.traits ?? [],
    mixingSlots: catalog?.mixingSlots?.length ? catalog.mixingSlots : FALLBACK_MIXING_SLOTS,
    parameterSpecs: catalog?.parameterSpecs ?? [],
  };
}

export function getParameterSpecStaticMax(catalog: SearchCatalog | null, id: string): number | null {
  if (!catalog) {
    return null;
  }
  const spec = catalog.parameterSpecs.find((item) => item.id === id);
  if (!spec) {
    return null;
  }
  const match = /^(\d+)\.\.(\d+)$/.exec(spec.staticRange.trim());
  if (!match) {
    return null;
  }
  const max = Number(match[2]);
  return Number.isFinite(max) ? max : null;
}
