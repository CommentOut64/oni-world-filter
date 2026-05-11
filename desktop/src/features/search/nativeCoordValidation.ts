const NATIVE_COORD_ERROR =
  "坐标无效：请输入 5 段原生坐标，且最后一段只能是 0 或 5 位大写 base36。";

function splitNativeCoordParts(coord: string, worldCodes: readonly string[]): string[] | null {
  let matchedPrefix = "";
  for (const worldCode of worldCodes) {
    if (coord.startsWith(worldCode) && worldCode.length > matchedPrefix.length) {
      matchedPrefix = worldCode;
    }
  }
  if (!matchedPrefix) {
    return null;
  }

  const suffix = coord.slice(matchedPrefix.length);
  const parts = suffix.split("-");
  if (parts.length !== 4 || parts.some((part) => part.length === 0)) {
    return null;
  }
  return parts;
}

export function validateNativeCoordInput(
  coord: string,
  worldCodes: readonly string[]
): string | null {
  const parts = splitNativeCoordParts(coord.trim(), worldCodes);
  if (parts === null) {
    return NATIVE_COORD_ERROR;
  }

  const [seedPart, _otherPart, _storyPart, mixingPart] = parts;
  if (!/^\d+$/.test(seedPart)) {
    return NATIVE_COORD_ERROR;
  }

  if (mixingPart !== "0" && !/^[0-9A-Z]{5}$/.test(mixingPart)) {
    return NATIVE_COORD_ERROR;
  }

  return null;
}
