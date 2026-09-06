import { execFile, spawn } from "node:child_process";
import { access } from "node:fs/promises";
import path from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const OUTPUT_LIMIT = 256 * 1024;
const PROCESS_GROUP_GRACE_MS = 2000;
const PROCESS_GROUP_KILL_WAIT_MS = 1000;
const PROCESS_GROUP_POLL_MS = 25;
const PROGRESS_DETAIL_FIELDS = new Set([
  "ack_timeout_seconds",
  "all_day_event_count",
  "avatar_bytes",
  "avatar_present",
  "checksum_hex",
  "chunk_bytes",
  "completed_event_count",
  "connect_timeout_seconds",
  "data_bytes",
  "data_frame_count",
  "date",
  "device_name",
  "discovery_mode",
  "fetched_event_count",
  "frame_index",
  "frame_total",
  "overdue_task_count",
  "owner_calendar_count",
  "payload_bytes",
  "payload_limit_bytes",
  "protocol_version",
  "result",
  "scan_timeout_seconds",
  "selected_event_count",
  "selected_task_count",
  "source_count",
  "total_frame_count",
  "unread_message_count",
]);

function normalizeProgress(message) {
  const details = {};
  if (message?.details && typeof message.details === "object") {
    for (const [key, value] of Object.entries(message.details)) {
      if (!PROGRESS_DETAIL_FIELDS.has(key)) continue;
      if (typeof value === "number" && Number.isFinite(value)) {
        details[key] = value;
      } else if (typeof value === "boolean") {
        details[key] = value;
      } else if (
        typeof value === "string" &&
        value.length <= 64 &&
        !/https?:\/\/|token|secret|authorization/i.test(value)
      ) {
        details[key] = value;
      }
    }
  }
  return {
    phase: message.phase.slice(0, 32),
    event:
      typeof message.event === "string"
        ? message.event.replace(/[^a-z0-9_-]/gi, "").slice(0, 48)
        : null,
    details,
  };
}

function signalProcessGroup(child, signal) {
  if (!child?.pid) return;
  try {
    process.kill(-child.pid, signal);
  } catch {
    if (child.exitCode === null) {
      try {
        child.kill(signal);
      } catch {
        // The process already exited.
      }
    }
  }
}

function processGroupExists(processGroupId) {
  try {
    process.kill(-processGroupId, 0);
    return true;
  } catch (error) {
    if (error?.code === "ESRCH") return false;
    return true;
  }
}

async function waitForProcessGroupExit(processGroupId, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (processGroupExists(processGroupId)) {
    if (Date.now() >= deadline) return false;
    await delay(PROCESS_GROUP_POLL_MS);
  }
  return true;
}

export function executeBridge(command, args, options, { signal, onProgress }) {
  return new Promise((resolve, reject) => {
    const { timeout: timeoutMs, ...spawnOptions } = options;
    const child = spawn(command, args, {
      ...spawnOptions,
      detached: true,
      stdio: ["ignore", "pipe", "pipe"],
    });
    let stdout = "";
    let stderr = "";
    let lineBuffer = "";
    let timedOut = false;
    let aborted = false;
    let outputExceeded = false;
    let processGroupCleanup = null;

    const stopProcessGroup = () => {
      if (processGroupCleanup) return processGroupCleanup;
      const processGroupId = child.pid;
      processGroupCleanup = (async () => {
        signalProcessGroup(child, "SIGTERM");
        if (
          !processGroupId ||
          await waitForProcessGroupExit(processGroupId, PROCESS_GROUP_GRACE_MS)
        ) {
          return;
        }
        signalProcessGroup(child, "SIGKILL");
        await waitForProcessGroupExit(
          processGroupId,
          PROCESS_GROUP_KILL_WAIT_MS,
        );
      })();
      return processGroupCleanup;
    };

    const timeout = setTimeout(() => {
      timedOut = true;
      stopProcessGroup();
    }, timeoutMs);
    timeout.unref();

    const abort = () => {
      aborted = true;
      stopProcessGroup();
    };
    signal?.addEventListener("abort", abort, { once: true });

    child.stdout.on("data", (chunk) => {
      const text = chunk.toString("utf8");
      stdout += text;
      lineBuffer += text;
      const lines = lineBuffer.split(/\r?\n/);
      lineBuffer = lines.pop() || "";
      for (const line of lines) {
        try {
          const message = JSON.parse(line);
          if (message?.type === "progress" && typeof message.phase === "string") {
            const progress = normalizeProgress(message);
            onProgress?.(progress.phase, progress);
          }
        } catch {
          // Final human-readable output is parsed after process exit.
        }
      }
      if (Buffer.byteLength(stdout) > OUTPUT_LIMIT) {
        outputExceeded = true;
        stopProcessGroup();
      }
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString("utf8");
      if (Buffer.byteLength(stderr) > OUTPUT_LIMIT) {
        outputExceeded = true;
        stopProcessGroup();
      }
    });
    child.once("error", (error) => {
      clearTimeout(timeout);
      signal?.removeEventListener("abort", abort);
      reject(error);
    });
    child.once("close", async (code) => {
      clearTimeout(timeout);
      signal?.removeEventListener("abort", abort);
      if (processGroupCleanup) await processGroupCleanup;
      if (aborted) {
        const error = new Error("同步已停止。");
        error.name = "AbortError";
        reject(error);
      } else if (timedOut) {
        const error = new Error("同步超时。");
        error.killed = true;
        reject(error);
      } else if (outputExceeded) {
        reject(Object.assign(new Error("同步进程输出过大。"), { stdout }));
      } else if (code !== 0) {
        reject(Object.assign(new Error("同步进程失败。"), { stdout }));
      } else {
        resolve({ stdout });
      }
    });
    if (signal?.aborted) abort();
  });
}

async function exists(filePath) {
  try {
    await access(filePath);
    return true;
  } catch {
    return false;
  }
}

export async function resolvePython(environment = process.env) {
  const candidates = [
    environment.PASSPORT_PYTHON,
    "/tmp/ai-passport-ble-venv/bin/python",
    "python3",
  ].filter(Boolean);
  for (const candidate of candidates) {
    if (!candidate.includes("/") || (await exists(candidate))) return candidate;
  }
  return "python3";
}

export async function resolveLarkCli(environment = process.env) {
  if (environment.LARK_CLI) return environment.LARK_CLI;
  try {
    const { stdout } = await execFileAsync("/usr/bin/which", ["lark-cli"], {
      timeout: 3000,
      maxBuffer: 4096,
    });
    const result = stdout.trim();
    if (result) return result;
  } catch {
    // The sanitized error below is more useful than raw process output.
  }
  throw new Error("未找到 lark-cli，请先安装并完成用户登录。");
}

export function parseBridgeOutput(stdout) {
  const lines = stdout
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
  for (let index = lines.length - 1; index >= 0; index -= 1) {
    try {
      const result = JSON.parse(lines[index]);
      if (result && typeof result.ok === "boolean") return result;
    } catch {
      // Ignore non-JSON child output and continue looking for the result line.
    }
  }
  throw new Error("同步程序没有返回可识别的结果。");
}

export function createBridgeRunner({
  projectRoot,
  environment = process.env,
  timeoutMs = 90000,
} = {}) {
  if (!projectRoot) throw new TypeError("projectRoot is required");
  const workingDirectory = path.resolve(projectRoot);

  return async function runBridge({ signal, onProgress } = {}) {
    const python = await resolvePython(environment);
    const larkCli = await resolveLarkCli(environment);
    const args = [
      "-m",
      "tools.passport_bridge",
      "--json-output",
      "--progress-json",
      "--lark-cli",
      larkCli,
    ];
    if (environment.PASSPORT_DEVICE) {
      args.push("--device", environment.PASSPORT_DEVICE);
    }
    try {
      const { stdout } = await executeBridge(python, args, {
        cwd: workingDirectory,
        env: environment,
        timeout: timeoutMs,
      }, { signal, onProgress });
      const result = parseBridgeOutput(stdout);
      if (!result.ok) throw new Error(result.error || "同步失败。");
      return result;
    } catch (error) {
      if (error?.name === "AbortError") throw new Error("同步已停止。");
      if (error?.killed) throw new Error("同步超时，请确认卡片处于 BLE 页面。");
      try {
        const parsed = parseBridgeOutput(error?.stdout || "");
        throw new Error(parsed.error || "同步失败，请检查飞书登录和卡片连接。");
      } catch (parseError) {
        if (parseError?.message !== "同步程序没有返回可识别的结果。") {
          throw parseError;
        }
        throw new Error("同步失败，请检查飞书登录和卡片连接。");
      }
    }
  };
}

export function createDemoRunner({
  projectRoot,
  environment = process.env,
  timeoutMs = 90000,
} = {}) {
  if (!projectRoot) throw new TypeError("projectRoot is required");
  const workingDirectory = path.resolve(projectRoot);

  return async function runDemo({ summary, signal, onProgress } = {}) {
    if (!summary || typeof summary !== "object") {
      throw new TypeError("演示摘要无效。");
    }
    const python = await resolvePython(environment);
    const args = [
      "-m",
      "tools.passport_bridge.demo",
      "--json-output",
      "--progress-json",
      "--summary-base64",
      Buffer.from(JSON.stringify(summary), "utf8").toString("base64"),
    ];
    if (environment.PASSPORT_DEVICE) args.push("--device", environment.PASSPORT_DEVICE);
    try {
      const { stdout } = await executeBridge(python, args, {
        cwd: workingDirectory,
        env: environment,
        timeout: timeoutMs,
      }, { signal, onProgress });
      const result = parseBridgeOutput(stdout);
      if (!result.ok) throw new Error(result.error || "演示数据同步失败。");
      return result;
    } catch (error) {
      if (error?.name === "AbortError") throw new Error("同步已停止。");
      if (error?.killed) throw new Error("同步超时，请确认卡片处于 BLE 页面。");
      try {
        const parsed = parseBridgeOutput(error?.stdout || "");
        throw new Error(parsed.error || "演示数据同步失败。");
      } catch (parseError) {
        if (parseError?.message !== "同步程序没有返回可识别的结果。") {
          throw parseError;
        }
        throw new Error("演示数据同步失败，请检查卡片连接和输入内容。");
      }
    }
  };
}
