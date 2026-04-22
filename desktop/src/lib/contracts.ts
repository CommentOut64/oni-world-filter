export interface DistanceRule {
  geyser: string;
  minDist: number;
  maxDist: number;
}

export interface CountRule {
  geyser: string;
  minCount: number;
  maxCount: number;
}

export interface SearchConstraints {
  required: string[];
  forbidden: string[];
  distance: DistanceRule[];
  count: CountRule[];
}

export type CpuMode = "balanced" | "turbo";

export interface SearchCpuConfig {
  mode: CpuMode;
  allowSmt: boolean;
  allowLowPerf: boolean;
  placement: "preferred" | "strict" | "none";
}

export interface SearchRequest {
  jobId: string;
  worldType: number;
  seedStart: number;
  seedEnd: number;
  mixing: number;
  constraints: SearchConstraints;
  cpu?: SearchCpuConfig;
}

export interface SearchAnalyzeRequest extends SearchRequest {
  command?: "analyze_search_request";
}

export interface ValidationIssue {
  layer: string;
  code: string;
  field: string;
  message: string;
}

export interface NormalizedConstraintGroup {
  geyserId: string;
  geyserIndex: number;
  minCount: number;
  maxCount: number;
  hasRequired: boolean;
  hasForbidden: boolean;
  hasExplicitCount: boolean;
  distance: DistanceRule[];
}

export interface NormalizedSearchRequestPayload {
  worldType: number;
  seedStart: number;
  seedEnd: number;
  mixing: number;
  groups: NormalizedConstraintGroup[];
}

export interface SearchAnalysisPayload {
  worldProfile: WorldEnvelopeProfile;
  normalizedRequest: NormalizedSearchRequestPayload;
  errors: ValidationIssue[];
  warnings: ValidationIssue[];
  bottlenecks: string[];
  predictedBottleneckProbability: number;
}

export interface SourceSummary {
  ruleId: string;
  templateName: string;
  geyserId: string;
  upperBound: number;
  sourceKind: string;
  poolId: string;
}

export interface SpatialEnvelope {
  envelopeId: string;
  confidence: string;
  method: string;
}

export interface WorldEnvelopeProfile {
  valid: boolean;
  worldType: number;
  worldCode: string;
  width: number;
  height: number;
  diagonal: number;
  activeMixingSlots: number[];
  disabledMixingSlots: number[];
  possibleGeyserTypes: string[];
  impossibleGeyserTypes: string[];
  possibleMaxCountByType: Record<string, number>;
  genericTypeUpperById: Record<string, number>;
  genericSlotUpper: number;
  exactSourceSummary: SourceSummary[];
  genericSourceSummary: SourceSummary[];
  sourcePools: SourcePool[];
  spatialEnvelopes: SpatialEnvelope[];
}

export interface SourcePool {
  poolId: string;
  sourceKind: string;
  capacityUpper: number;
}

export interface SearchAnalysisEvent {
  event: "search_analysis";
  jobId: string;
  analysis: SearchAnalysisPayload;
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
