import {
  ROLE_PRIMARY,
  ROLE_SECONDARY,
  buildGeyserTypeMap,
  buildTraitIdMap,
  diffSortedSets,
  entityKey,
  positionKey,
  sortEntities,
  sortStrings,
  unique,
} from "./common.mjs";

const EXTERNAL_SPECIAL_ENTITY_KIND = new Map([
  ["OilWell", "oil_reservoir"],
  ["SmallReefGeyser", "small_reef_geyser"],
  ["UnderwaterVent", "underwater_vent"],
  ["WarpConduitSender", "warp_sender"],
  ["WarpConduitReceiver", "warp_receiver"],
  ["WarpPortal", "warp_portal"],
]);

const EXTERNAL_TRAIT_ALIAS = new Map([
  ["traits/GeoDormant", "traits/Geodormant"],
]);

function normalizeProjectWorld(role, previewEvent, traitIdMap) {
  if (!previewEvent) {
    return null;
  }
  const summary = previewEvent.preview.summary;
  return {
    role,
    worldAssetId: summary.worldAssetId,
    isPrimary: summary.isPrimary,
    worldPlacementIndex: summary.worldPlacementIndex,
    traits: sortStrings(summary.traits.map((traitIndex) => traitIdMap.get(traitIndex) ?? `trait#${traitIndex}`)),
    geysers: sortEntities(
      summary.geysers.map((geyser) => ({
        kind: geyser.id,
        rawKind: geyser.id,
        typeIndex: geyser.type,
        worldX: geyser.worldX,
        worldY: geyser.worldY,
      }))
    ),
  };
}

export function normalizeProjectResult(previews, catalog, spec) {
  const traitIdMap = buildTraitIdMap(catalog.traits);
  return {
    source: "project",
    coord: spec.coord,
    worldType: spec.worldType,
    worldCode: spec.worldCode,
    seed: spec.seed,
    mixing: spec.mixing,
    mixingLevels: spec.mixingLevels,
    worlds: {
      [ROLE_PRIMARY]: normalizeProjectWorld(ROLE_PRIMARY, previews.primary, traitIdMap),
      [ROLE_SECONDARY]: normalizeProjectWorld(ROLE_SECONDARY, previews.secondary, traitIdMap),
    },
  };
}

function normalizeExternalSpecialEntities(world) {
  const entities = [];
  for (const item of [...world.other_entities, ...world.buildings]) {
    const mappedKind = EXTERNAL_SPECIAL_ENTITY_KIND.get(item.tag);
    if (!mappedKind) {
      continue;
    }
    entities.push({
      kind: mappedKind,
      rawKind: item.tag,
      worldX: item.x,
      worldY: item.y,
    });
  }
  return entities;
}

function normalizeExternalWorld(role, world) {
  if (!world) {
    return null;
  }
  const geysers = world.geysers.map((geyser) => ({
    kind: EXTERNAL_SPECIAL_ENTITY_KIND.get(geyser.type) ?? geyser.type,
    rawKind: geyser.tag,
    worldX: geyser.x,
    worldY: geyser.y,
  }));
  return {
    role,
    worldAssetId: world.name,
    isPrimary: world.is_starting,
    traits: sortStrings(world.world_traits.map((trait) => EXTERNAL_TRAIT_ALIAS.get(trait) ?? trait)),
    geysers: sortEntities([...geysers, ...normalizeExternalSpecialEntities(world)]),
  };
}

export function normalizeExternalResult(map, projectResult, spec) {
  const primaryWorld = map.worlds.find((world) => world.is_starting) ?? map.worlds[0] ?? null;
  const secondaryAssetId = projectResult.worlds[ROLE_SECONDARY]?.worldAssetId ?? null;
  const secondaryWorld = secondaryAssetId
    ? map.worlds.find((world) => world.name === secondaryAssetId) ?? null
    : null;
  return {
    source: "external",
    coord: spec.coord,
    worldType: spec.worldType,
    worldCode: spec.worldCode,
    seed: spec.seed,
    mixing: spec.mixing,
    mixingLevels: spec.mixingLevels,
    worlds: {
      [ROLE_PRIMARY]: normalizeExternalWorld(ROLE_PRIMARY, primaryWorld),
      [ROLE_SECONDARY]: normalizeExternalWorld(ROLE_SECONDARY, secondaryWorld),
    },
  };
}

function compareWorld(role, projectWorld, externalWorld) {
  if (!projectWorld && !externalWorld) {
    return {
      role,
      diffKinds: [],
      traitDiff: { onlyProject: [], onlyExternal: [] },
      geyserDiff: { onlyProject: [], onlyExternal: [] },
    };
  }
  if (!projectWorld || !externalWorld) {
    return {
      role,
      diffKinds: [`${role}_presence_mismatch`],
      traitDiff: { onlyProject: projectWorld?.traits ?? [], onlyExternal: externalWorld?.traits ?? [] },
      geyserDiff: {
        onlyProject: projectWorld?.geysers?.map(entityKey) ?? [],
        onlyExternal: externalWorld?.geysers?.map(entityKey) ?? [],
      },
    };
  }

  const diffKinds = [];
  if (projectWorld.worldAssetId !== externalWorld.worldAssetId) {
    diffKinds.push(`${role}_world_asset_mismatch`);
  }

  const traitDiff = diffSortedSets(projectWorld.traits, externalWorld.traits);
  if (traitDiff.onlyLeft.length || traitDiff.onlyRight.length) {
    diffKinds.push(`${role}_trait_mismatch`);
  }

  const projectEntityKeys = projectWorld.geysers.map(entityKey);
  const externalEntityKeys = externalWorld.geysers.map(entityKey);
  const geyserDiff = diffSortedSets(projectEntityKeys, externalEntityKeys);
  if (geyserDiff.onlyLeft.length || geyserDiff.onlyRight.length) {
    if (projectWorld.geysers.length !== externalWorld.geysers.length) {
      diffKinds.push(`${role}_geyser_count_mismatch`);
    }

    const projectPositions = unique(projectWorld.geysers.map(positionKey)).sort();
    const externalPositions = unique(externalWorld.geysers.map(positionKey)).sort();
    const samePositions = JSON.stringify(projectPositions) === JSON.stringify(externalPositions);
    if (samePositions) {
      diffKinds.push(`${role}_geyser_kind_mismatch`);
    } else {
      diffKinds.push(`${role}_geyser_position_mismatch`);
    }
  }

  return {
    role,
    diffKinds,
    traitDiff: {
      onlyProject: traitDiff.onlyLeft,
      onlyExternal: traitDiff.onlyRight,
    },
    geyserDiff: {
      onlyProject: geyserDiff.onlyLeft,
      onlyExternal: geyserDiff.onlyRight,
    },
  };
}

export function compareResults(projectResult, externalResult) {
  const primary = compareWorld(ROLE_PRIMARY, projectResult.worlds[ROLE_PRIMARY], externalResult.worlds[ROLE_PRIMARY]);
  const secondary = compareWorld(
    ROLE_SECONDARY,
    projectResult.worlds[ROLE_SECONDARY],
    externalResult.worlds[ROLE_SECONDARY]
  );
  const diffKinds = unique([...primary.diffKinds, ...secondary.diffKinds]).sort();
  return {
    coord: projectResult.coord,
    same: diffKinds.length === 0,
    diffKinds,
    worlds: {
      [ROLE_PRIMARY]: primary,
      [ROLE_SECONDARY]: secondary,
    },
  };
}
