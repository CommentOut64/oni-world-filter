export interface SidecarBindingState {
  listening: boolean;
  bindingSidecar: boolean;
  lastError: string | null;
}

export interface BeginSidecarBindingResult {
  nextState: SidecarBindingState;
  shouldSubscribe: boolean;
}

export function beginSidecarBinding(
  state: SidecarBindingState
): BeginSidecarBindingResult {
  if (state.listening || state.bindingSidecar) {
    return {
      nextState: state,
      shouldSubscribe: false,
    };
  }

  return {
    nextState: {
      ...state,
      bindingSidecar: true,
    },
    shouldSubscribe: true,
  };
}

export function completeSidecarBinding(
  state: SidecarBindingState
): SidecarBindingState {
  return {
    ...state,
    listening: true,
    bindingSidecar: false,
  };
}

export function failSidecarBinding(
  state: SidecarBindingState,
  error: string
): SidecarBindingState {
  return {
    ...state,
    bindingSidecar: false,
    lastError: error,
  };
}

export function disposeSidecarBinding(
  state: SidecarBindingState
): SidecarBindingState {
  return {
    ...state,
    listening: false,
    bindingSidecar: false,
  };
}
