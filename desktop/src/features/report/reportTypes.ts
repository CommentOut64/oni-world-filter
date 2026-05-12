export interface WorldReportGeyserRow {
  name: string;
  coord: string;
  temperature: string;
  eruptionRate: string;
  averageYield: string;
  eruptionWindow: string;
  activeWindow: string;
}

export interface WorldReportViewModel {
  worldCategoryLabel: string;
  worldName: string;
  coord: string;
  seedLabel: string;
  worldSizeLabel: string;
  startLabel: string;
  mixingSummary: string;
  geyserRows: WorldReportGeyserRow[];
}
