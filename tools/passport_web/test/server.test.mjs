import assert from "node:assert/strict";
import { once } from "node:events";
import { readFile } from "node:fs/promises";
import http from "node:http";
import test from "node:test";
import { setTimeout as delay } from "node:timers/promises";

import {
  DEMO_LIMITS,
  pairingWasRemoved,
  utf8ByteLength,
  validateDemoSummary,
} from "../public/app.js";
import { executeBridge, parseBridgeOutput } from "../bridge-runner.mjs";
import { createPassportServer, SyncController } from "../server.mjs";

function successfulResult() {
  return {
    ok: true,
    payload_bytes: 4446,
    summary: {
      version: 4,
      user_name: "张剑秋",
      date: "2026-09-04",
      avatar_rgb565: "",
      events: [],
      tasks: [],
      overdue_task_count: 1,
      unread_message_count: 8,
    },
  };
}

function demoSummary(overrides = {}) {
  return {
    version: 4,
    generated_at: "2026-09-05T04:00:00Z",
    generated_at_epoch: 1788580800,
    utc_offset_minutes: 480,
    user_name: "演示用户",
    avatar_rgb565: "",
    date: "2026-09-05",
    events: [],
    tasks: [],
    overdue_task_count: 0,
    unread_message_count: 0,
    ...overrides,
  };
}

function request({
  port,
  path = "/",
  method = "GET",
  headers = {},
  chunks = [],
}) {
  return new Promise((resolve, reject) => {
    const outgoing = http.request(
      {
        host: "127.0.0.1",
        port,
        path,
        method,
        headers,
      },
      (incoming) => {
        let body = "";
        incoming.setEncoding("utf8");
        incoming.on("data", (chunk) => {
          body += chunk;
        });
        incoming.on("end", () => {
          resolve({
            status: incoming.statusCode,
            headers: incoming.headers,
            body,
          });
        });
      },
    );
    outgoing.on("error", reject);
    for (const chunk of chunks) outgoing.write(chunk);
    outgoing.end();
  });
}

function processExists(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    if (error?.code === "ESRCH") return false;
    throw error;
  }
}

test("parses the final sanitized bridge result", () => {
  const result = parseBridgeOutput(`ignored\n${JSON.stringify(successfulResult())}\n`);
  assert.equal(result.ok, true);
  assert.equal(result.payload_bytes, 4446);
  assert.throws(() => parseBridgeOutput("not json"), /可识别/);
});

test("identifies BLE pairing removal failures", () => {
  assert.equal(
    pairingWasRemoved(
      'BLE transfer failed (BleakError: failed to connect: Error Domain=CBErrorDomain Code=14 "Peer removed pairing information")',
    ),
    true,
  );
  assert.equal(pairingWasRemoved("BLE transfer failed"), false);
});

test("validates demo title limits as UTF-8 bytes without truncation", () => {
  const eventTitle = "中".repeat(85);
  const taskTitle = `${"中".repeat(42)}a`;
  assert.equal(utf8ByteLength(eventTitle), DEMO_LIMITS.eventTitleBytes);
  assert.equal(utf8ByteLength(taskTitle), DEMO_LIMITS.taskTitleBytes);

  const summary = demoSummary({
    events: [{
      title: eventTitle,
      start_minute: 0,
      end_minute: 1,
      all_day: false,
      completed: false,
    }],
    tasks: [{
      guid: "task-boundary",
      title: taskTitle,
      due_at_epoch: 0,
      all_day: false,
    }],
  });
  assert.ok(validateDemoSummary(summary) <= DEMO_LIMITS.payloadBytes);
  assert.equal(summary.events[0].title, eventTitle);
  assert.equal(summary.tasks[0].title, taskTitle);

  assert.throws(
    () => validateDemoSummary({
      ...summary,
      events: [{ ...summary.events[0], title: `${eventTitle}a` }],
    }),
    /255 个 UTF-8 字节/,
  );
  assert.throws(
    () => validateDemoSummary({
      ...summary,
      tasks: [{ ...summary.tasks[0], title: `${taskTitle}a` }],
    }),
    /127 个 UTF-8 字节/,
  );
});

test("enforces demo collection and payload boundaries", () => {
  const eventItem = {
    title: "Event",
    start_minute: 0,
    end_minute: 1,
    all_day: false,
    completed: false,
  };
  const taskItem = {
    guid: "task-boundary",
    title: "Task",
    due_at_epoch: 0,
    all_day: false,
  };
  const boundary = demoSummary({
    events: Array.from({ length: DEMO_LIMITS.events }, () => ({ ...eventItem })),
    tasks: Array.from(
      { length: DEMO_LIMITS.tasks },
      (_, index) => ({ ...taskItem, guid: `task-${index}` }),
    ),
  });
  assert.doesNotThrow(() => validateDemoSummary(boundary));
  assert.throws(
    () => validateDemoSummary({
      ...boundary,
      events: [...boundary.events, { ...eventItem }],
    }),
    /最多只能添加 16 条/,
  );
  assert.throws(
    () => validateDemoSummary({
      ...boundary,
      tasks: [...boundary.tasks, { ...taskItem }],
    }),
    /最多只能添加 16 条/,
  );
  assert.equal(DEMO_LIMITS.payloadBytes, 12 * 1024);
  assert.throws(
    () => validateDemoSummary(
      demoSummary({ avatar_rgb565: "x".repeat(DEMO_LIMITS.payloadBytes) }),
    ),
    /12288 个 UTF-8 字节/,
  );
});

test("publishes the release capacity in the demo editor UI", async () => {
  const [html, script] = await Promise.all([
    readFile(new URL("../public/index.html", import.meta.url), "utf8"),
    readFile(new URL("../public/app.js", import.meta.url), "utf8"),
  ]);
  assert.match(html, /最多 16 条；标题最多 255 UTF-8 字节/);
  assert.match(html, /最多 16 条；标题最多 127 UTF-8 字节/);
  assert.match(html, /payload 不超过 12 KiB/);
  assert.match(script, /maxlength="\$\{DEMO_LIMITS\.eventTitleBytes\}"/);
  assert.match(script, /maxlength="\$\{DEMO_LIMITS\.taskTitleBytes\}"/);
});

test("prevents overlapping synchronization and schedules the next run", async () => {
  let finish;
  const pending = new Promise((resolve) => {
    finish = resolve;
  });
  let calls = 0;
  const timers = [];
  const controller = new SyncController({
    runner: async () => {
      calls += 1;
      return pending;
    },
    now: () => 1000,
    setTimer: (callback, delay) => {
      const timer = { callback, delay, unref() {} };
      timers.push(timer);
      return timer;
    },
    clearTimer: () => {},
  });
  const first = controller.trigger("manual");
  assert.equal(await controller.trigger("manual"), false);
  assert.equal(calls, 1);
  assert.equal(controller.snapshot().sync.state, "syncing");
  finish(successfulResult());
  assert.equal(await first, true);
  assert.equal(controller.snapshot().sync.state, "success");
  assert.equal(controller.snapshot().service.nextSyncAt, "1970-01-01T00:10:01.000Z");
  assert.equal(timers.at(-1).delay, 600000);
});

test("uses the locally authored summary when demo mode is enabled", async () => {
  const demoSummary = {
    version: 4,
    user_name: "演示用户",
    date: "2026-09-05",
    avatar_rgb565: "",
    events: [],
    tasks: [],
    overdue_task_count: 0,
    unread_message_count: 0,
  };
  let standardCalls = 0;
  let receivedSummary = null;
  const controller = new SyncController({
    autoSync: false,
    runner: async () => {
      standardCalls += 1;
      return successfulResult();
    },
    demoRunner: async ({ summary }) => {
      receivedSummary = summary;
      return { ok: true, payload_bytes: 222, summary };
    },
  });

  controller.updateDemo({ enabled: true, summary: demoSummary });
  await controller.trigger("manual");
  assert.equal(standardCalls, 0);
  assert.deepEqual(receivedSummary, demoSummary);
  assert.equal(controller.snapshot().service.demoEnabled, true);
  assert.equal(controller.snapshot().service.autoSync, false);
  assert.equal(controller.snapshot().sync.payloadBytes, 222);
});

test("stop aborts an active child without scheduling again", async () => {
  let aborted = false;
  let timerCalls = 0;
  const controller = new SyncController({
    runner: ({ signal }) =>
      new Promise((resolve, reject) => {
        signal.addEventListener("abort", () => {
          aborted = true;
          reject(new Error("同步已停止。"));
        });
      }),
    setTimer: () => {
      timerCalls += 1;
      return { unref() {} };
    },
    clearTimer: () => {},
  });
  const pending = controller.trigger("manual");
  controller.stop();
  await pending;
  assert.equal(aborted, true);
  assert.equal(timerCalls, 0);
});

test("queued startup and timer triggers do not run after stop", async () => {
  let calls = 0;
  let queuedTimer;
  const controller = new SyncController({
    runner: async () => {
      calls += 1;
      return successfulResult();
    },
    setTimer: (callback) => {
      queuedTimer = callback;
      return { unref() {} };
    },
    clearTimer: () => {},
  });

  controller.start();
  controller.schedule();
  controller.stop();
  await Promise.resolve();
  await queuedTimer();
  assert.equal(calls, 0);
});

test("controller reports actual bridge phases", async () => {
  const phases = [];
  const controller = new SyncController({
    autoSync: false,
    runner: async ({ onProgress }) => {
      for (const phase of [
        "collecting",
        "summarizing",
        "connecting",
        "transferring",
        "complete",
      ]) {
        onProgress(phase);
      }
      return successfulResult();
    },
  });
  controller.on("state", ({ sync }) => {
    if (sync.state === "syncing") phases.push(sync.phase);
  });

  assert.equal(await controller.trigger("manual"), true);
  assert.deepEqual(phases.slice(-5), [
    "collecting",
    "summarizing",
    "connecting",
    "transferring",
    "complete",
  ]);
  const logs = controller.snapshot().logs;
  assert.deepEqual(
    logs
      .filter(({ scope }) => scope === "DATA" || scope === "BLE")
      .map(({ scope }) => scope),
    ["DATA", "DATA", "BLE", "BLE", "BLE"],
  );
  assert.match(logs.at(-1).message, /0 条日程，0 条待办，4446 bytes/);
});

test("controller formats detailed data, protocol, and BLE progress", async () => {
  const controller = new SyncController({
    autoSync: false,
    runner: async ({ onProgress }) => {
      onProgress("collecting", {
        phase: "collecting",
        event: "calendar_ready",
        details: {
          owner_calendar_count: 2,
          fetched_event_count: 18,
        },
      });
      onProgress("summarizing", {
        phase: "summarizing",
        event: "payload_ready",
        details: {
          protocol_version: 4,
          payload_bytes: 4446,
          payload_limit_bytes: 12288,
          chunk_bytes: 180,
          data_frame_count: 25,
          checksum_hex: "12ab34cd",
        },
      });
      onProgress("transferring", {
        phase: "transferring",
        event: "chunk_sent",
        details: {
          frame_index: 7,
          frame_total: 25,
          data_bytes: 180,
        },
      });
      onProgress("complete", {
        phase: "complete",
        event: "ack_received",
        details: { result: "COMPLETE" },
      });
      return successfulResult();
    },
  });

  await controller.trigger("manual");
  const messages = controller.snapshot().logs.map(({ message }) => message);
  assert.ok(messages.some((message) => /owner 日历 2 个/.test(message)));
  assert.ok(messages.some((message) => /4446\/12288 bytes/.test(message)));
  assert.ok(messages.some((message) => /DATA 7\/25/.test(message)));
  assert.ok(messages.some((message) => /CRC 校验通过/.test(message)));
});

test("console logs are bounded and sanitize sensitive text", () => {
  let now = 1000;
  const controller = new SyncController({
    autoSync: false,
    runner: async () => successfulResult(),
    now: () => now++,
  });

  for (let index = 0; index < 205; index += 1) {
    controller.appendLog("info", "SYNC", `状态 ${index}`);
  }
  controller.appendLog(
    "error",
    "unsafe scope",
    "https://private.invalid/avatar access_token=very-secret\n下一行",
  );

  const logs = controller.snapshot().logs;
  const serialized = JSON.stringify(logs);
  assert.equal(logs.length, 200);
  assert.equal(logs.at(-1).scope, "UNSAFESCOPE");
  assert.equal(logs.at(-1).level, "error");
  assert.match(logs.at(-1).message, /\[已隐藏 URL\]/);
  assert.match(logs.at(-1).message, /\[凭证已隐藏\]/);
  assert.doesNotMatch(serialized, /private\.invalid|very-secret|access_token/);
});

test("bridge runner parses progress lines from the child process", async () => {
  const phases = [];
  const progressMessages = [];
  const output = [
    JSON.stringify({
      type: "progress",
      phase: "collecting",
      event: "calendar_ready",
      details: {
        owner_calendar_count: 2,
        fetched_event_count: 18,
        access_token: "must-not-cross",
      },
    }),
    JSON.stringify({ type: "progress", phase: "connecting" }),
    JSON.stringify(successfulResult()),
    "",
  ].join("\n");
  const result = await executeBridge(
    process.execPath,
    ["-e", `process.stdout.write(${JSON.stringify(output)})`],
    { timeout: 1000 },
    {
      onProgress: (phase, progress) => {
        phases.push(phase);
        progressMessages.push(progress);
      },
    },
  );

  assert.deepEqual(phases, ["collecting", "connecting"]);
  assert.deepEqual(progressMessages[0], {
    phase: "collecting",
    event: "calendar_ready",
    details: {
      owner_calendar_count: 2,
      fetched_event_count: 18,
    },
  });
  assert.equal(progressMessages[0].details.access_token, undefined);
  assert.equal(parseBridgeOutput(result.stdout).ok, true);
});

for (const mode of ["abort", "timeout"]) {
  test(`${mode} terminates a resistant bridge descendant`, async () => {
    let descendantPid;
    let resolvePid;
    const pidReady = new Promise((resolve) => {
      resolvePid = resolve;
    });
    const abortController = new AbortController();
    const descendantScript =
      "process.on('SIGTERM', () => {}); setInterval(() => {}, 1000)";
    const parentScript = [
      "const { spawn } = require('node:child_process');",
      `const child = spawn(process.execPath, ['-e', ${JSON.stringify(descendantScript)}], { stdio: 'ignore' });`,
      "setTimeout(() => console.log(JSON.stringify({ type: 'progress', phase: String(child.pid) })), 100);",
      "setInterval(() => {}, 1000);",
    ].join("\n");

    const run = executeBridge(
      process.execPath,
      ["-e", parentScript],
      { timeout: mode === "timeout" ? 200 : 10000 },
      {
        signal: mode === "abort" ? abortController.signal : undefined,
        onProgress: (phase) => {
          descendantPid = Number(phase);
          resolvePid();
        },
      },
    );

    await pidReady;
    if (mode === "abort") abortController.abort();
    try {
      await assert.rejects(
        run,
        mode === "abort" ? /同步已停止/ : /同步超时/,
      );
      assert.equal(processExists(descendantPid), false);
    } finally {
      if (descendantPid && processExists(descendantPid)) {
        process.kill(descendantPid, "SIGKILL");
      }
    }
  });
}

test("serves APIs and rejects cross-origin mutations", async (context) => {
  const controller = new SyncController({
    autoSync: false,
    runner: async () => successfulResult(),
  });
  const server = createPassportServer({ controller });
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  context.after(() => new Promise((resolve) => server.close(resolve)));
  const address = server.address();
  const base = `http://127.0.0.1:${address.port}`;

  const health = await fetch(`${base}/healthz`);
  assert.equal(health.status, 200);
  assert.deepEqual(await health.json(), { ok: true });

  const status = await fetch(`${base}/api/status`);
  assert.equal(status.status, 200);
  const initialStatus = await status.json();
  assert.equal(initialStatus.service.autoSync, false);
  assert.ok(Array.isArray(initialStatus.logs));

  const rejected = await fetch(`${base}/api/sync`, {
    method: "POST",
    headers: { "Content-Type": "application/json", Origin: "https://example.com" },
    body: "{}",
  });
  assert.equal(rejected.status, 403);

  const accepted = await fetch(`${base}/api/sync`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: "{}",
  });
  assert.equal(accepted.status, 202);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(controller.snapshot().sync.state, "success");
  assert.match(controller.snapshot().logs.at(-1).message, /同步完成/);

  const updated = await fetch(`${base}/api/settings`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ autoSync: true, intervalSeconds: 600 }),
  });
  assert.equal(updated.status, 200);
  assert.equal((await updated.json()).service.intervalSeconds, 600);
});

test("accepts a same-origin demo configuration and sends it over the demo runner", async (context) => {
  let receivedSummary = null;
  const controller = new SyncController({
    autoSync: false,
    runner: async () => successfulResult(),
    demoRunner: async ({ summary }) => {
      receivedSummary = summary;
      return { ok: true, payload_bytes: 321, summary };
    },
  });
  const server = createPassportServer({ controller });
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  context.after(() => new Promise((resolve) => server.close(resolve)));
  const { port } = server.address();
  const summary = {
    version: 4,
    user_name: "演示用户",
    date: "2026-09-05",
    avatar_rgb565: "",
    events: [],
    tasks: [],
    overdue_task_count: 0,
    unread_message_count: 0,
  };

  const configured = await request({
    port,
    path: "/api/demo",
    method: "PUT",
    headers: {
      Host: `127.0.0.1:${port}`,
      "Content-Type": "application/json",
    },
    chunks: [JSON.stringify({ enabled: true, summary })],
  });
  assert.equal(configured.status, 200);
  assert.equal(JSON.parse(configured.body).service.demoEnabled, true);

  const accepted = await request({
    port,
    path: "/api/sync",
    method: "POST",
    headers: {
      Host: `127.0.0.1:${port}`,
      "Content-Type": "application/json",
    },
    chunks: ["{}"],
  });
  assert.equal(accepted.status, 202);
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(receivedSummary, summary);
});

test("rejects malformed and foreign Host values without crashing", async (context) => {
  const controller = new SyncController({
    autoSync: false,
    runner: async () => successfulResult(),
  });
  const server = createPassportServer({ controller });
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  context.after(() => new Promise((resolve) => server.close(resolve)));
  const { port } = server.address();

  for (const host of ["evil.example", "[", `127.0.0.1:${port + 1}`]) {
    const response = await request({
      port,
      path: "/api/status",
      headers: { Host: host },
    });
    assert.equal(response.status, 421);
  }

  const healthy = await request({
    port,
    path: "/healthz",
    headers: { Host: `localhost:${port}` },
  });
  assert.equal(healthy.status, 200);
});

test("closes an active SSE client during server shutdown", async () => {
  const controller = new SyncController({
    autoSync: false,
    runner: async () => successfulResult(),
  });
  const server = createPassportServer({ controller });
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  const { port } = server.address();
  const responseReady = new Promise((resolve, reject) => {
    const outgoing = http.get(
      {
        host: "127.0.0.1",
        port,
        path: "/api/events",
        headers: { Host: `127.0.0.1:${port}` },
      },
      (incoming) => {
        incoming.once("data", () => resolve(incoming));
      },
    );
    outgoing.on("error", reject);
  });
  const incoming = await responseReady;
  const responseEnded = once(incoming, "end");
  const closed = new Promise((resolve) => server.close(resolve));

  server.closeEventClients();
  await Promise.race([
    Promise.all([closed, responseEnded]),
    delay(1000).then(() => {
      throw new Error("server shutdown timed out with an active SSE client");
    }),
  ]);
  assert.equal(incoming.complete, true);
});

test("enforces CSP, body limits, and split UTF-8 JSON input", async (context) => {
  const controller = new SyncController({
    autoSync: false,
    runner: async () => successfulResult(),
  });
  const server = createPassportServer({ controller });
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  context.after(() => new Promise((resolve) => server.close(resolve)));
  const { port } = server.address();
  const host = `127.0.0.1:${port}`;

  const page = await request({ port, headers: { Host: host } });
  assert.equal(page.status, 200);
  assert.match(page.headers["content-security-policy"], /default-src 'self'/);
  assert.equal(page.headers["x-content-type-options"], "nosniff");
  assert.equal(page.headers["cache-control"], "no-store");

  const script = await request({
    port,
    path: "/app.js?v=cache-test",
    headers: { Host: host },
  });
  assert.equal(script.status, 200);
  assert.equal(script.headers["cache-control"], "no-store");

  const encoded = Buffer.from(
    JSON.stringify({
      autoSync: true,
      intervalSeconds: 600,
      note: "中文",
    }),
  );
  const splitAt = encoded.indexOf(Buffer.from("中")) + 1;
  const updated = await request({
    port,
    path: "/api/settings",
    method: "PUT",
    headers: {
      Host: host,
      "Content-Type": "application/json",
    },
    chunks: [encoded.subarray(0, splitAt), encoded.subarray(splitAt)],
  });
  assert.equal(updated.status, 200);

  const oversized = await request({
    port,
    path: "/api/settings",
    method: "PUT",
    headers: {
      Host: host,
      "Content-Type": "application/json",
    },
    chunks: [JSON.stringify({ padding: "x".repeat(17 * 1024) })],
  });
  assert.equal(oversized.status, 413);
});
