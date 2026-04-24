import test from "node:test";
import assert from "node:assert/strict";

import { fitToWorld } from "../src/features/preview/viewport.ts";
import * as viewportModule from "../src/features/preview/viewport.ts";

test("preview viewport resize policy keeps user viewport after manual interaction", () => {
  const reconcileViewportOnStageResize = (
    viewportModule as Record<string, unknown>
  )["reconcileViewportOnStageResize"];

  assert.equal(typeof reconcileViewportOnStageResize, "function");

  const currentViewport = {
    scale: 2.4,
    x: -180,
    y: -96,
  };
  const fittedViewport = fitToWorld({
    worldWidth: 256,
    worldHeight: 384,
    stageWidth: 720,
    stageHeight: 640,
  });

  assert.deepEqual(
    (
      reconcileViewportOnStageResize as (input: {
        hasManualViewportInteraction: boolean;
        currentViewport: { scale: number; x: number; y: number };
        fittedViewport: { scale: number; x: number; y: number };
      }) => { scale: number; x: number; y: number }
    )({
      hasManualViewportInteraction: true,
      currentViewport,
      fittedViewport,
    }),
    currentViewport
  );
});

test("preview viewport resize policy still auto fits before user interaction", () => {
  const reconcileViewportOnStageResize = (
    viewportModule as Record<string, unknown>
  )["reconcileViewportOnStageResize"];

  assert.equal(typeof reconcileViewportOnStageResize, "function");

  const currentViewport = {
    scale: 1.2,
    x: 10,
    y: 20,
  };
  const fittedViewport = fitToWorld({
    worldWidth: 256,
    worldHeight: 384,
    stageWidth: 720,
    stageHeight: 640,
  });

  assert.deepEqual(
    (
      reconcileViewportOnStageResize as (input: {
        hasManualViewportInteraction: boolean;
        currentViewport: { scale: number; x: number; y: number };
        fittedViewport: { scale: number; x: number; y: number };
      }) => { scale: number; x: number; y: number }
    )({
      hasManualViewportInteraction: false,
      currentViewport,
      fittedViewport,
    }),
    fittedViewport
  );
});

test("preview interaction reset policy only resets on preview session change", () => {
  const shouldResetPreviewInteractionState = (
    viewportModule as Record<string, unknown>
  )["shouldResetPreviewInteractionState"];

  assert.equal(typeof shouldResetPreviewInteractionState, "function");
  assert.equal(
    (
      shouldResetPreviewInteractionState as (input: {
        previousSessionKey: string | null;
        nextSessionKey: string | null;
      }) => boolean
    )({
      previousSessionKey: "13:100001",
      nextSessionKey: "13:100001",
    }),
    false
  );
  assert.equal(
    (
      shouldResetPreviewInteractionState as (input: {
        previousSessionKey: string | null;
        nextSessionKey: string | null;
      }) => boolean
    )({
      previousSessionKey: "13:100001",
      nextSessionKey: "13:100002",
    }),
    true
  );
});
