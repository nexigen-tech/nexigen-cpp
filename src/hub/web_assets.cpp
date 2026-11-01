// nexigen::hub — embedded web assets
// (change nexigen-hub; spec hub/web-ui).
#include <string_view>

#include <nexigen/hub/web_assets.hpp>

namespace nexigen::hub {

std::string_view index_html() noexcept {
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>nexigen hub</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<header>
  <h1>nexigen hub</h1>
  <span id="connection" class="status disconnected">connecting</span>
</header>
<main>
  <aside id="tree" aria-label="run tree"></aside>
  <section id="detail">
    <h2 id="title">Select a stream</h2>
    <div id="state"></div>
    <div id="streams" aria-label="streams"></div>
    <div class="toolbar">
      <button id="pause" type="button">Pause</button>
      <button id="clear" type="button">Clear</button>
      <label><input id="follow" type="checkbox" checked> Follow</label>
      <span id="cursor"></span>
    </div>
    <pre id="viewer" aria-live="polite"></pre>
  </section>
</main>
<script src="/app.js"></script>
</body>
</html>)HTML";
}

std::string_view style_css() noexcept {
    return R"CSS(:root{color-scheme:dark;--bg:#11151b;--panel:#18202a;--line:#2a3644;--fg:#d7e0ea;--dim:#8b9bad;--accent:#4db6ac;--warn:#ffb74d;--err:#ef5350}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:13px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
header{display:flex;align-items:center;justify-content:space-between;padding:10px 14px;border-bottom:1px solid var(--line);background:var(--panel)}
h1{font-size:15px;margin:0;letter-spacing:.04em}
h2{font-size:13px;margin:0 0 8px;color:var(--accent)}
main{display:grid;grid-template-columns:minmax(240px,28%) 1fr;height:calc(100vh - 45px)}
#tree{overflow:auto;border-right:1px solid var(--line);padding:8px}
#detail{display:flex;flex-direction:column;min-width:0;padding:10px;gap:8px}
#state{color:var(--dim);white-space:pre-wrap}
#streams{display:flex;flex-wrap:wrap;gap:6px}
button{background:#22303d;color:var(--fg);border:1px solid var(--line);padding:4px 8px;border-radius:4px;cursor:pointer}
button:hover{border-color:var(--accent)}
.stream{font-size:12px;padding:3px 7px;border:1px solid var(--line);border-radius:12px;cursor:pointer;background:#1b2530}
.stream.selected{border-color:var(--accent);color:var(--accent)}
.toolbar{display:flex;align-items:center;gap:10px;color:var(--dim)}
#viewer{flex:1;min-height:0;margin:0;overflow:auto;background:#0c1015;border:1px solid var(--line);padding:8px;white-space:pre-wrap;word-break:break-word}
.node{padding:2px 4px;cursor:pointer;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.node:hover{background:#202b37}
.node.suite{color:var(--accent)}
.node.failed{color:var(--err)}
.node.degraded{color:var(--warn)}
.dim{color:var(--dim)}
.status{padding:2px 8px;border-radius:10px;font-size:12px}
.status.connected{background:#153a34;color:#7fe0d0}
.status.disconnected{background:#3a2020;color:#ff9a9a}
@media(max-width:760px){main{grid-template-columns:1fr;grid-template-rows:35% 1fr}#tree{border-right:0;border-bottom:1px solid var(--line)}}
)CSS";
}

std::string_view app_js() noexcept {
    return R"JS((() => {
  "use strict";
  const treeEl = document.getElementById("tree");
  const titleEl = document.getElementById("title");
  const stateEl = document.getElementById("state");
  const streamsEl = document.getElementById("streams");
  const viewerEl = document.getElementById("viewer");
  const connEl = document.getElementById("connection");
  const pauseEl = document.getElementById("pause");
  const clearEl = document.getElementById("clear");
  const followEl = document.getElementById("follow");
  const cursorEl = document.getElementById("cursor");

  let tree = {runs: []};
  let selected = null;
  let source = null;
  let lastSequence = 0;
  let paused = false;
  let lineCount = 0;
  const maxLines = 5000;
  const pausedLines = [];
  let reconnectTimer = null;

  const setConnection = (ok, text) => {
    connEl.className = "status " + (ok ? "connected" : "disconnected");
    connEl.textContent = text;
  };

  const stateBadge = (state) => {
    const span = document.createElement("span");
    span.className = "dim";
    span.textContent = " [" + (state || "unknown") + "]";
    return span;
  };

  const node = (text, cls, onClick) => {
    const div = document.createElement("div");
    div.className = "node " + cls;
    div.textContent = text;
    if (onClick) div.addEventListener("click", onClick);
    return div;
  };

  const allStreams = () => {
    const out = [];
    const push = (stream, label) => {
      if (stream && stream.id) out.push({stream, label});
    };
    for (const run of tree.runs || []) {
      for (const suite of run.suites || []) {
        push(suite.runner_log, run.run_key + "/" + suite.suite_id + "/runner");
        for (const app of suite.apps || []) {
          for (const inst of app.instances || []) {
            for (const attempt of inst.attempts || []) {
              for (const stream of attempt.streams || []) {
                push(stream, run.run_key + "/" + suite.suite_id + "/" + app.app + "/" + inst.instance + "/" + attempt.attempt + "/" + stream.kind);
              }
            }
          }
        }
      }
      for (const inst of run.standalone_instances || []) {
        for (const attempt of inst.attempts || []) {
          for (const stream of attempt.streams || []) {
            push(stream, run.run_key + "/standalone/" + inst.instance + "/" + stream.kind);
          }
        }
      }
    }
    return out;
  };

  const renderTree = () => {
    treeEl.textContent = "";
    if (!tree.runs || tree.runs.length === 0) {
      treeEl.appendChild(node("No runs discovered.", "dim"));
      return;
    }
    for (const run of tree.runs) {
      treeEl.appendChild(node(run.run_key + "  " + run.run_dir, "dim"));
      for (const suite of run.suites || []) {
        const s = node("suite " + suite.suite_id, "suite", () => selectSuite(suite));
        s.appendChild(stateBadge(suite.state));
        treeEl.appendChild(s);
        for (const app of suite.apps || []) {
          const a = node("  app " + app.app, "", () => selectApp(app));
          a.appendChild(stateBadge(app.state));
          a.style.paddingLeft = "16px";
          treeEl.appendChild(a);
          for (const inst of app.instances || []) {
            const i = node("    instance " + inst.instance, "", () => selectInstance(inst));
            i.appendChild(stateBadge(inst.state));
            i.style.paddingLeft = "28px";
            treeEl.appendChild(i);
            for (const attempt of inst.attempts || []) {
              const t = node("      attempt " + attempt.attempt, "dim", () => renderStreams(inst, attempt));
              t.style.paddingLeft = "40px";
              treeEl.appendChild(t);
            }
          }
        }
      }
      for (const inst of run.standalone_instances || []) {
        treeEl.appendChild(node("  standalone " + inst.instance, "", () => selectInstance(inst)));
      }
    }
  };

  const selectSuite = (suite) => {
    titleEl.textContent = "suite " + suite.suite_id;
    stateEl.textContent = "state: " + suite.state + "\nrestarts used: " + (suite.suite_restarts_used || 0) +
      (suite.reason ? "\nreason: " + suite.reason : "");
    streamsEl.textContent = "";
    for (const app of suite.apps || []) {
      for (const inst of app.instances || []) {
        for (const attempt of inst.attempts || []) {
          renderStreams(inst, attempt);
        }
      }
    }
  };

  const selectApp = (app) => {
    titleEl.textContent = "app " + app.app;
    stateEl.textContent = "state: " + app.state;
    streamsEl.textContent = "";
    for (const inst of app.instances || []) {
      for (const attempt of inst.attempts || []) renderStreams(inst, attempt);
    }
  };

  const selectInstance = (inst) => {
    titleEl.textContent = "instance " + inst.instance;
    stateEl.textContent = "state: " + inst.state + "\n" + (inst.instance_dir || "");
    streamsEl.textContent = "";
    for (const attempt of inst.attempts || []) renderStreams(inst, attempt);
  };

  const renderStreams = (inst, attempt) => {
    for (const stream of attempt.streams || []) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "stream";
      button.textContent = stream.kind + " #" + attempt.attempt;
      button.dataset.id = stream.id;
      button.addEventListener("click", () => selectStream(stream, attempt));
      streamsEl.appendChild(button);
    }
  };

  const appendLine = (text) => {
    if (paused) {
      pausedLines.push(text);
      if (pausedLines.length > maxLines) pausedLines.shift();
      return;
    }
    viewerEl.textContent += text + "\n";
    lineCount += 1;
    while (lineCount > maxLines) {
      const first = viewerEl.textContent.indexOf("\n");
      if (first < 0) break;
      viewerEl.textContent = viewerEl.textContent.slice(first + 1);
      lineCount -= 1;
    }
    if (followEl.checked) viewerEl.scrollTop = viewerEl.scrollHeight;
  };

  const closeSource = () => {
    if (source) { source.close(); source = null; }
  };

  const scheduleReconnect = () => {
    setConnection(false, "reconnecting");
    closeSource();
    if (reconnectTimer !== null) return;
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      if (selected) subscribe();
    }, 1000);
  };

  const subscribe = () => {
    closeSource();
    if (!selected) return;
    const url = "/api/events?stream=" + encodeURIComponent(selected.id) +
      "&since=" + lastSequence;
    source = new EventSource(url);
    source.addEventListener("record", (event) => {
      const record = JSON.parse(event.data);
      if (record.sequence <= lastSequence) return;
      lastSequence = record.sequence;
      cursorEl.textContent = "seq " + lastSequence;
      appendLine(record.line);
      setConnection(true, "live");
    });
    source.addEventListener("snapshot_begin", () => {
      viewerEl.textContent = "";
      lineCount = 0;
      pausedLines.length = 0;
    });
    source.addEventListener("snapshot_end", () => setConnection(true, "live"));
    source.addEventListener("gap", (event) => {
      const gap = JSON.parse(event.data);
      appendLine("[gap: dropped " + gap.dropped + " records]");
    });
    source.onopen = () => setConnection(true, "connected");
    source.onerror = scheduleReconnect;
  };

  const selectStream = (stream, attempt) => {
    if (reconnectTimer !== null) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    selected = stream;
    lastSequence = 0;
    pausedLines.length = 0;
    titleEl.textContent = stream.kind + "  " + stream.id;
    stateEl.textContent = "path: " + stream.path + "\nattempt: " + (attempt ? attempt.attempt : "-");
    viewerEl.textContent = "";
    lineCount = 0;
    for (const button of streamsEl.querySelectorAll(".stream")) {
      button.classList.toggle("selected", button.dataset.id === stream.id);
    }
    subscribe();
  };

  const treeSource = new EventSource("/api/events?tree=1");
  treeSource.addEventListener("tree", (event) => {
    tree = JSON.parse(event.data);
    renderTree();
    setConnection(true, "connected");
  });
  treeSource.addEventListener("error", () => setConnection(false, "reconnecting"));

  fetch("/api/tree").then((response) => response.json()).then((value) => {
    tree = value;
    renderTree();
  }).catch(() => setConnection(false, "server unavailable"));

  pauseEl.addEventListener("click", () => {
    paused = !paused;
    pauseEl.textContent = paused ? "Resume" : "Pause";
    if (!paused) {
      const pending = pausedLines.splice(0);
      for (const line of pending) appendLine(line);
    }
  });
  clearEl.addEventListener("click", () => {
    viewerEl.textContent = "";
    lineCount = 0;
    pausedLines.length = 0;
  });
})();
)JS";
}

} // namespace nexigen::hub
