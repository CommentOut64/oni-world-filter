import type { WorldOption } from "../../lib/contracts.ts";

const BROKEN_AQUATIC_WORLD_CODES = new Set(["AQU-A-", "V-AQU-C-", "AQU-C-"]);

export function isBrokenAquaticWorldCode(code: string | null | undefined): boolean {
  return typeof code === "string" && BROKEN_AQUATIC_WORLD_CODES.has(code);
}

export function findWorldCodeByType(
  worlds: readonly WorldOption[],
  worldType: number
): string | null {
  if (!Number.isFinite(worldType)) {
    return null;
  }
  const matched = worlds.find((item) => item.id === worldType);
  return matched?.code ?? null;
}

export function isBrokenAquaticWorldType(
  worlds: readonly WorldOption[],
  worldType: number
): boolean {
  return isBrokenAquaticWorldCode(findWorldCodeByType(worlds, worldType));
}
