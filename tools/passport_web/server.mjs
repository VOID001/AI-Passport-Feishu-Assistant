import { EventEmitter } from "node:events";
import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";
import { StringDecoder } from "node:string_decoder";

import { createBridgeRunner, createDemoRunner } from "./bridge-runner.mjs";

const HOST = "127.0.0.1";
const PORT = Number(process.env.PASSPORT_WEB_PORT || 4177);
const DEFAULT_INTERVAL_SECONDS = 600;
const MINIMUM_INTERVAL_SECONDS = 60;
const MAXIMUM_INTERVAL_SECONDS = 3600;
const BODY_LIMIT = 16 * 1024;
const MAX_LOG_ENTRIES = 200;
const BLE_PROTOCOL_VERSION = 4;
const LOG_LEVELS = new Set(["info", "success", "warning", "error"]);
const PROGRESS_LOGS = Object.freeze({
  collecting: {
    scope: "DATA",
    message: "正在读取日历、近 30 天待办和未读计数",
  },
  summarizing: {
    scope: "DATA",
    message: "工作数据已读取，正在生成设备摘要",
  },
  connecting: {
    scope: "BLE",
    message: "正在扫描并连接 AI Passport BLE 服务",
  },
  transferring: {
    scope: "BLE",
    message: "BLE 已连接，正在分片写入并等待设备校验",
  },
  complete: {
    level: "success",
    scope: "BLE",
    message: "设备已返回 COMPLETE 确认",
  },
});
const directory = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.resolve(directory, "..", "..");
const publicRoot = path.join(directory, "public");
const vendorFile = path.join(directory, "node_modules", "lucide", "dist", "umd", "lucide.js");

function iso(value) {
  return value ? new Date(value).toISOString() : null;
}

function sanitizeLogMessage(value) {
  return String(value || "")
    .replace(/https?:\/\/[^\s]+/gi, "[已隐藏 URL]")
    .replace(
      /((?:access|refresh|user|tenant)[_-]?token|app[_-]?secret|authorization)\s*[:=]\s*[^\s,;]+/gi,
      "[凭证已隐藏]",
    )
    .replace(/[\r\n\t]+/g, " ")
    .trim()
    .slice(0, 320) || "状态已更新";
}

function detailNumber(details, key) {
  const value = details?.[key];
  return typeof value === "number" && Number.isFinite(value) ? value : 0;
}

function formatProgressLog(phase, progress) {
  const event = progress?.event;
  const details = progress?.details || {};
  switch (event) {
    case "sources_started":
      return {
        scope: "DATA",
        message:
          `开始并行读取 ${detailNumber(details, "source_count")} 个数据源` +
          `；目标日期 ${details.date || "今日"}`,
      };
    case "calendar_ready":
      return {
        scope: "CALENDAR",
        message:
          `owner 日历 ${detailNumber(details, "owner_calendar_count")} 个` +
          `；拉取 ${detailNumber(details, "fetched_event_count")} 条候选日程`,
      };
    case "profile_ready":
      return {
        scope: "PROFILE",
        message: details.avatar_present
          ? `用户资料读取完成；头像已转换为 ${detailNumber(details, "avatar_bytes")} bytes RGB565`
          : "用户资料读取完成；未获得头像，设备将使用文字占位",
      };
    case "tasks_ready":
      return {
        scope: "TASK",
        message:
          `近 30 天待办任务：${detailNumber(details, "overdue_task_count")} 条`,
      };
    case "unread_ready":
      return {
        scope: "UNREAD",
        message:
          `飞书 Dock 未读计数：${detailNumber(details, "unread_message_count")}`,
      };
    case "summary_ready":
      return {
        scope: "SUMMARY",
        message:
          `摘要筛选 ${detailNumber(details, "selected_event_count")}/` +
          `${detailNumber(details, "fetched_event_count")} 条日程` +
          `；待办 ${detailNumber(details, "selected_task_count")} 条` +
          `；已结束 ${detailNumber(details, "completed_event_count")}` +
          `；全天 ${detailNumber(details, "all_day_event_count")}`,
      };
    case "payload_ready":
      return {
        scope: "PROTOCOL",
        message:
          `v${detailNumber(details, "protocol_version")} JSON ` +
          `${detailNumber(details, "payload_bytes")}/` +
          `${detailNumber(details, "payload_limit_bytes")} bytes` +
          `；chunk ${detailNumber(details, "chunk_bytes")} bytes` +
          `；DATA ${detailNumber(details, "data_frame_count")} 帧` +
          `；CRC32 ${details.checksum_hex || "未知"}`,
      };
    case "scan_started":
      return {
        scope: "BLE",
        message:
          `扫描 ${details.device_name || "AI Passport"}` +
          `；模式 ${details.discovery_mode || "advertisement"}` +
          `；超时 ${detailNumber(details, "scan_timeout_seconds")} 秒`,
      };
    case "device_found":
      return {
        level: "success",
        scope: "BLE",
        message: `已发现匹配设备 ${details.device_name || "AI Passport"}`,
      };
    case "connected":
      return {
        level: "success",
        scope: "BLE",
        message:
          `BLE 连接已建立；连接超时上限 ` +
          `${detailNumber(details, "connect_timeout_seconds")} 秒`,
      };
    case "notifications_ready":
      return {
        scope: "BLE",
        message:
          `状态通知已启用；设备 ACK 超时 ` +
          `${detailNumber(details, "ack_timeout_seconds")} 秒`,
      };
    case "protocol_version_checked":
      return {
        level: "success",
        scope: "PROTOCOL",
        message:
          `BLE 协议已匹配：Host v${detailNumber(details, "host_protocol_version")}` +
          ` / Device v${detailNumber(details, "device_protocol_version")}`,
      };
    case "completion_queue_unavailable":
      return {
        level: "warning",
        scope: "TASK",
        message: "设备未提供待办完成队列；本次仅同步摘要",
      };
    case "completion_failed":
      return {
        level: "warning",
        scope: "TASK",
        message: "待办完成回传失败；设备会在下次同步时重试",
      };
    case "completion_ack_failed":
      return {
        level: "warning",
        scope: "TASK",
        message: "待办完成已提交，但设备确认失败；下次同步会重试",
      };
    case "transfer_started":
      return {
        scope: "TRANSFER",
        message:
          `开始传输 ${detailNumber(details, "payload_bytes")} bytes` +
          `；BEGIN + ${detailNumber(details, "data_frame_count")} DATA + COMMIT` +
          `；共 ${detailNumber(details, "total_frame_count")} 帧`,
      };
    case "begin_sent":
      return {
        scope: "TRANSFER",
        message:
          `BEGIN 已写入；payload ${detailNumber(details, "payload_bytes")} bytes` +
          `；CRC32 ${details.checksum_hex || "未知"}`,
      };
    case "chunk_sent":
      return {
        scope: "TRANSFER",
        message:
          `DATA ${detailNumber(details, "frame_index")}/` +
          `${detailNumber(details, "frame_total")} 已写入` +
          `；有效载荷 ${detailNumber(details, "data_bytes")} bytes`,
      };
    case "commit_sent":
      return {
        scope: "TRANSFER",
        message: "COMMIT 已写入；等待设备执行长度、序列与 CRC 校验",
      };
    case "ack_received":
      return {
        level: "success",
        scope: "BLE",
        message:
          `设备 ACK：${details.result || "COMPLETE"}` +
          "；长度、序列与 CRC 校验通过",
      };
    default:
      return PROGRESS_LOGS[phase] || {
        scope: "SYNC",
        message: "同步阶段已更新",
      };
  }
}

export class SyncController extends EventEmitter {
  constructor({
    runner,
    demoRunner = null,
    intervalSeconds = DEFAULT_INTERVAL_SECONDS,
    autoSync = true,
    now = () => Date.now(),
    setTimer = setTimeout,
    clearTimer = clearTimeout,
  }) {
    super();
    this.runner = runner;
    this.demoRunner = demoRunner;
    this.now = now;
    this.setTimer = setTimer;
    this.clearTimer = clearTimer;
    this.timer = null;
    this.abortController = null;
    this.activeTrigger = null;
    this.stopped = false;
    this.startedAt = this.now();
    this.settings = { autoSync, intervalSeconds };
    this.sync = {
      state: "idle",
      phase: "等待同步",
      lastAttemptAt: null,
      lastSuccessAt: null,
      error: null,
      payloadBytes: null,
    };
    this.summary = null;
    this.demo = { enabled: false, summary: null };
    this.deviceProtocolVersion = null;
    this.nextSyncAt = null;
    this.logs = [];
    this.nextLogId = 1;
  }

  snapshot() {
    return {
      service: {
        startedAt: iso(this.startedAt),
        autoSync: this.settings.autoSync,
        intervalSeconds: this.settings.intervalSeconds,
        nextSyncAt: iso(this.nextSyncAt),
        protocolVersion: BLE_PROTOCOL_VERSION,
        deviceProtocolVersion: this.deviceProtocolVersion,
        demoEnabled: this.demo.enabled,
      },
      sync: {
        ...this.sync,
        lastAttemptAt: iso(this.sync.lastAttemptAt),
        lastSuccessAt: iso(this.sync.lastSuccessAt),
      },
      summary: this.summary,
      logs: this.logs.map((entry) => ({ ...entry })),
    };
  }

  appendLog(level, scope, message) {
    const entry = {
      id: this.nextLogId,
      timestamp: iso(this.now()),
      level: LOG_LEVELS.has(level) ? level : "info",
      scope: String(scope || "SYSTEM")
        .toUpperCase()
        .replace(/[^A-Z0-9_-]/g, "")
        .slice(0, 16) || "SYSTEM",
      message: sanitizeLogMessage(message),
    };
    this.nextLogId += 1;
    this.logs.push(entry);
    if (this.logs.length > MAX_LOG_ENTRIES) {
      this.logs.splice(0, this.logs.length - MAX_LOG_ENTRIES);
    }
    return entry;
  }

  emitState() {
    this.emit("state", this.snapshot());
  }

  start() {
    this.stopped = false;
    this.appendLog(
      "info",
      "SERVICE",
      `本地服务已启动；自动同步${this.settings.autoSync ? "已启用" : "已暂停"}`,
    );
    if (this.settings.autoSync) {
      queueMicrotask(() => this.trigger("startup"));
    }
    this.emitState();
  }

  schedule() {
    if (this.timer) this.clearTimer(this.timer);
    this.timer = null;
    this.nextSyncAt = null;
    if (this.stopped || !this.settings.autoSync) {
      this.emitState();
      return;
    }
    this.nextSyncAt = this.now() + this.settings.intervalSeconds * 1000;
    this.timer = this.setTimer(() => this.trigger("schedule"), this.settings.intervalSeconds * 1000);
    this.timer?.unref?.();
    this.appendLog(
      "info",
      "SCHEDULE",
      `下一次自动同步将在 ${this.settings.intervalSeconds} 秒后执行`,
    );
    this.emitState();
  }

  updateSettings({ autoSync, intervalSeconds }) {
    if (typeof autoSync !== "boolean") {
      throw new TypeError("autoSync 必须是布尔值。");
    }
    if (
      !Number.isInteger(intervalSeconds) ||
      intervalSeconds < MINIMUM_INTERVAL_SECONDS ||
      intervalSeconds > MAXIMUM_INTERVAL_SECONDS
    ) {
      throw new TypeError("同步间隔必须在 60 到 3600 秒之间。");
    }
    this.settings = { autoSync, intervalSeconds };
    this.appendLog(
      "info",
      "SETTINGS",
      `自动同步${autoSync ? "已启用" : "已暂停"}；间隔 ${intervalSeconds} 秒`,
    );
    this.schedule();
  }

  updateDemo({ enabled, summary }) {
    if (typeof enabled !== "boolean") {
      throw new TypeError("演示模式必须是布尔值。");
    }
    if (enabled && (!summary || typeof summary !== "object" || Array.isArray(summary))) {
      throw new TypeError("演示摘要无效。");
    }
    this.demo = { enabled, summary: enabled ? summary : null };
    if (enabled) {
      this.settings = { ...this.settings, autoSync: false };
      this.appendLog("info", "DEMO", "演示模式已启用；自动同步已暂停");
    } else {
      this.appendLog("info", "DEMO", "演示模式已关闭；将使用飞书工作数据");
    }
    this.schedule();
  }

  trigger(source = "manual") {
    if (this.stopped) {
      return Promise.resolve(false);
    }
    if (this.sync.state === "syncing") {
      this.appendLog("warning", "SYNC", "已有同步任务运行，本次请求已忽略");
      this.emitState();
      return Promise.resolve(false);
    }
    const operation = this.performTrigger(source);
    this.activeTrigger = operation;
    void operation.finally(() => {
      if (this.activeTrigger === operation) this.activeTrigger = null;
    });
    return operation;
  }

  async performTrigger(source) {
    const sourceLabels = {
      manual: "手动",
      schedule: "定时",
      startup: "启动",
    };
    this.appendLog(
      "info",
      "SYNC",
      `${sourceLabels[source] || "系统"}同步已开始`,
    );
    if (this.timer) this.clearTimer(this.timer);
    this.timer = null;
    this.nextSyncAt = null;
    this.abortController = new AbortController();
    this.sync = {
      ...this.sync,
      state: "syncing",
      phase: source === "manual" ? "手动同步中" : "自动同步中",
      lastAttemptAt: this.now(),
      error: null,
    };
    this.emitState();
    if (this.stopped) return false;
    let lastProgressKey = null;
    try {
      const runner = this.demo.enabled ? this.demoRunner : this.runner;
      if (!runner) throw new Error("演示同步程序不可用。");
      const result = await runner({
        summary: this.demo.summary,
        signal: this.abortController.signal,
        onProgress: (phase, progress = null) => {
          if (this.stopped || this.sync.state !== "syncing") return;
          const progressKey = [
            phase,
            progress?.event || "",
            progress?.details?.frame_index || "",
          ].join(":");
          if (progressKey !== lastProgressKey) {
            const log = formatProgressLog(phase, progress);
            this.appendLog(
              log.level || "info",
              log.scope,
              log.message,
            );
            lastProgressKey = progressKey;
          }
          if (progress?.event === "protocol_version_checked") {
            this.deviceProtocolVersion =
              detailNumber(progress.details, "device_protocol_version") || null;
          }
          this.sync = { ...this.sync, phase };
          this.emitState();
        },
      });
      this.summary = result.summary;
      const eventCount = Array.isArray(result.summary?.events)
        ? result.summary.events.length
        : 0;
      const taskCount = Array.isArray(result.summary?.tasks)
        ? result.summary.tasks.length
        : 0;
      this.appendLog(
        "success",
        "SYNC",
        `同步完成：${eventCount} 条日程，${taskCount} 条待办，` +
          `${result.payload_bytes} bytes`,
      );
      this.sync = {
        state: "success",
        phase: "卡片已确认",
        lastAttemptAt: this.sync.lastAttemptAt,
        lastSuccessAt: this.now(),
        error: null,
        payloadBytes: result.payload_bytes,
      };
    } catch (error) {
      const message = error?.message || "同步失败，请重试。";
      this.appendLog("error", "SYNC", `同步失败：${message}`);
      this.sync = {
        ...this.sync,
        state: "error",
        phase: "同步失败",
        error: message,
      };
    } finally {
      this.abortController = null;
      if (!this.stopped) this.schedule();
    }
    return true;
  }

  stop() {
    this.stopped = true;
    this.appendLog("info", "SERVICE", "本地服务正在停止");
    if (this.timer) this.clearTimer(this.timer);
    this.timer = null;
    this.abortController?.abort();
    this.nextSyncAt = null;
    this.emitState();
    return this.activeTrigger || Promise.resolve();
  }
}

function sendJson(response, status, value) {
  const body = JSON.stringify(value);
  response.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
    "Cache-Control": "no-store",
    "Content-Security-Policy": "default-src 'none'",
    "X-Content-Type-Options": "nosniff",
  });
  response.end(body);
}

async function readJson(request) {
  if (!request.headers["content-type"]?.startsWith("application/json")) {
    throw Object.assign(new Error("请求必须使用 JSON。"), { status: 415 });
  }
  let body = "";
  const decoder = new StringDecoder("utf8");
  for await (const chunk of request) {
    body += decoder.write(chunk);
    if (Buffer.byteLength(body) > BODY_LIMIT) {
      throw Object.assign(new Error("请求内容过大。"), { status: 413 });
    }
  }
  body += decoder.end();
  try {
    return body ? JSON.parse(body) : {};
  } catch {
    throw Object.assign(new Error("JSON 格式无效。"), { status: 400 });
  }
}

function trustedHost(request) {
  const port = request.socket.localPort;
  const host = request.headers.host;
  return host === `127.0.0.1:${port}` || host === `localhost:${port}`;
}

function sameOrigin(request) {
  const origin = request.headers.origin;
  if (!origin) return true;
  try {
    const parsed = new URL(origin);
    return (
      parsed.protocol === "http:" &&
      (parsed.hostname === "127.0.0.1" || parsed.hostname === "localhost") &&
      Number(parsed.port || 80) === request.socket.localPort
    );
  } catch {
    return false;
  }
}

async function serveFile(response, filePath, contentType) {
  try {
    const info = await stat(filePath);
    response.writeHead(200, {
      "Content-Type": contentType,
      "Content-Length": info.size,
      "Cache-Control": "no-store",
      "Content-Security-Policy":
        "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'",
      "X-Content-Type-Options": "nosniff",
      "Referrer-Policy": "no-referrer",
    });
    createReadStream(filePath).pipe(response);
  } catch {
    sendJson(response, 404, { error: "资源不存在。" });
  }
}

export function createPassportServer({ controller }) {
  const clients = new Set();
  const onState = (state) => {
    const message = `data: ${JSON.stringify(state)}\n\n`;
    for (const client of clients) client.write(message);
  };
  controller.on("state", onState);
  const closeEventClients = () => {
    for (const client of clients) {
      client.end(() => client.socket?.end());
    }
    clients.clear();
  };

  const server = http.createServer(async (request, response) => {
    try {
      if (!trustedHost(request)) {
        return sendJson(response, 421, { error: "请求 Host 无效。" });
      }
      const url = new URL(
        request.url || "/",
        `http://127.0.0.1:${request.socket.localPort}`,
      );
      if (request.method === "GET" && url.pathname === "/api/status") {
        return sendJson(response, 200, controller.snapshot());
      }
      if (request.method === "GET" && url.pathname === "/healthz") {
        return sendJson(response, 200, { ok: true });
      }
      if (request.method === "GET" && url.pathname === "/api/events") {
        response.writeHead(200, {
          "Content-Type": "text/event-stream",
          "Cache-Control": "no-store",
          Connection: "close",
          "X-Content-Type-Options": "nosniff",
        });
        clients.add(response);
        response.write(`data: ${JSON.stringify(controller.snapshot())}\n\n`);
        request.on("close", () => clients.delete(response));
        return;
      }
      if (
        (request.method === "POST" || request.method === "PUT") &&
        url.pathname.startsWith("/api/") &&
        !sameOrigin(request)
      ) {
        return sendJson(response, 403, { error: "拒绝跨源请求。" });
      }
      if (request.method === "POST" && url.pathname === "/api/sync") {
        await readJson(request);
        if (controller.sync.state === "syncing") {
          return sendJson(response, 409, { error: "同步正在进行。" });
        }
        void controller.trigger("manual");
        return sendJson(response, 202, { accepted: true });
      }
      if (request.method === "PUT" && url.pathname === "/api/demo") {
        const body = await readJson(request);
        controller.updateDemo(body);
        return sendJson(response, 200, controller.snapshot());
      }
      if (request.method === "PUT" && url.pathname === "/api/settings") {
        const body = await readJson(request);
        controller.updateSettings(body);
        return sendJson(response, 200, controller.snapshot());
      }
      if (request.method !== "GET") {
        return sendJson(response, 405, { error: "不支持该请求方法。" });
      }
      if (url.pathname === "/" || url.pathname === "/index.html") {
        return serveFile(response, path.join(publicRoot, "index.html"), "text/html; charset=utf-8");
      }
      if (url.pathname === "/styles.css") {
        return serveFile(response, path.join(publicRoot, "styles.css"), "text/css; charset=utf-8");
      }
      if (url.pathname === "/app.js") {
        return serveFile(response, path.join(publicRoot, "app.js"), "text/javascript; charset=utf-8");
      }
      if (url.pathname === "/vendor/lucide.js") {
        return serveFile(response, vendorFile, "text/javascript; charset=utf-8");
      }
      return sendJson(response, 404, { error: "页面不存在。" });
    } catch (error) {
      return sendJson(response, error.status || 400, { error: error.message || "请求失败。" });
    }
  });

  server.on("close", () => {
    controller.off("state", onState);
    closeEventClients();
  });
  server.closeEventClients = closeEventClients;
  return server;
}

export async function startServer() {
  const runner = createBridgeRunner({ projectRoot });
  const demoRunner = createDemoRunner({ projectRoot });
  const controller = new SyncController({ runner, demoRunner });
  const server = createPassportServer({ controller });
  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(PORT, HOST, resolve);
  });
  controller.start();
  const url = `http://${HOST}:${PORT}`;
  console.log(`AI Passport local service: ${url}`);
  if (process.platform === "darwin" && process.env.PASSPORT_NO_OPEN !== "1") {
    spawn("/usr/bin/open", [url], { detached: true, stdio: "ignore" }).unref();
  }
  let shuttingDown = false;
  const shutdown = async () => {
    if (shuttingDown) return;
    shuttingDown = true;
    const forcedExit = setTimeout(() => process.exit(1), 4000);
    forcedExit.unref();
    const stopped = controller.stop();
    server.closeEventClients();
    const closed = new Promise((resolve) => server.close(resolve));
    server.closeIdleConnections?.();
    await Promise.allSettled([stopped, closed]);
    clearTimeout(forcedExit);
    process.exit(0);
  };
  process.once("SIGINT", shutdown);
  process.once("SIGTERM", shutdown);
  return { server, controller, url };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  await startServer();
}
