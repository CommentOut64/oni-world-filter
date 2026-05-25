import type { SearchCatalog } from "./contracts";
import { FALLBACK_MIXING_SLOT_PATHS, MIXING_SLOT_DISPLAY_NAMES } from "./displayNames.ts";
import fallbackData from "./searchCatalogFallbackData.json";

const FALLBACK_DLC_PATHS = new Set(fallbackData.mixingPackages.map((item) => item.path));

export const FALLBACK_MIXING_SLOTS = FALLBACK_MIXING_SLOT_PATHS.map((path, slot) => {
  const displayName = MIXING_SLOT_DISPLAY_NAMES[path];
  return {
    slot,
    path,
    type:
      FALLBACK_DLC_PATHS.has(path)
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
  worlds: fallbackData.worlds.map((item, id) => ({ id, code: item.code })),
  geysers: fallbackData.geysers.map((item, id) => ({
    id,
    key: item.key,
    name: item.name,
    kind: item.kind,
    parameterSource: item.parameterSource,
    supportsDynamicParameters: item.supportsDynamicParameters,
    supportsCoordinateParameters: item.supportsCoordinateParameters,
  })),
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
