import type { SearchCatalog } from "./contracts";

export const EMPTY_SEARCH_CATALOG: SearchCatalog = {
  worlds: [],
  geysers: [],
  traits: [],
  mixingSlots: [],
  parameterSpecs: [],
};

export function normalizeSearchCatalog(catalog: Partial<SearchCatalog> | null | undefined): SearchCatalog {
  return {
    worlds: catalog?.worlds ?? [],
    geysers: catalog?.geysers ?? [],
    traits: catalog?.traits ?? [],
    mixingSlots: catalog?.mixingSlots ?? [],
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
