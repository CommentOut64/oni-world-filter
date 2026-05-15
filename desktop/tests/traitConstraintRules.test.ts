import assert from "node:assert/strict";
import test from "node:test";

import type { TraitMeta } from "../src/lib/contracts.ts";
import {
  buildTraitOptionStates,
  findFirstAvailableTrait,
} from "../src/features/search/traitConstraintRules.ts";

const traits: TraitMeta[] = [
  {
    id: "traits/MetalRich",
    name: "Metal Rich",
    description: "",
    traitTags: [],
    exclusiveWith: ["traits/MetalPoor"],
    exclusiveWithTags: [],
    forbiddenDLCIds: [],
    effectSummary: [],
    searchable: true,
  },
  {
    id: "traits/MetalPoor",
    name: "Metal Poor",
    description: "",
    traitTags: [],
    exclusiveWith: ["traits/MetalRich"],
    exclusiveWithTags: [],
    forbiddenDLCIds: [],
    effectSummary: [],
    searchable: true,
  },
  {
    id: "traits/MagmaVents",
    name: "Magma Vents",
    description: "",
    traitTags: [],
    exclusiveWith: [],
    exclusiveWithTags: ["core"],
    forbiddenDLCIds: [],
    effectSummary: [],
    searchable: true,
  },
  {
    id: "traits/FrozenCore",
    name: "Frozen Core",
    description: "",
    traitTags: [],
    exclusiveWith: [],
    exclusiveWithTags: ["core"],
    forbiddenDLCIds: [],
    effectSummary: [],
    searchable: true,
  },
  {
    id: "traits/GeoActive",
    name: "Geo Active",
    description: "",
    traitTags: [],
    exclusiveWith: [],
    exclusiveWithTags: [],
    forbiddenDLCIds: [],
    effectSummary: [],
    searchable: true,
  },
];

test("buildTraitOptionStates disables explicitly exclusive traits selected in other rows", () => {
  const options = buildTraitOptionStates(traits, ["traits/MetalRich", "traits/GeoActive"], 1);
  const metalPoor = options.find((item) => item.value === "traits/MetalPoor");
  const geoActive = options.find((item) => item.value === "traits/GeoActive");

  assert.equal(metalPoor?.disabled, true);
  assert.equal(geoActive?.disabled, false);
});

test("buildTraitOptionStates disables tag-conflicting traits selected in other rows", () => {
  const options = buildTraitOptionStates(traits, ["traits/MagmaVents", ""], 1);
  const frozenCore = options.find((item) => item.value === "traits/FrozenCore");

  assert.equal(frozenCore?.disabled, true);
});

test("buildTraitOptionStates disables traits unavailable in current world profile", () => {
  const options = buildTraitOptionStates(
    traits,
    ["traits/MetalRich", ""],
    0,
    new Set(["traits/GeoActive"])
  );
  const metalRich = options.find((item) => item.value === "traits/MetalRich");
  const geoActive = options.find((item) => item.value === "traits/GeoActive");

  assert.equal(metalRich?.disabled, true);
  assert.equal(geoActive?.disabled, false);
});

test("findFirstAvailableTrait skips duplicate and mutually exclusive traits", () => {
  const firstAvailable = findFirstAvailableTrait(traits, new Set(["traits/MetalRich"]));

  assert.equal(firstAvailable, "traits/MagmaVents");
});

test("findFirstAvailableTrait skips traits unavailable in current world profile", () => {
  const firstAvailable = findFirstAvailableTrait(
    traits,
    new Set(["traits/MetalRich"]),
    new Set(["traits/GeoActive"])
  );

  assert.equal(firstAvailable, "traits/GeoActive");
});
