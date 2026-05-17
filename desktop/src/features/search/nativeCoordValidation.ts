const NATIVE_COORD_ERROR =
  "坐标无效：请输入完整原生坐标，且最后一段需为 1 到 5 位大写 base36，且不能超出 mixing 有效范围。";

const MIXING_MAX = 48_828_124;

function decodeLittleEndianBase36(input: string): number {
  let value = 0;
  for (let index = input.length - 1; index >= 0; index -= 1) {
    value *= 36;
    value += Number.parseInt(input[index] ?? "0", 36);
  }
  return value;
}

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

  if (!/^[0-9A-Z]{1,5}$/.test(mixingPart)) {
    return NATIVE_COORD_ERROR;
  }

  if (decodeLittleEndianBase36(mixingPart) > MIXING_MAX) {
    return NATIVE_COORD_ERROR;
  }

  return null;
}
