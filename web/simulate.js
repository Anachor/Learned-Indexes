// Simulate: one prefix of one n, with the run's seed. The server runs the
// experiment's program, which doubles delta from 1/2 until no larger delta can
// lower qc = log2(delta) + log2(lambda), and sends back every delta it tried
// with its segments. This file shows them as a table and draws the segments.
//
// app.js calls setupSimulate once the experiment has loaded.

function setupSimulate(experiment) {
  const toggle = document.getElementById("simulate-toggle");
  const section = document.getElementById("simulation");
  const form = document.getElementById("simulate-form");
  const nInput = document.getElementById("sim-n");
  const tInput = document.getElementById("sim-t");
  const status = document.getElementById("sim-status");
  const result = document.getElementById("sim-result");
  const summary = document.getElementById("sim-summary");
  const body = document.querySelector("#sim-table tbody");
  const stop = document.getElementById("sim-stop");
  const command = document.getElementById("sim-command");
  const plotHeading = document.getElementById("plot-heading");
  const canvas = document.getElementById("sim-plot");
  const runPicker = document.getElementById("run");
  const nPicker = document.getElementById("n");

  toggle.hidden = false;

  // -- defaults: the n on screen, and half of it --------------------------

  let tEdited = false;  // once t is typed in, changing n leaves it alone

  function currentN() {
    // The n picker may be on "Across n"; then the smallest n it offers.
    if (/^\d+$/.test(nPicker.value)) return Number(nPicker.value);
    const option = [...nPicker.options].find((o) => /^\d+$/.test(o.value));
    return option ? Number(option.value) : 1024;
  }

  function defaultT() {
    return Math.max(1, Math.floor(Number(nInput.value) / 2));
  }

  function prefill() {
    nInput.value = currentN();
    tInput.value = defaultT();
    tEdited = false;
  }

  toggle.addEventListener("click", () => {
    section.hidden = !section.hidden;
    toggle.classList.toggle("active", !section.hidden);
    if (!section.hidden && !nInput.value) prefill();
  });
  nPicker.addEventListener("change", () => {
    if (!section.hidden) prefill();
  });
  nInput.addEventListener("input", () => {
    if (!tEdited) tInput.value = defaultT();
  });
  tInput.addEventListener("input", () => {
    tEdited = true;
  });

  // -- running ----------------------------------------------------------------

  let data = null;      // the last simulation
  let selected = 0;     // index into data.deltas of the delta drawn
  let view = null;      // [lowest key, highest key] shown, or null for all

  form.addEventListener("submit", (event) => {
    event.preventDefault();
    const n = Number(nInput.value);
    const t = Number(tInput.value);
    if (!(t >= 1 && t <= n)) {
      status.textContent = "t must be between 1 and n.";
      return;
    }

    const button = form.querySelector("button");
    button.disabled = true;
    status.textContent = "Running…";
    const query = "?run=" + runPicker.value + "&n=" + n + "&t=" + t;
    fetch("/api/simulate/" + encodeURIComponent(experiment) + query)
      .then((response) => response.json().then((payload) => {
        if (!response.ok) throw new Error(payload.error || response.statusText);
        return payload;
      }))
      .then((payload) => {
        status.textContent = "";
        show(payload);
      })
      .catch((error) => {
        status.textContent = "Simulation failed: " + error.message;
      })
      .finally(() => {
        button.disabled = false;
      });
  });

  // -- table --------------------------------------------------------------

  // The minimum of delta * lambda is the minimum of qc, and exact: delta is a
  // multiple of 1/2. Ties go to the smaller delta, the first one tried.
  function bestIndex(deltas) {
    let best = 0;
    deltas.forEach((row, i) => {
      if (row.delta * row.lambda < deltas[best].delta * deltas[best].lambda) best = i;
    });
    return best;
  }

  function formatDelta(delta) {
    return delta === 0.5 ? "1/2" : delta.toLocaleString("en-US");
  }

  function show(payload) {
    data = payload;
    const best = bestIndex(data.deltas);
    const winner = data.deltas[best];
    selected = best;
    view = null;

    summary.textContent =
      "run " + data.run + " · seed " + data.seed +
      " · n = " + data.n.toLocaleString("en-US") +
      " · t = " + data.t.toLocaleString("en-US") +
      " — best δ = " + formatDelta(winner.delta) +
      ", λ = " + winner.lambda.toLocaleString("en-US") +
      ", qc = " + winner.qc.toFixed(3);

    body.textContent = "";
    data.deltas.forEach((row, i) => {
      const tr = document.createElement("tr");
      if (i === best) tr.classList.add("best");
      for (const text of [formatDelta(row.delta), row.lambda.toLocaleString("en-US"), row.qc.toFixed(3)]) {
        const td = document.createElement("td");
        td.textContent = text;
        tr.appendChild(td);
      }
      tr.addEventListener("click", () => {
        selected = i;
        markSelected();
        draw();
      });
      body.appendChild(tr);
    });

    // Why the doubling stopped: lambda >= 1, so qc >= log2(delta) for every
    // larger delta, and at the next delta that already reaches the best.
    const last = data.deltas[data.deltas.length - 1].delta;
    stop.textContent =
      "Stopped after δ = " + formatDelta(last) + ": every δ ≥ " + formatDelta(2 * last) +
      " has qc ≥ log₂δ ≥ " + Math.log2(2 * last).toFixed(3) +
      ", no lower than the best, " + winner.qc.toFixed(3) + ".";
    command.textContent = data.command;

    result.hidden = false;
    markSelected();
    draw();
  }

  function markSelected() {
    [...body.rows].forEach((tr, i) => tr.classList.toggle("selected", i === selected));
    const row = data.deltas[selected];
    plotHeading.textContent = "δ = " + formatDelta(row.delta) + ", λ = " +
      row.lambda.toLocaleString("en-US") + " segments";
  }

  // -- plot -----------------------------------------------------------------

  const MARGIN = { left: 92, right: 16, top: 12, bottom: 40 };  // left: room for 1,048,576
  let drag = null;  // [start, current] in css pixels, while selecting a range

  function colour(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  }

  // First index with keys[i] >= value.
  function lowerBound(keys, value) {
    let lo = 0, hi = keys.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (keys[mid] < value) lo = mid + 1; else hi = mid;
    }
    return lo;
  }

  // About count round-numbered ticks covering [lo, hi].
  function ticks(lo, hi, count) {
    const raw = (hi - lo) / count;
    const power = Math.pow(10, Math.floor(Math.log10(raw)));
    const step = [1, 2, 5, 10].map((m) => m * power).find((s) => s >= raw) || raw;
    const out = [];
    // Multiples of the step, rounded so 0.1 + 0.2 and -0 print cleanly.
    for (let i = Math.ceil(lo / step); i * step <= hi; i++) out.push(Number((i * step).toPrecision(12)) + 0);
    return out;
  }

  function geometry() {
    const keys = data.keys;
    const delta = data.deltas[selected].delta;
    let [x0, x1] = view || [keys[0], keys[keys.length - 1]];
    if (x0 === x1) { x0 -= 1; x1 += 1; }

    // The ranks of the keys in view, widened by the band.
    const first = lowerBound(keys, x0);
    const last = lowerBound(keys, x1 + 1e-9) - 1;
    let y0 = (last >= first ? first : 0) - delta;
    let y1 = (last >= first ? last : keys.length - 1) + delta;
    const pad = (y1 - y0) * 0.04 || 1;
    y0 -= pad;
    y1 += pad;

    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    const plotWidth = width - MARGIN.left - MARGIN.right;
    const plotHeight = height - MARGIN.top - MARGIN.bottom;
    return {
      x0, x1, y0, y1, first, last, width, height, plotWidth, plotHeight,
      px: (x) => MARGIN.left + (x - x0) / (x1 - x0) * plotWidth,
      py: (y) => MARGIN.top + (1 - (y - y0) / (y1 - y0)) * plotHeight,
      key: (px) => x0 + (px - MARGIN.left) / plotWidth * (x1 - x0),
    };
  }

  function draw() {
    if (!data || result.hidden) return;
    const ratio = window.devicePixelRatio || 1;
    const cssWidth = canvas.clientWidth;
    const cssHeight = canvas.clientHeight;
    if (!cssWidth) return;
    canvas.width = Math.round(cssWidth * ratio);
    canvas.height = Math.round(cssHeight * ratio);
    const context = canvas.getContext("2d");
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    context.clearRect(0, 0, cssWidth, cssHeight);

    const g = geometry();
    const keys = data.keys;
    const row = data.deltas[selected];
    const delta = row.delta;
    const segmentColours = [colour("--series-1"), colour("--series-2")];

    // axes and grid
    context.font = "12px ui-monospace, SFMono-Regular, Menlo, monospace";
    context.fillStyle = colour("--muted");
    context.strokeStyle = colour("--grid");
    context.lineWidth = 1;
    context.textAlign = "center";
    context.textBaseline = "top";
    for (const x of ticks(g.x0, g.x1, Math.max(2, Math.floor(g.plotWidth / 110)))) {
      const px = Math.round(g.px(x)) + 0.5;
      context.beginPath();
      context.moveTo(px, MARGIN.top);
      context.lineTo(px, MARGIN.top + g.plotHeight);
      context.stroke();
      context.fillText(x.toLocaleString("en-US"), px, MARGIN.top + g.plotHeight + 6);
    }
    context.textAlign = "right";
    context.textBaseline = "middle";
    for (const y of ticks(g.y0, g.y1, Math.max(2, Math.floor(g.plotHeight / 60)))) {
      const py = Math.round(g.py(y)) + 0.5;
      context.beginPath();
      context.moveTo(MARGIN.left, py);
      context.lineTo(MARGIN.left + g.plotWidth, py);
      context.stroke();
      context.fillText(y.toLocaleString("en-US"), MARGIN.left - 8, py);
    }
    context.textAlign = "center";
    context.textBaseline = "bottom";
    context.fillText("key", MARGIN.left + g.plotWidth / 2, g.height - 2);
    context.save();
    context.translate(10, MARGIN.top + g.plotHeight / 2);
    context.rotate(-Math.PI / 2);
    context.textBaseline = "middle";
    context.fillText("rank", 0, 0);
    context.restore();

    context.save();
    context.beginPath();
    context.rect(MARGIN.left, MARGIN.top, g.plotWidth, g.plotHeight);
    context.clip();

    // Segments in view: the band first, the points over it, the line on top.
    // Neighbouring segments alternate colours so the boundaries show.
    const visible = [];
    row.segments.forEach(([begin, end, lx0, ly0, slope], s) => {
      const from = Math.max(keys[begin], g.x0);
      const to = Math.min(keys[end - 1], g.x1);
      if (from > to) return;
      visible.push({ from, to, line: (x) => ly0 + slope * (x - lx0), colour: segmentColours[s % 2] });
    });

    context.globalAlpha = 0.16;
    for (const s of visible) {
      context.fillStyle = s.colour;
      const a = g.px(s.from), b = Math.max(g.px(s.to), a + 2);  // a one-key segment still shows
      context.beginPath();
      context.moveTo(a, g.py(s.line(s.from) + delta));
      context.lineTo(b, g.py(s.line(s.to) + delta));
      context.lineTo(b, g.py(s.line(s.to) - delta));
      context.lineTo(a, g.py(s.line(s.from) - delta));
      context.closePath();
      context.fill();
    }
    context.globalAlpha = 1;

    // Points: dots while they can be told apart, single pixels once crowded.
    const count = g.last - g.first + 1;
    const crowded = count > g.plotWidth / 3;
    context.fillStyle = colour("--text");
    context.globalAlpha = crowded ? 0.5 : 0.85;
    for (let i = Math.max(0, g.first - 1); i <= Math.min(keys.length - 1, g.last + 1); i++) {
      const px = g.px(keys[i]), py = g.py(i);
      if (crowded) {
        context.fillRect(px - 0.5, py - 0.5, 1.5, 1.5);
      } else {
        context.beginPath();
        context.arc(px, py, 2.5, 0, 2 * Math.PI);
        context.fill();
      }
    }
    context.globalAlpha = 1;

    context.lineWidth = 2;
    for (const s of visible) {
      context.strokeStyle = s.colour;
      const a = g.px(s.from), b = Math.max(g.px(s.to), a + 2);
      context.beginPath();
      context.moveTo(a, g.py(s.line(s.from)));
      context.lineTo(b, g.py(s.line(s.to)));
      context.stroke();
    }

    if (drag) {
      const [a, b] = drag;
      context.fillStyle = colour("--link");
      context.globalAlpha = 0.15;
      context.fillRect(Math.min(a, b), MARGIN.top, Math.abs(b - a), g.plotHeight);
      context.globalAlpha = 1;
    }
    context.restore();

    context.strokeStyle = colour("--border");
    context.lineWidth = 1;
    context.strokeRect(MARGIN.left + 0.5, MARGIN.top + 0.5, g.plotWidth, g.plotHeight);
  }

  // -- zoom -------------------------------------------------------------------

  function offsetX(event) {
    const box = canvas.getBoundingClientRect();
    const g = geometry();
    return Math.min(Math.max(event.clientX - box.left, MARGIN.left), MARGIN.left + g.plotWidth);
  }

  canvas.addEventListener("mousedown", (event) => {
    if (!data) return;
    const x = offsetX(event);
    drag = [x, x];
    event.preventDefault();
  });
  window.addEventListener("mousemove", (event) => {
    if (!drag) return;
    drag[1] = offsetX(event);
    draw();
  });
  window.addEventListener("mouseup", () => {
    if (!drag) return;
    const [a, b] = drag;
    drag = null;
    if (Math.abs(b - a) >= 4) {
      const g = geometry();
      view = [g.key(Math.min(a, b)), g.key(Math.max(a, b))];
    }
    draw();
  });
  canvas.addEventListener("dblclick", () => {
    view = null;
    draw();
  });

  new ResizeObserver(draw).observe(canvas);
  window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", draw);
}
