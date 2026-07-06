import { spawn } from "node:child_process";
import path from "node:path";
import readline from "node:readline";

export class SidecarClient {
  constructor(sidecarPath) {
    this.sidecarPath = sidecarPath;
    this.child = null;
    this.readline = null;
    this.pending = null;
    this.nextId = 1;
    this.closed = false;
    this.exitPromise = null;
  }

  async start() {
    if (this.child) {
      return;
    }
    this.child = spawn(this.sidecarPath, [], {
      cwd: path.dirname(this.sidecarPath),
      stdio: ["pipe", "pipe", "pipe"],
      windowsHide: true,
    });
    this.child.stderr.setEncoding("utf8");
    this.child.stdout.setEncoding("utf8");
    this.readline = readline.createInterface({ input: this.child.stdout });
    this.readline.on("line", (line) => this.#handleLine(line));
    this.exitPromise = new Promise((resolve) => {
      this.child.once("exit", (code, signal) => {
        const error =
          code === 0
            ? null
            : new Error(`oni-sidecar.exe 异常退出: code=${code ?? "null"} signal=${signal ?? "null"}`);
        if (this.pending) {
          this.pending.reject(error ?? new Error("oni-sidecar.exe 提前退出"));
          this.pending = null;
        }
        resolve();
      });
    });
  }

  #handleLine(line) {
    if (!this.pending) {
      return;
    }
    let payload;
    try {
      payload = JSON.parse(line);
    } catch (error) {
      this.pending.reject(new Error(`sidecar 输出非法 JSON: ${line}`));
      this.pending = null;
      return;
    }

    if (payload.jobId !== this.pending.jobId) {
      this.pending.reject(
        new Error(`sidecar jobId 不匹配，expected=${this.pending.jobId} actual=${payload.jobId}`)
      );
      this.pending = null;
      return;
    }

    if (payload.event === "failed") {
      this.pending.reject(new Error(payload.message ?? "sidecar 请求失败"));
      this.pending = null;
      return;
    }

    this.pending.resolve(payload);
    this.pending = null;
  }

  async request(command) {
    await this.start();
    if (this.pending) {
      throw new Error("sidecar 当前已有未完成请求");
    }
    const jobId = `worldgen-diff-${this.nextId++}`;
    const payload = { ...command, jobId };
    const promise = new Promise((resolve, reject) => {
      this.pending = { jobId, resolve, reject };
    });
    this.child.stdin.write(`${JSON.stringify(payload)}\n`, "utf8");
    return promise;
  }

  async getCatalog() {
    const response = await this.request({ command: "get_search_catalog" });
    return response.catalog;
  }

  async preview(worldType, seed, mixing, target = "primary") {
    return this.request({
      command: "preview",
      worldType,
      seed,
      mixing,
      target,
    });
  }

  async close() {
    if (this.closed) {
      return;
    }
    this.closed = true;
    if (!this.child) {
      return;
    }
    this.child.stdin.end();
    await this.exitPromise;
    this.readline?.close();
    this.child = null;
  }
}

export async function loadCatalogFromSidecar(sidecarPath) {
  const client = new SidecarClient(sidecarPath);
  try {
    return await client.getCatalog();
  } finally {
    await client.close();
  }
}
