export interface WorldProfileRequestState {
  worldType: number;
  mixing: number;
}

export function hasMatchingWorldProfileRequest(
  state: WorldProfileRequestState | null,
  worldType: number,
  mixing: number
): boolean {
  return state !== null && state.worldType === worldType && state.mixing === mixing;
}
