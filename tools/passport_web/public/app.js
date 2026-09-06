"use strict";

const API = Object.freeze({
  status: "/api/status",
  sync: "/api/sync",
  settings: "/api/settings",
  demo: "/api/demo",
  events: "/api/events",
});
const DEMO_REVEAL_CLICKS = 5;
const DEMO_REVEAL_WINDOW_MS = 3000;
export const DEMO_LIMITS = Object.freeze({
  events: 16,
  eventTitleBytes: 255,
  tasks: 16,
  taskTitleBytes: 127,
  payloadBytes: 12 * 1024,
});
const UTF8_ENCODER = new TextEncoder();

const PHASES = Object.freeze([
  {
    title: "读取工作数据",
    detail: "正在读取日程、任务与消息计数。",
    aliases: ["start", "queue", "prepare", "fetch", "collect", "lark", "feishu", "calendar", "读取", "收集"],
  },
  {
    title: "生成今日摘要",
    detail: "正在整理卡片需要显示的今日内容。",
    aliases: ["summary", "build", "format", "serial", "encode", "payload", "整理", "摘要", "生成"],
  },
  {
    title: "连接卡片",
    detail: "正在发现 AI Passport 并建立 BLE 连接。",
    aliases: ["scan", "discover", "connect", "device", "bluetooth", "ble", "发现", "卡片", "连接"],
  },
  {
    title: "写入并校验",
    detail: "正在传输摘要并确认设备接收结果。",
    aliases: ["transfer", "write", "send", "verify", "upload", "传输", "写入", "校验"],
  },
]);

const dom = {};
let currentStatus = null;
let settingsSaving = false;
let statusRequest = null;
let refreshQueued = false;
let eventSource = null;
let reconnectTimer = null;
let demoAvatarRgb565 = "";
let demoPanelVisible = false;
let demoRevealClicks = 0;
let demoLastRevealClickAt = 0;
let demoToastTimer = null;

function byId(id) {
  return document.getElementById(id);
}

function cacheDom() {
  [
    "service-notice",
    "live-connection",
    "auto-sync",
    "sync-interval",
    "sync-now",
    "demo-reveal",
    "demo-section",
    "demo-enabled",
    "demo-editor",
    "demo-user-name",
    "demo-unread-count",
    "demo-avatar-file",
    "demo-avatar-preview",
    "demo-events",
    "demo-tasks",
    "add-demo-event",
    "add-demo-task",
    "sync-demo",
    "demo-validation",
    "demo-toast",
    "retry-sync",
    "device-state",
    "device-detail",
    "sync-console",
    "console-count",
    "protocol-version",
    "protocol-version-detail",
    "scheduler-state",
    "scheduler-detail",
    "last-sync",
    "last-sync-detail",
    "next-sync",
    "next-sync-detail",
    "service-started",
    "sync-state-badge",
    "sync-feedback",
    "sync-feedback-title",
    "sync-feedback-detail",
    "sync-rail",
    "attempt-time",
    "payload-meta",
    "payload-size",
    "agenda-date",
    "summary-identity",
    "summary-user",
    "avatar-canvas",
    "avatar-fallback",
    "agenda-loading",
    "agenda-empty",
    "agenda-error",
    "agenda-content",
    "overdue-count",
    "unread-count",
    "event-count",
    "agenda-list",
    "no-events",
    "announcement",
  ].forEach((id) => {
    dom[id] = byId(id);
  });
  dom.stages = Array.from(document.querySelectorAll(".sync-stage"));
}

function createIcons() {
  if (window.lucide && typeof window.lucide.createIcons === "function") {
    window.lucide.createIcons();
  }
}

function setText(element, value) {
  element.textContent = value;
}

export function pairingWasRemoved(error) {
  return typeof error === "string" && error.includes("Peer removed pairing information");
}

function setLiveConnection(state, label) {
  dom["live-connection"].dataset.state = state;
  setText(dom["live-connection"].lastElementChild, label);
}

function showServiceNotice(message, state = "error") {
  dom["service-notice"].dataset.state = state;
  const icon = dom["service-notice"].querySelector("svg");
  const text = dom["service-notice"].querySelector("span");
  if (icon) {
    icon.outerHTML = `<i data-lucide="${state === "loading" ? "loader-circle" : "circle-alert"}" aria-hidden="true"></i>`;
  }
  setText(text, message);
  createIcons();
}

function hideServiceNotice() {
  dom["service-notice"].dataset.state = "ready";
}

function announce(message) {
  setText(dom.announcement, "");
  window.setTimeout(() => setText(dom.announcement, message), 20);
}

function showDemoEnabledToast() {
  window.clearTimeout(demoToastTimer);
  dom["demo-toast"].hidden = false;
  demoToastTimer = window.setTimeout(() => {
    dom["demo-toast"].hidden = true;
  }, 2600);
}

function parseDate(value) {
  if (!value) return null;
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? null : date;
}

function formatDateTime(value) {
  const date = parseDate(value);
  if (!date) return "—";
  const today = new Date();
  const sameDay =
    date.getFullYear() === today.getFullYear() &&
    date.getMonth() === today.getMonth() &&
    date.getDate() === today.getDate();
  const time = new Intl.DateTimeFormat("zh-CN", {
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  }).format(date);
  if (sameDay) return `今天 ${time}`;
  return new Intl.DateTimeFormat("zh-CN", {
    month: "numeric",
    day: "numeric",
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  }).format(date);
}

function formatFullDateTime(value) {
  const date = parseDate(value);
  if (!date) return "时间未知";
  return new Intl.DateTimeFormat("zh-CN", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  }).format(date);
}

function formatSummaryDate(value) {
  if (typeof value !== "string") return "日期未知";
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value);
  const date = match
    ? new Date(Number(match[1]), Number(match[2]) - 1, Number(match[3]))
    : parseDate(value);
  if (!date || Number.isNaN(date.getTime())) return value;
  return new Intl.DateTimeFormat("zh-CN", {
    year: "numeric",
    month: "long",
    day: "numeric",
    weekday: "long",
  }).format(date);
}

function formatMinute(value) {
  const minutes = Number(value);
  if (!Number.isFinite(minutes)) return "时间未定";
  if (minutes === 1440) return "24:00";
  const normalized = Math.max(0, Math.min(1439, Math.floor(minutes)));
  const hours = Math.floor(normalized / 60);
  const remainder = normalized % 60;
  return `${String(hours).padStart(2, "0")}:${String(remainder).padStart(2, "0")}`;
}

function formatBytes(value) {
  const bytes = Number(value);
  if (!Number.isFinite(bytes) || bytes < 0) return null;
  if (bytes < 1024) return `${Math.round(bytes)} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function normalizedCount(value) {
  const number = Number(value);
  return Number.isFinite(number) && number >= 0 ? Math.floor(number) : 0;
}

export function utf8ByteLength(value) {
  return UTF8_ENCODER.encode(String(value)).byteLength;
}

function validateDemoItems(items, itemLabel, maximumItems, maximumTitleBytes) {
  if (!Array.isArray(items) || items.length > maximumItems) {
    throw new Error(`${itemLabel}最多只能添加 ${maximumItems} 条。`);
  }
  for (const item of items) {
    if (
      !item ||
      typeof item.title !== "string" ||
      !item.title ||
      utf8ByteLength(item.title) > maximumTitleBytes
    ) {
      throw new Error(
        `${itemLabel}标题不能为空，且不能超过 ${maximumTitleBytes} 个 UTF-8 字节。`,
      );
    }
  }
}

export function validateDemoSummary(summary) {
  if (!summary || typeof summary !== "object") {
    throw new Error("演示数据格式无效。");
  }
  validateDemoItems(
    summary.events,
    "日程",
    DEMO_LIMITS.events,
    DEMO_LIMITS.eventTitleBytes,
  );
  validateDemoItems(
    summary.tasks,
    "待办",
    DEMO_LIMITS.tasks,
    DEMO_LIMITS.taskTitleBytes,
  );
  const payloadBytes = utf8ByteLength(JSON.stringify(summary));
  if (payloadBytes > DEMO_LIMITS.payloadBytes) {
    throw new Error(`演示数据不能超过 ${DEMO_LIMITS.payloadBytes} 个 UTF-8 字节。`);
  }
  return payloadBytes;
}

function getPhaseIndex(phase) {
  if (Number.isInteger(phase)) {
    return Math.max(0, Math.min(PHASES.length - 1, phase));
  }
  const normalized = String(phase || "").trim().toLowerCase();
  if (!normalized) return 0;
  const index = PHASES.findIndex(({ aliases }) =>
    aliases.some((alias) => normalized.includes(alias)),
  );
  return index >= 0 ? index : 0;
}

function getUiState(status) {
  const state = status.sync.state;
  if (state === "syncing") return "syncing";
  if (state === "success") return "success";
  if (state === "error") return "error";
  return status.summary ? "ready" : "empty";
}

function ensureIntervalOption(seconds) {
  const value = String(seconds);
  if (Array.from(dom["sync-interval"].options).some((option) => option.value === value)) return;
  const minutes = Math.max(1, Math.round(seconds / 60));
  const option = document.createElement("option");
  option.value = value;
  option.textContent = `每 ${minutes} 分钟`;
  dom["sync-interval"].append(option);
}

function renderControls(status) {
  const { service, sync } = status;
  ensureIntervalOption(service.intervalSeconds);
  dom["auto-sync"].checked = Boolean(service.autoSync);
  dom["sync-interval"].value = String(service.intervalSeconds);
  dom["auto-sync"].disabled = settingsSaving;
  dom["sync-interval"].disabled = settingsSaving;

  const syncing = sync.state === "syncing";
  dom["sync-now"].disabled = syncing;
  dom["retry-sync"].disabled = syncing;
  dom["sync-now"].dataset.loading = String(syncing);
  setText(dom["sync-now"].querySelector("span"), syncing ? "同步进行中" : "立即同步");
  dom["demo-enabled"].disabled = syncing;
  dom["demo-editor"].disabled = !status.service.demoEnabled || syncing;
  dom["sync-demo"].disabled = !status.service.demoEnabled || syncing;
  dom["demo-enabled"].checked = Boolean(status.service.demoEnabled);
  dom["demo-section"].hidden = !(demoPanelVisible || status.service.demoEnabled);
}

function renderStatusColumn(status) {
  const { service, sync } = status;
  const phaseIndex = getPhaseIndex(sync.phase);
  const lastSuccess = parseDate(sync.lastSuccessAt);

  if (sync.state === "syncing" && phaseIndex >= 3) {
    setText(dom["device-state"], "正在写入");
    setText(dom["device-detail"], "BLE 已连接，正在传输设备数据");
  } else if (sync.state === "syncing" && phaseIndex >= 2) {
    setText(dom["device-state"], "正在连接");
    setText(dom["device-detail"], "正在发现并连接 AI Passport");
  } else if (sync.state === "error" && phaseIndex >= 2) {
    setText(dom["device-state"], "连接需检查");
    setText(dom["device-detail"], "确认卡片停留在 BLE 页面后重试");
  } else if (lastSuccess) {
    setText(dom["device-state"], "最近同步可达");
    setText(dom["device-detail"], `设备于 ${formatDateTime(lastSuccess)} 完成接收`);
  } else {
    setText(dom["device-state"], "等待检测");
    setText(dom["device-detail"], "同步时将自动查找卡片");
  }

  const hostProtocolVersion = Number.isInteger(service.protocolVersion)
    ? service.protocolVersion
    : 4;
  const deviceProtocolVersion = service.deviceProtocolVersion;
  setText(dom["protocol-version"], `Host v${hostProtocolVersion}`);
  setText(
    dom["protocol-version-detail"],
    deviceProtocolVersion
      ? `Device v${deviceProtocolVersion}，版本已匹配`
      : "等待设备协议校验",
  );

  if (service.autoSync) {
    const minutes = Math.max(1, Math.round(service.intervalSeconds / 60));
    setText(dom["scheduler-state"], "自动同步已启用");
    setText(dom["scheduler-detail"], `每 ${minutes} 分钟运行一次`);
  } else {
    setText(dom["scheduler-state"], "自动同步已暂停");
    setText(dom["scheduler-detail"], "仍可随时手动同步");
  }

  if (lastSuccess) {
    setText(dom["last-sync"], formatDateTime(lastSuccess));
    setText(dom["last-sync-detail"], "设备数据写入成功");
  } else {
    setText(dom["last-sync"], "尚未同步");
    setText(dom["last-sync-detail"], "暂无成功记录");
  }

  if (service.autoSync && service.nextSyncAt) {
    setText(dom["next-sync"], formatDateTime(service.nextSyncAt));
    setText(dom["next-sync-detail"], "服务将自动发起同步");
  } else if (service.autoSync) {
    setText(dom["next-sync"], "正在安排");
    setText(dom["next-sync-detail"], "等待下一次调度时间");
  } else {
    setText(dom["next-sync"], "已暂停");
    setText(dom["next-sync-detail"], "开启自动同步后恢复");
  }

  setText(
    dom["service-started"],
    service.startedAt ? `始于 ${formatFullDateTime(service.startedAt)}` : "启动时间未知",
  );
}

function renderSyncRail(sync) {
  const phaseIndex = getPhaseIndex(sync.phase);
  const hasCompletedRun =
    sync.state === "success" ||
    (sync.state === "idle" && Boolean(sync.lastSuccessAt) && Boolean(currentStatus?.summary));

  dom.stages.forEach((stage, index) => {
    let state = "pending";
    if (hasCompletedRun) {
      state = "complete";
    } else if (sync.state === "syncing") {
      state = index < phaseIndex ? "complete" : index === phaseIndex ? "current" : "pending";
    } else if (sync.state === "error") {
      state = index < phaseIndex ? "complete" : index === phaseIndex ? "error" : "pending";
    }
    stage.dataset.state = state;
    stage.setAttribute(
      "aria-label",
      `${PHASES[index].title}：${
        state === "complete"
          ? "已完成"
          : state === "current"
            ? "进行中"
            : state === "error"
              ? "出错"
              : "等待"
      }`,
    );
  });
}

function renderSyncState(status) {
  const { sync, summary } = status;
  const uiState = getUiState(status);
  const phaseIndex = getPhaseIndex(sync.phase);
  const badgeLabels = {
    syncing: "同步中",
    success: "同步成功",
    error: "需要处理",
    ready: "已就绪",
    empty: "等待首次同步",
  };

  dom["sync-state-badge"].dataset.state = uiState;
  setText(dom["sync-state-badge"].lastElementChild, badgeLabels[uiState]);
  dom["sync-feedback"].dataset.state = uiState;
  dom["retry-sync"].hidden = uiState !== "error";

  if (uiState === "syncing") {
    setText(dom["sync-feedback-title"], PHASES[phaseIndex].title);
    setText(dom["sync-feedback-detail"], PHASES[phaseIndex].detail);
  } else if (uiState === "success") {
    setText(dom["sync-feedback-title"], "最近一次同步已完成");
    setText(dom["sync-feedback-detail"], "今日摘要已写入 AI Passport，可安全离开 BLE 页面。");
  } else if (uiState === "error") {
    setText(dom["sync-feedback-title"], sync.error || "同步未完成");
    setText(
      dom["sync-feedback-detail"],
      pairingWasRemoved(sync.error)
        ? "卡片已删除配对信息。请在 macOS 蓝牙设置中忘记此设备，再让卡片停留在 BLE 页面并重新配对。"
        : "确认 lark-cli 已登录、蓝牙权限已开启，并让卡片停留在 BLE 页面后重试。",
    );
  } else if (uiState === "ready") {
    setText(dom["sync-feedback-title"], "最近一次设备内容已就绪");
    setText(dom["sync-feedback-detail"], "可立即同步更新，或等待下一次自动同步。");
  } else {
    setText(dom["sync-feedback-title"], "等待首次同步");
    setText(dom["sync-feedback-detail"], "点击“立即同步”，将今日工作摘要写入 AI Passport。");
  }

  renderSyncRail(sync);
  setText(
    dom["attempt-time"],
    sync.lastAttemptAt ? `最近尝试 ${formatFullDateTime(sync.lastAttemptAt)}` : "尚无同步尝试",
  );
  const payload = formatBytes(sync.payloadBytes);
  dom["payload-meta"].hidden = !payload;
  if (payload) setText(dom["payload-size"], `传输 ${payload}`);

  if (!summary && uiState === "error") {
    dom["agenda-error"].hidden = false;
  }
}

function drawRgb565Avatar(base64) {
  if (typeof base64 !== "string" || !base64.trim()) return false;
  try {
    const normalized = base64.includes(",") ? base64.slice(base64.indexOf(",") + 1) : base64;
    const binary = window.atob(normalized.replace(/\s/g, ""));
    if (binary.length !== 32 * 32 * 2) return false;

    const context = dom["avatar-canvas"].getContext("2d", { alpha: false });
    if (!context) return false;
    const imageData = context.createImageData(32, 32);
    for (let pixelIndex = 0; pixelIndex < 32 * 32; pixelIndex += 1) {
      const byteIndex = pixelIndex * 2;
      const rgb565 = binary.charCodeAt(byteIndex) | (binary.charCodeAt(byteIndex + 1) << 8);
      const outputIndex = pixelIndex * 4;
      imageData.data[outputIndex] = Math.round(((rgb565 >> 11) & 0x1f) * (255 / 31));
      imageData.data[outputIndex + 1] = Math.round(((rgb565 >> 5) & 0x3f) * (255 / 63));
      imageData.data[outputIndex + 2] = Math.round((rgb565 & 0x1f) * (255 / 31));
      imageData.data[outputIndex + 3] = 255;
    }
    context.putImageData(imageData, 0, 0);
    return true;
  } catch {
    return false;
  }
}

function renderAvatar(summary) {
  const rendered = drawRgb565Avatar(summary.avatar_rgb565);
  dom["avatar-canvas"].hidden = !rendered;
  dom["avatar-fallback"].hidden = rendered;
  setText(dom["avatar-fallback"], String(summary.user_name || "AI").trim().slice(0, 1) || "AI");
}

function createAgendaItem(event) {
  const item = document.createElement("li");
  const completed = Boolean(event.completed);
  item.className = "agenda-item";
  item.dataset.completed = String(completed);

  const time = document.createElement("span");
  time.className = "agenda-item__time";
  const timeIcon = document.createElement("i");
  timeIcon.dataset.lucide = event.all_day ? "calendar-days" : "clock-3";
  timeIcon.setAttribute("aria-hidden", "true");
  time.append(timeIcon);
  const timeText = document.createElement("span");
  timeText.textContent = event.all_day
    ? "全天"
    : `${formatMinute(event.start_minute)}–${formatMinute(event.end_minute)}`;
  time.append(timeText);

  const title = document.createElement("span");
  title.className = "agenda-item__title";
  title.textContent = String(event.title || "无标题日程");

  const state = document.createElement("span");
  state.className = "agenda-item__status";
  state.textContent = completed ? "已完成" : "待进行";

  item.append(time, title, state);
  return item;
}

function renderSummary(summary, syncState) {
  dom["agenda-loading"].hidden = true;
  dom["agenda-empty"].hidden = true;
  dom["agenda-error"].hidden = true;

  if (!summary) {
    dom["agenda-content"].hidden = true;
    dom["summary-identity"].hidden = true;
    dom["agenda-date"].textContent = "完成首次同步后显示";
    if (syncState !== "error") dom["agenda-empty"].hidden = false;
    return;
  }

  const events = Array.isArray(summary.events) ? summary.events : [];
  dom["agenda-content"].hidden = false;
  dom["summary-identity"].hidden = false;
  setText(dom["agenda-date"], formatSummaryDate(summary.date));
  setText(dom["summary-user"], summary.user_name || "账户名称未知");
  setText(dom["overdue-count"], String(normalizedCount(summary.overdue_task_count)));
  setText(dom["unread-count"], String(normalizedCount(summary.unread_message_count)));
  setText(dom["event-count"], String(events.length));
  renderAvatar(summary);

  dom["agenda-list"].replaceChildren(...events.map(createAgendaItem));
  dom["no-events"].hidden = events.length !== 0;
  createIcons();
}

function formatLogTime(value) {
  const date = parseDate(value);
  if (!date) return "--:--:--";
  return new Intl.DateTimeFormat("zh-CN", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  }).format(date);
}

function createLogLine(entry) {
  const line = document.createElement("div");
  line.className = "terminal-line";
  line.dataset.level = entry.level;

  const time = document.createElement("time");
  time.textContent = formatLogTime(entry.timestamp);
  const scope = document.createElement("span");
  scope.className = "terminal-line__scope";
  scope.textContent = entry.scope;
  const message = document.createElement("span");
  message.className = "terminal-line__message";
  message.textContent = entry.message;
  line.append(time, scope, message);
  return line;
}

function renderConsole(logs) {
  const consoleElement = dom["sync-console"];
  const wasAtBottom =
    consoleElement.scrollHeight - consoleElement.scrollTop - consoleElement.clientHeight < 32;
  const entries = Array.isArray(logs) ? logs : [];
  setText(dom["console-count"], `${entries.length} 条日志`);

  if (!entries.length) {
    const empty = document.createElement("div");
    empty.className = "terminal-line terminal-line--empty";
    empty.innerHTML = "<time>--:--:--</time><span class=\"terminal-line__scope\">SYSTEM</span>";
    const message = document.createElement("span");
    message.className = "terminal-line__message";
    message.textContent = "等待本地服务日志…";
    empty.append(message);
    consoleElement.replaceChildren(empty);
    return;
  }

  consoleElement.replaceChildren(...entries.map(createLogLine));
  if (wasAtBottom) consoleElement.scrollTop = consoleElement.scrollHeight;
}

function validateStatus(payload) {
  if (!payload || typeof payload !== "object" || !payload.service || !payload.sync) {
    throw new Error("状态响应格式无效");
  }
  const intervalSeconds = Number(payload.service.intervalSeconds);
  if (!Number.isFinite(intervalSeconds) || intervalSeconds < 60) {
    throw new Error("同步间隔无效");
  }
  return {
    service: {
      startedAt: payload.service.startedAt || null,
      autoSync: Boolean(payload.service.autoSync),
      intervalSeconds,
      nextSyncAt: payload.service.nextSyncAt || null,
      demoEnabled: Boolean(payload.service.demoEnabled),
      protocolVersion: payload.service.protocolVersion,
      deviceProtocolVersion: payload.service.deviceProtocolVersion,
    },
    sync: {
      state: ["idle", "syncing", "success", "error"].includes(payload.sync.state)
        ? payload.sync.state
        : "idle",
      phase: payload.sync.phase || null,
      lastAttemptAt: payload.sync.lastAttemptAt || null,
      lastSuccessAt: payload.sync.lastSuccessAt || null,
      error: typeof payload.sync.error === "string" ? payload.sync.error : null,
      payloadBytes: payload.sync.payloadBytes,
    },
    summary: payload.summary && typeof payload.summary === "object" ? payload.summary : null,
    logs: Array.isArray(payload.logs)
      ? payload.logs
        .filter((entry) => entry && typeof entry === "object")
        .map((entry) => ({
          timestamp: entry.timestamp || null,
          level: ["info", "success", "warning", "error"].includes(entry.level)
            ? entry.level
            : "info",
          scope: String(entry.scope || "SYSTEM").slice(0, 16),
          message: String(entry.message || "状态已更新").slice(0, 320),
        }))
      : [],
  };
}

function renderStatus(status) {
  currentStatus = status;
  hideServiceNotice();
  renderControls(status);
  renderStatusColumn(status);
  renderSyncState(status);
  renderSummary(status.summary, status.sync.state);
  renderConsole(status.logs);
}

async function fetchJson(url, options) {
  const response = await fetch(url, {
    ...options,
    headers: {
      Accept: "application/json",
      ...(options?.headers || {}),
    },
    cache: "no-store",
  });
  return response;
}

async function refreshStatus({ announceError = false } = {}) {
  if (statusRequest) {
    refreshQueued = true;
    return statusRequest;
  }

  statusRequest = (async () => {
    try {
      const response = await fetchJson(API.status);
      if (!response.ok) throw new Error("status request failed");
      renderStatus(validateStatus(await response.json()));
    } catch {
      showServiceNotice("无法连接本地服务。确认 npm start 正在运行后刷新页面。");
      if (!currentStatus) {
        dom["agenda-loading"].hidden = true;
        dom["agenda-error"].hidden = false;
        dom["sync-state-badge"].dataset.state = "error";
        setText(dom["sync-state-badge"].lastElementChild, "服务不可用");
        dom["sync-now"].disabled = true;
      }
      if (announceError) announce("无法连接本地服务");
    } finally {
      statusRequest = null;
      if (refreshQueued) {
        refreshQueued = false;
        void refreshStatus();
      }
    }
  })();

  return statusRequest;
}

function renderOptimisticSync() {
  if (!currentStatus) return;
  renderStatus({
    ...currentStatus,
    sync: {
      ...currentStatus.sync,
      state: "syncing",
      phase: "queued",
      lastAttemptAt: new Date().toISOString(),
      error: null,
    },
  });
}

async function requestSync() {
  if (!currentStatus || currentStatus.sync.state === "syncing") return;
  renderOptimisticSync();
  announce("已开始同步");

  try {
    const response = await fetchJson(API.sync, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: "{}",
    });
    if (response.status !== 202 && response.status !== 409) {
      throw new Error("sync request failed");
    }
    if (response.status === 409) announce("同步已在进行中");
    await refreshStatus({ announceError: true });
  } catch {
    if (currentStatus) {
      renderStatus({
        ...currentStatus,
        sync: {
          ...currentStatus.sync,
          state: "error",
          error: "本地服务未接受同步请求。",
        },
      });
    }
    showServiceNotice("无法发起同步。确认本地服务仍在运行后重试。");
    announce("无法发起同步");
  }
}

async function saveSettings(nextSettings) {
  if (!currentStatus || settingsSaving) return;
  settingsSaving = true;
  renderControls(currentStatus);

  try {
    const response = await fetchJson(API.settings, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(nextSettings),
    });
    if (!response.ok) throw new Error("settings request failed");

    let payload = null;
    if (response.status !== 204) {
      const contentType = response.headers.get("content-type") || "";
      if (contentType.includes("application/json")) payload = await response.json();
    }
    if (payload?.service && payload?.sync) {
      renderStatus(validateStatus(payload));
    } else {
      await refreshStatus({ announceError: true });
    }
    announce("同步设置已更新");
  } catch {
    showServiceNotice("设置未保存。确认本地服务正常后再试一次。");
    announce("同步设置保存失败");
  } finally {
    settingsSaving = false;
    if (currentStatus) renderControls(currentStatus);
  }
}

function bindControls() {
  dom["sync-now"].addEventListener("click", requestSync);
  dom["retry-sync"].addEventListener("click", requestSync);

  dom["auto-sync"].addEventListener("change", () => {
    void saveSettings({
      autoSync: dom["auto-sync"].checked,
      intervalSeconds: Number(dom["sync-interval"].value),
    });
  });

  dom["sync-interval"].addEventListener("change", () => {
    void saveSettings({
      autoSync: dom["auto-sync"].checked,
      intervalSeconds: Number(dom["sync-interval"].value),
    });
  });

  dom["demo-enabled"].addEventListener("change", () => {
    void saveDemo(Boolean(dom["demo-enabled"].checked), false);
  });
  dom["demo-reveal"].addEventListener("click", revealDemoPanel);
  dom["add-demo-event"].addEventListener("click", () => addDemoEvent());
  dom["add-demo-task"].addEventListener("click", () => addDemoTask());
  dom["sync-demo"].addEventListener("click", () => void saveDemo(true, true));
  dom["demo-avatar-file"].addEventListener("change", () => void loadDemoAvatar());

  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) void refreshStatus();
  });
}

function revealDemoPanel() {
  const now = Date.now();
  demoRevealClicks =
    now - demoLastRevealClickAt <= DEMO_REVEAL_WINDOW_MS ? demoRevealClicks + 1 : 1;
  demoLastRevealClickAt = now;
  if (demoRevealClicks < DEMO_REVEAL_CLICKS) return;
  demoRevealClicks = 0;
  demoLastRevealClickAt = 0;
  if (currentStatus?.service.demoEnabled) {
    demoPanelVisible = true;
    renderControls(currentStatus);
    return;
  }
  void saveDemo(true, false);
}

function localDateValue(date = new Date()) {
  const offset = date.getTimezoneOffset() * 60000;
  return new Date(date.getTime() - offset).toISOString().slice(0, 10);
}

function createDemoRow(kind, values = {}) {
  const row = document.createElement("div");
  row.className = "demo-row";
  const isEvent = kind === "event";
  const defaultDate = localDateValue();
  row.innerHTML = isEvent
    ? `<label><span>标题</span><input data-field="title" maxlength="${DEMO_LIMITS.eventTitleBytes}" value="${escapeHtml(values.title || "")}"></label>
       <label><span>开始</span><input data-field="start" type="time" value="${values.start || "09:00"}"></label>
       <label><span>结束</span><input data-field="end" type="time" value="${values.end || "10:00"}"></label>
       <label class="compact-check"><input data-field="allDay" type="checkbox" ${values.allDay ? "checked" : ""}><span>全天</span></label>`
    : `<label><span>标题</span><input data-field="title" maxlength="${DEMO_LIMITS.taskTitleBytes}" value="${escapeHtml(values.title || "")}"></label>
       <label><span>截止日期</span><input data-field="due" type="date" value="${values.due || defaultDate}"></label>
       <label class="compact-check"><input data-field="allDay" type="checkbox" ${values.allDay ? "checked" : ""}><span>全天</span></label>`;
  const remove = document.createElement("button");
  remove.type = "button";
  remove.className = "icon-button";
  remove.title = "删除";
  remove.setAttribute("aria-label", "删除");
  remove.innerHTML = '<i data-lucide="trash-2" aria-hidden="true"></i>';
  remove.addEventListener("click", () => row.remove());
  row.append(remove);
  return row;
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (character) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  })[character]);
}

function addDemoEvent(values) {
  if (dom["demo-events"].children.length >= DEMO_LIMITS.events) return;
  dom["demo-events"].append(createDemoRow("event", values));
  createIcons();
}

function addDemoTask(values) {
  if (dom["demo-tasks"].children.length >= DEMO_LIMITS.tasks) return;
  dom["demo-tasks"].append(createDemoRow("task", values));
  createIcons();
}

function timeToMinute(value) {
  const match = /^(\d{2}):(\d{2})$/.exec(String(value || ""));
  if (!match) return null;
  const result = Number(match[1]) * 60 + Number(match[2]);
  return result >= 0 && result <= 1440 ? result : null;
}

function readDemoSummary() {
  const now = new Date();
  const generatedAtEpoch = Math.floor(now.getTime() / 1000);
  const offset = -now.getTimezoneOffset();
  const events = Array.from(dom["demo-events"].children).map((row) => {
    const title = row.querySelector('[data-field="title"]').value.trim();
    const allDay = row.querySelector('[data-field="allDay"]').checked;
    const start = allDay ? 0 : timeToMinute(row.querySelector('[data-field="start"]').value);
    const end = allDay ? 1440 : timeToMinute(row.querySelector('[data-field="end"]').value);
    if (!title || start === null || end === null || end < start) throw new Error("请填写有效的日程标题和时间。");
    return { title, start_minute: start, end_minute: end, all_day: allDay, completed: false };
  });
  const tasks = Array.from(dom["demo-tasks"].children).map((row, index) => {
    const title = row.querySelector('[data-field="title"]').value.trim();
    const due = row.querySelector('[data-field="due"]').value;
    if (!title || !due) throw new Error("请填写有效的待办标题和截止日期。");
    const dueAt = new Date(`${due}T12:00:00`).getTime() / 1000;
    return { guid: `demo-${generatedAtEpoch}-${index}`, title, due_at_epoch: Math.floor(dueAt), all_day: row.querySelector('[data-field="allDay"]').checked };
  });
  const userName = dom["demo-user-name"].value.trim();
  const unread = Number(dom["demo-unread-count"].value);
  if (!userName) throw new Error("请填写显示名称。");
  if (!Number.isInteger(unread) || unread < 0 || unread > 65535) throw new Error("未读消息必须在 0 到 65535 之间。");
  const summary = {
    version: 4,
    generated_at: now.toISOString().replace(/\.\d{3}Z$/, "Z"),
    generated_at_epoch: generatedAtEpoch,
    utc_offset_minutes: offset,
    user_name: userName,
    avatar_rgb565: demoAvatarRgb565,
    date: localDateValue(now),
    events,
    tasks,
    overdue_task_count: tasks.length,
    unread_message_count: unread,
  };
  validateDemoSummary(summary);
  return summary;
}

async function loadDemoAvatar() {
  const file = dom["demo-avatar-file"].files?.[0];
  if (!file) return;
  if (file.size > 5 * 1024 * 1024) {
    setText(dom["demo-validation"], "头像文件不能超过 5 MB。");
    return;
  }
  const image = await createImageBitmap(file);
  const canvas = document.createElement("canvas");
  canvas.width = 32;
  canvas.height = 32;
  const context = canvas.getContext("2d", { alpha: false });
  context.drawImage(image, 0, 0, 32, 32);
  image.close();
  const pixels = context.getImageData(0, 0, 32, 32).data;
  const bytes = new Uint8Array(2048);
  for (let index = 0; index < 1024; index += 1) {
    const source = index * 4;
    const rgb565 = ((pixels[source] >> 3) << 11) | ((pixels[source + 1] >> 2) << 5) | (pixels[source + 2] >> 3);
    bytes[index * 2] = rgb565 & 0xff;
    bytes[index * 2 + 1] = rgb565 >> 8;
  }
  demoAvatarRgb565 = btoa(String.fromCharCode(...bytes));
  dom["demo-avatar-preview"].textContent = "";
  dom["demo-avatar-preview"].style.backgroundImage = `url(${canvas.toDataURL("image/png")})`;
  setText(dom["demo-validation"], "头像已转换为设备格式。");
}

async function saveDemo(enabled, startSync) {
  let summary = null;
  try {
    if (enabled) summary = readDemoSummary();
  } catch (error) {
    setText(dom["demo-validation"], error.message || "演示数据无效。");
    return;
  }
  try {
    const response = await fetchJson(API.demo, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ enabled, summary }),
    });
    if (!response.ok) {
      const payload = await response.json().catch(() => null);
      throw new Error(payload?.error || "演示模式未保存。");
    }
    renderStatus(validateStatus(await response.json()));
    demoPanelVisible = enabled;
    if (currentStatus) renderControls(currentStatus);
    setText(dom["demo-validation"], enabled ? "演示数据已保存，准备同步。" : "已切换回飞书数据源。");
    if (enabled) {
      showDemoEnabledToast();
      announce("您已开启演示模式");
    }
    if (startSync) await requestSync();
  } catch (error) {
    setText(dom["demo-validation"], error.message || "演示模式未保存。");
  }
}

function connectEventStream() {
  window.clearTimeout(reconnectTimer);
  if (eventSource) eventSource.close();

  setLiveConnection("connecting", "连接中");
  eventSource = new EventSource(API.events);

  eventSource.addEventListener("open", () => {
    setLiveConnection("live", "实时");
    void refreshStatus();
  });

  const refreshFromEvent = () => {
    void refreshStatus();
  };
  eventSource.addEventListener("message", refreshFromEvent);
  ["status", "state", "sync", "settings", "update", "connected"].forEach((eventName) => {
    eventSource.addEventListener(eventName, refreshFromEvent);
  });

  eventSource.addEventListener("error", () => {
    setLiveConnection("reconnecting", "重连中");
    eventSource.close();
    reconnectTimer = window.setTimeout(connectEventStream, 3000);
  });
}

function init() {
  cacheDom();
  addDemoEvent({ title: "演示项目同步", start: "10:00", end: "10:30" });
  addDemoTask({ title: "确认演示内容" });
  dom["agenda-loading"].dataset.loading = "true";
  createIcons();
  bindControls();
  showServiceNotice("正在读取本地服务状态…", "loading");
  void refreshStatus();
  connectEventStream();

  window.addEventListener("beforeunload", () => {
    window.clearTimeout(reconnectTimer);
    if (eventSource) eventSource.close();
  });
}

if (typeof document !== "undefined") {
  document.addEventListener("DOMContentLoaded", init);
}
