export interface DistanceRule {
  geyser: string;
  minDist: number;
  maxDist: number;
}

export interface SearchConstraints {
  required: string[];
  forbidden: string[];
  distance: DistanceRule[];
}

export type CpuMode = "balanced" | "turbo" | "custom";

export interface SearchCpuConfig {
  mode: CpuMode;
  workers: number;
  allowSmt: boolean;
  allowLowPerf: boolean;
  placement: "preferred" | "strict" | "none";
  enableWarmup: boolean;
  enableAdaptiveDown: boolean;
  chunkSize: number;
  progressInterval: number;
  sampleWindowMs: number;
  adaptiveMinWorkers: number;
  adaptiveDropThreshold: number;
  adaptiveDropWindows: number;
  adaptiveCooldownMs: number;
}

export interface SearchRequest {
  jobId: string;
  worldType: number;
  seedStart: number;
  seedEnd: number;
  mixing: number;
  threads: number;
  constraints: SearchConstraints;
  cpu?: SearchCpuConfig;
}

export interface PreviewRequest {
  jobId: string;
  worldType: number;
  seed: number;
  mixing: number;
}

export interface WorldOption {
  id: number;
  code: string;
}

export interface GeyserOption {
  id: number;
  key: string;
}

export interface TraitMeta {
  id: string;
  name: string;
  description: string;
  traitTags: string[];
  exclusiveWith: string[];
  exclusiveWithTags: string[];
  forbiddenDLCIds: string[];
  effectSummary: string[];
  searchable: boolean;
}

export interface MixingSlotMeta {
  slot: number;
  path: string;
  type: string;
  name: string;
  description: string;
}

export interface ParameterSpec {
  id: string;
  valueType: string;
  meaning: string;
  staticRange: string;
  supportsDynamicRange: boolean;
  source: string;
}

export interface SearchCatalog {
  worlds: WorldOption[];
  geysers: GeyserOption[];
  traits: TraitMeta[];
  mixingSlots: MixingSlotMeta[];
  parameterSpecs: ParameterSpec[];
}

export interface Point {
  x: number;
  y: number;
}

export interface WorldSize {
  w: number;
  h: number;
}

export interface GeyserSummary {
  type: number;
  x: number;
  y: number;
  id?: string;
}

export interface SearchMatchPayload {
  start: Point;
  worldSize: WorldSize;
  traits: number[];
  geysers: GeyserSummary[];
}

export interface SearchStartedEvent {
  event: "started";
  jobId: string;
  seedStart: number;
  seedEnd: number;
  totalSeeds: number;
  workerCount: number;
}

export interface SearchProgressEvent {
  event: "progress";
  jobId: string;
  processedSeeds: number;
  totalSeeds: number;
  totalMatches: number;
  activeWorkers: number;
  hasWindowSample: boolean;
  windowSeedsPerSecond?: number;
  activeWorkersReduced: boolean;
  peakSeedsPerSecond: number;
}

export interface SearchMatchEvent {
  event: "match";
  jobId: string;
  seed: number;
  processedSeeds: number;
  totalSeeds: number;
  totalMatches: number;
  summary: SearchMatchPayload;
}

export interface ThroughputPayload {
  averageSeedsPerSecond: number;
  stddevSeedsPerSecond: number;
  processedSeeds: number;
  valid: boolean;
}

export interface SearchCompletedEvent {
  event: "completed";
  jobId: string;
  processedSeeds: number;
  totalSeeds: number;
  totalMatches: number;
  finalActiveWorkers: number;
  autoFallbackCount: number;
  stoppedByBudget: boolean;
  throughput: ThroughputPayload;
}

export interface SearchCancelledEvent {
  event: "cancelled";
  jobId: string;
  processedSeeds: number;
  totalSeeds: number;
  totalMatches: number;
  finalActiveWorkers: number;
}

export interface SearchFailedEvent {
  event: "failed";
  jobId: string;
  message: string;
  processedSeeds?: number;
  totalSeeds?: number;
}

export interface PreviewPolygon {
  hasHole: boolean;
  zoneType: number;
  vertices: [number, number][];
}

export interface PreviewSummary {
  seed: number;
  worldType: number;
  start: Point;
  worldSize: WorldSize;
  traits: number[];
  geysers: GeyserSummary[];
}

export interface PreviewPayload {
  summary: PreviewSummary;
  polygons: PreviewPolygon[];
}

export interface PreviewEvent {
  event: "preview";
  jobId: string;
  worldType: number;
  seed: number;
  mixing: number;
  preview: PreviewPayload;
}

export interface SidecarStderrEvent {
  jobId: string;
  message: string;
}

export type SidecarEvent =
  | SearchStartedEvent
  | SearchProgressEvent
  | SearchMatchEvent
  | SearchCompletedEvent
  | SearchCancelledEvent
  | SearchFailedEvent
  | PreviewEvent;

export interface SearchMatchSummary {
  seed: number;
  worldType: number;
  mixing: number;
  coord: string;
  traits: number[];
  start: Point;
  worldSize: WorldSize;
  geysers: GeyserSummary[];
  nearestDistance: number | null;
}

export interface SearchError {
  message: string;
  code?: string;
}
