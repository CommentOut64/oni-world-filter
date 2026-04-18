import test from "node:test";
import assert from "node:assert/strict";

import { createSidecarListenerRegistry } from "../src/state/sidecarListenerRegistry.ts";

test("stale async binding is released immediately after dispose", () => {
  const registry = createSidecarListenerRegistry();
  const bindingId = registry.beginBinding();
  let released = 0;

  registry.dispose();

  const accepted = registry.resolveBinding(bindingId, () => {
    released += 1;
  });

  assert.equal(accepted, false);
  assert.equal(released, 1);
});

test("new active binding replaces the old listener only once", () => {
  const registry = createSidecarListenerRegistry();

  const firstBindingId = registry.beginBinding();
  let firstReleased = 0;
  assert.equal(
    registry.resolveBinding(firstBindingId, () => {
      firstReleased += 1;
    }),
    true
  );

  const secondBindingId = registry.beginBinding();
  let secondReleased = 0;
  assert.equal(
    registry.resolveBinding(secondBindingId, () => {
      secondReleased += 1;
    }),
    true
  );

  assert.equal(firstReleased, 1);
  assert.equal(secondReleased, 0);

  registry.dispose();

  assert.equal(firstReleased, 1);
  assert.equal(secondReleased, 1);
});
