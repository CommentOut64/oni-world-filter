export interface SidecarListenerRegistry {
  beginBinding(): number;
  isActiveBinding(bindingId: number): boolean;
  resolveBinding(bindingId: number, release: () => void): boolean;
  dispose(): void;
}

function once(fn: () => void): () => void {
  let called = false;
  return () => {
    if (called) {
      return;
    }
    called = true;
    fn();
  };
}

export function createSidecarListenerRegistry(): SidecarListenerRegistry {
  let activeBindingId = 0;
  let currentRelease: (() => void) | null = null;

  return {
    beginBinding() {
      activeBindingId += 1;
      return activeBindingId;
    },
    isActiveBinding(bindingId) {
      return bindingId === activeBindingId;
    },
    resolveBinding(bindingId, release) {
      const safeRelease = once(release);
      if (bindingId !== activeBindingId) {
        safeRelease();
        return false;
      }
      if (currentRelease) {
        currentRelease();
      }
      currentRelease = safeRelease;
      return true;
    },
    dispose() {
      activeBindingId += 1;
      if (currentRelease) {
        currentRelease();
        currentRelease = null;
      }
    },
  };
}
