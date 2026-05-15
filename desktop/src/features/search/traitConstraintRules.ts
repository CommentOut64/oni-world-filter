import type { TraitMeta } from "../../lib/contracts.ts";

export interface TraitOptionState {
  value: string;
  disabled: boolean;
}

function isTraitUnavailableInWorld(
  traitId: string,
  availableTraitIds?: ReadonlySet<string> | null
): boolean {
  if (!availableTraitIds) {
    return false;
  }
  return !availableTraitIds.has(traitId);
}

function hasSharedExclusiveTag(lhs: TraitMeta, rhs: TraitMeta): boolean {
  return lhs.exclusiveWithTags.some((tag) => rhs.exclusiveWithTags.includes(tag));
}

export function areTraitsMutuallyExclusive(lhs: TraitMeta, rhs: TraitMeta): boolean {
  if (lhs.id === rhs.id) {
    return false;
  }
  return (
    lhs.exclusiveWith.includes(rhs.id) ||
    rhs.exclusiveWith.includes(lhs.id) ||
    hasSharedExclusiveTag(lhs, rhs)
  );
}

function createTraitMap(traits: readonly TraitMeta[]): ReadonlyMap<string, TraitMeta> {
  return new Map(traits.map((trait) => [trait.id, trait] as const));
}

function isTraitBlockedBySelectedTraits(
  candidate: TraitMeta,
  selectedTraitIds: readonly string[],
  traitMap: ReadonlyMap<string, TraitMeta>
): boolean {
  return selectedTraitIds.some((traitId) => {
    if (!traitId || traitId === candidate.id) {
      return false;
    }
    const selectedTrait = traitMap.get(traitId);
    return selectedTrait ? areTraitsMutuallyExclusive(candidate, selectedTrait) : false;
  });
}

export function findFirstAvailableTrait(
  traits: readonly TraitMeta[],
  selectedTraitIds: ReadonlySet<string>,
  availableTraitIds?: ReadonlySet<string> | null
): string | null {
  const selectedIds = [...selectedTraitIds].filter((traitId) => traitId.length > 0);
  const traitMap = createTraitMap(traits);
  for (const trait of traits) {
    if (selectedTraitIds.has(trait.id)) {
      continue;
    }
    if (isTraitUnavailableInWorld(trait.id, availableTraitIds)) {
      continue;
    }
    if (isTraitBlockedBySelectedTraits(trait, selectedIds, traitMap)) {
      continue;
    }
    return trait.id;
  }
  return null;
}

export function buildTraitOptionStates(
  traits: readonly TraitMeta[],
  selectedTraitIdsByRow: readonly string[],
  currentRowIndex: number,
  availableTraitIds?: ReadonlySet<string> | null
): TraitOptionState[] {
  const currentValue = selectedTraitIdsByRow[currentRowIndex] ?? "";
  const selectedTraitIds = selectedTraitIdsByRow.filter(
    (traitId, index) => index !== currentRowIndex && traitId.length > 0
  );
  const selectedTraitSet = new Set(selectedTraitIds);
  const traitMap = createTraitMap(traits);

  return traits.map((trait) => ({
    value: trait.id,
    disabled:
      isTraitUnavailableInWorld(trait.id, availableTraitIds) ||
      (trait.id !== currentValue &&
        (selectedTraitSet.has(trait.id) ||
          isTraitBlockedBySelectedTraits(trait, selectedTraitIds, traitMap))),
  }));
}
