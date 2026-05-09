import type { SearchCatalog, TraitMeta } from "./contracts.ts";
import { TRAIT_DISPLAY_NAMES } from "./traitDisplayNames.ts";

export function resolveTraitMetaBySummaryIndex(
  traitIndex: number,
  catalog: SearchCatalog | null
): TraitMeta | null {
  if (!catalog) {
    return null;
  }
  return catalog.traits[traitIndex] ?? null;
}

export function resolveTraitDisplayName(
  traitIndex: number,
  catalog: SearchCatalog | null
): string {
  const traitMeta = resolveTraitMetaBySummaryIndex(traitIndex, catalog);
  if (!traitMeta) {
    return `未知特质(#${traitIndex})`;
  }

  return TRAIT_DISPLAY_NAMES[traitMeta.id] ?? traitMeta.id;
}
