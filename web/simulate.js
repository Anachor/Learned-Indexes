// Simulate tab, with the run's seed and permutation. Two actions, both run by
// the server with the experiment's program:
//
//   Simulate        plays the best delta's segments for t = n/8, n/4, ..., n
//   Inspect prefix  one prefix length t: delta = 1/2, 1, 3/2, ... until no larger
//                   delta can lower qc = log2(delta) + log2(lambda); the table,
//                   and the segments of whichever delta is picked
//
// Nothing runs until one of the buttons is pressed. app.js calls setupSimulate
// once the experiment has loaded, and the returned shown() each time the tab
// is opened.

// sizes: the n values each run has data for, offered in the n box.
function setupSimulate(experiment, runs, sizes) {
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
  const intro = document.getElementById("sim-intro");
  const simRun = document.getElementById("sim-run");
  const runPicker = document.getElementById("run");  // on the Plots tab
  const nPicker = document.getElementById("n");
  const nPick = document.getElementById("sim-n-pick");
  const simulateButton = document.getElementById("sim-simulate");
  const inspectButton = document.getElementById("sim-inspect");

  for (const run of runs) simRun.appendChild(new Option(run, run));

  // -- defaults: the run and n open on the Plots tab; t left empty -----------

  // n: a dropdown of the run's n values beside a box for any n. Picking one
  // fills the box; typing another n sets the dropdown to "other".
  function offerSizes() {
    nPick.textContent = "";
    for (const n of (sizes || {})[simRun.value] || []) nPick.appendChild(new Option(n.toLocaleString("en-US"), n));
    nPick.appendChild(new Option("other", ""));
    matchPick();
  }

  function matchPick() {
    const known = [...nPick.options].some((o) => o.value !== "" && o.value === nInput.value);
    nPick.value = known ? nInput.value : "";
  }

  simRun.addEventListener("change", offerSizes);
  nPick.addEventListener("change", () => {
    if (nPick.value === "") return nInput.focus();
    nInput.value = nPick.value;
  });
  nInput.addEventListener("input", matchPick);

  function currentN() {
    // The n picker may be on "Across n"; then the smallest n it offers.
    if (/^\d+$/.test(nPicker.value)) return Number(nPicker.value);
    const option = [...nPicker.options].find((o) => /^\d+$/.test(o.value));
    return option ? Number(option.value) : 1024;
  }

  function prefill() {
    simRun.value = runPicker.value;
    nInput.value = currentN();
    offerSizes();
  }

  // The n in the box, or null (with a message) when it is not a whole number.
  function readN() {
    const text = nInput.value.replace(/[,\s_]/g, "");
    if (!/^\d+$/.test(text) || Number(text) < 1) {
      status.textContent = "n must be a whole number, at least 1.";
      return null;
    }
    return Number(text);
  }

  // -- running ----------------------------------------------------------------

  let data = null;      // the last simulation
  let selected = 0;     // index into data.deltas of the delta drawn
  let segments = {};    // k -> that delta's segments, as fetched
  let view = null;      // [lowest key, highest key] shown, or null for all

  // Every simulation asked for, by "run:n:t": Simulate's prefixes and Inspect
  // prefix share it, so nothing is fetched twice. Failures are dropped from it so
  // they can be retried.
  const cache = new Map();

  function simulation(run, n, t) {
    const key = run + ":" + n + ":" + t;
    if (!cache.has(key)) {
      const query = "?run=" + run + "&n=" + n + "&t=" + t;
      const request = fetch("/api/simulate/" + encodeURIComponent(experiment) + query)
        .then((response) => response.json().then((payload) => {
          if (!response.ok) throw new Error(payload.error || response.statusText);
          return payload;
        }));
      request.catch(() => cache.delete(key));
      cache.set(key, request);
    }
    return cache.get(key);
  }

  // Inspect prefix: the form's submit, so Enter in the t box does it too.
  form.addEventListener("submit", (event) => {
    event.preventDefault();
    const n = readN();
    if (n === null) return;
    if (tInput.value === "") {
      status.textContent = "Enter a prefix length t, 1 to " + n.toLocaleString("en-US") + ".";
      tInput.focus();
      return;
    }
    const t = Number(tInput.value);
    if (!(Number.isInteger(t) && t >= 1 && t <= n)) {
      status.textContent = "t must be a whole number from 1 to n.";
      return;
    }

    inspectButton.disabled = true;
    status.textContent = "Running…";
    simulation(simRun.value, n, t)
      .then((payload) => {
        status.textContent = "";
        show(payload);
      })
      .catch((error) => {
        status.textContent = "Simulation failed: " + error.message;
      })
      .finally(() => {
        inspectButton.disabled = false;
      });
  });

  simulateButton.addEventListener("click", () => {
    const n = readN();
    if (n === null) return;
    status.textContent = "";
    loadPlayer(n);
    player.scrollIntoView({ behavior: "smooth", block: "nearest" });
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
    return delta.toLocaleString("en-US");
  }

  function show(payload) {
    data = payload;
    const best = bestIndex(data.deltas);
    const winner = data.deltas[best];
    selected = best;
    view = null;
    segments = { [data.best_k]: data.segments };

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
      tr.addEventListener("click", () => pick(i));
      body.appendChild(tr);
    });

    // Why the search stopped: lambda >= 1, so qc >= log2(delta) for every
    // larger delta, and at the next delta that already reaches the best.
    const next = data.deltas[data.deltas.length - 1].delta + 0.5;
    stop.textContent =
      data.deltas.length + " values of δ tried, stopped after δ = " + formatDelta(next - 0.5) +
      ": every δ ≥ " + formatDelta(next) + " has qc ≥ log₂δ ≥ " + Math.log2(next).toFixed(3) +
      ", no lower than the best, " + winner.qc.toFixed(3) + ".";
    command.textContent = data.command;

    result.hidden = false;
    intro.hidden = true;
    markSelected();
    draw();

    // Bring the best row into view in the scrolling table, not the page.
    const wrap = body.closest(".table-wrap");
    const row = body.rows[best];
    wrap.scrollTop = row.offsetTop - wrap.clientHeight / 2;
  }

  // Draws another delta, fetching its segments the first time.
  function pick(i) {
    selected = i;
    markSelected();
    const k = data.deltas[i].k;
    if (segments[k]) return draw();

    draw();  // the points, until the segments arrive
    const asked = data;
    const query = "?run=" + data.run + "&n=" + data.n + "&t=" + data.t + "&k=" + k;
    fetch("/api/simulate/" + encodeURIComponent(experiment) + query)
      .then((response) => response.json().then((payload) => {
        if (!response.ok) throw new Error(payload.error || response.statusText);
        return payload;
      }))
      .then((payload) => {
        if (asked !== data) return;  // a new simulation has replaced this one
        segments[k] = payload.segments;
        markSelected();
        draw();
      })
      .catch((error) => {
        if (asked === data) plotHeading.textContent += " — could not load: " + error.message;
      });
  }

  function markSelected() {
    [...body.rows].forEach((tr, i) => tr.classList.toggle("selected", i === selected));
    const row = data.deltas[selected];
    plotHeading.textContent = "δ = " + formatDelta(row.delta) + ", λ = " +
      row.lambda.toLocaleString("en-US") + " segments" + (segments[row.k] ? "" : " — loading…");
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

  // Pixel mapping for one canvas: keys across, ranks up, over the key range
  // shown (all of it when range is null), the ranks widened by the band.
  function geometryFor(target, keys, delta, range, margin) {
    let [x0, x1] = range || [keys[0], keys[keys.length - 1]];
    if (x0 === x1) { x0 -= 1; x1 += 1; }

    // The ranks of the keys in view, widened by the band.
    const first = lowerBound(keys, x0);
    const last = lowerBound(keys, x1 + 1e-9) - 1;
    let y0 = (last >= first ? first : 0) - delta;
    let y1 = (last >= first ? last : keys.length - 1) + delta;
    const pad = (y1 - y0) * 0.04 || 1;
    y0 -= pad;
    y1 += pad;

    const width = target.clientWidth;
    const height = target.clientHeight;
    const plotWidth = width - margin.left - margin.right;
    const plotHeight = height - margin.top - margin.bottom;
    return {
      x0, x1, y0, y1, first, last, width, height, plotWidth, plotHeight,
      px: (x) => margin.left + (x - x0) / (x1 - x0) * plotWidth,
      py: (y) => margin.top + (1 - (y - y0) / (y1 - y0)) * plotHeight,
      key: (px) => x0 + (px - margin.left) / plotWidth * (x1 - x0),
    };
  }

  function geometry() {
    return geometryFor(canvas, data.keys, data.deltas[selected].delta, view, MARGIN);
  }

  // Hide segments: one setting for both plots, so the keys alone can be seen.
  let segmentsShown = true;
  const segmentToggles = document.querySelectorAll(".segments-toggle");
  for (const button of segmentToggles) {
    button.addEventListener("click", () => {
      segmentsShown = !segmentsShown;
      for (const b of segmentToggles) {
        b.textContent = segmentsShown ? "Hide segments" : "Show segments";
        b.setAttribute("aria-pressed", String(!segmentsShown));
        b.classList.toggle("active", !segmentsShown);
      }
      draw();
      drawFrame();
    });
  }

  // The main plot: the selected delta of the last simulation.
  function draw() {
    if (!data || result.hidden) return;
    const row = data.deltas[selected];
    const segs = segmentsShown ? segments[row.k] || [] : [];
    render(canvas, data.keys, segs, row.delta, view, MARGIN, drag);
  }

  // Draws keys against rank on one canvas, with each segment's line and its
  // +-delta band: the main plot and the overview's small ones alike.
  function render(target, keys, segs, delta, range, margin, dragBox) {
    const ratio = window.devicePixelRatio || 1;
    const cssWidth = target.clientWidth;
    const cssHeight = target.clientHeight;
    if (!cssWidth) return;
    target.width = Math.round(cssWidth * ratio);
    target.height = Math.round(cssHeight * ratio);
    const context = target.getContext("2d");
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    context.clearRect(0, 0, cssWidth, cssHeight);

    const g = geometryFor(target, keys, delta, range, margin);
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
      context.moveTo(px, margin.top);
      context.lineTo(px, margin.top + g.plotHeight);
      context.stroke();
      context.fillText(x.toLocaleString("en-US"), px, margin.top + g.plotHeight + 6);
    }
    context.textAlign = "right";
    context.textBaseline = "middle";
    for (const y of ticks(g.y0, g.y1, Math.max(2, Math.floor(g.plotHeight / 60)))) {
      const py = Math.round(g.py(y)) + 0.5;
      context.beginPath();
      context.moveTo(margin.left, py);
      context.lineTo(margin.left + g.plotWidth, py);
      context.stroke();
      context.fillText(y.toLocaleString("en-US"), margin.left - 8, py);
    }
    context.textAlign = "center";
    context.textBaseline = "bottom";
    context.fillText("key", margin.left + g.plotWidth / 2, g.height - 2);
    context.save();
    context.translate(10, margin.top + g.plotHeight / 2);
    context.rotate(-Math.PI / 2);
    context.textBaseline = "middle";
    context.fillText("rank", 0, 0);
    context.restore();

    context.save();
    context.beginPath();
    context.rect(margin.left, margin.top, g.plotWidth, g.plotHeight);
    context.clip();

    // Segments in view: the band first, the points over it, the line on top.
    // Neighbouring segments alternate colours so the boundaries show.
    const visible = [];
    segs.forEach(([begin, end, lx0, ly0, slope], s) => {
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

    if (dragBox) {
      const [a, b] = dragBox;
      context.fillStyle = colour("--link");
      context.globalAlpha = 0.15;
      context.fillRect(Math.min(a, b), margin.top, Math.abs(b - a), g.plotHeight);
      context.globalAlpha = 1;
    }
    context.restore();

    context.strokeStyle = colour("--border");
    context.lineWidth = 1;
    context.strokeRect(margin.left + 0.5, margin.top + 0.5, g.plotWidth, g.plotHeight);
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

  // -- Simulate: the optimal segments as the prefix grows ---------------------
  //
  // Plays t = n/8, n/4, ..., n for the run and n in the form: one plot, stepping
  // through the prefixes, the keys and the best delta's segments redrawn at
  // each. Every simulation is cached, so replaying and scrubbing do not ask the
  // server again.

  const player = document.getElementById("sim-player");
  const playerHeading = document.getElementById("player-heading");
  const playButton = document.getElementById("player-play");
  const frameSlider = document.getElementById("player-frame");
  const frameLabel = document.getElementById("player-label");
  const openButton = document.getElementById("player-open");
  const playerCanvas = document.getElementById("player-plot");
  const PLAYER_MARGIN = { left: 92, right: 16, top: 12, bottom: 40 };
  const FRAME_MS = 1400;

  let frames = [];         // {i, t, sim} per prefix, sim null until loaded
  let frameIndex = 0;
  let playerKey = null;    // "run:n" the player shows
  let playerN = 0;
  let timer = null;

  // t = i n / 8 for i = 1..8; for n < 8 some coincide, and are shown once.
  function prefixes(n) {
    const out = [];
    for (let i = 1; i <= 8; i++) {
      const t = Math.max(1, Math.round(i * n / 8));
      if (!out.some((p) => p.t === t)) out.push({ i, t });
    }
    return out;
  }

  function fraction(i) {
    return { 1: "n/8", 2: "n/4", 3: "3n/8", 4: "n/2", 5: "5n/8", 6: "3n/4", 7: "7n/8", 8: "n" }[i];
  }

  function drawFrame() {
    const frame = frames[frameIndex];
    if (!frame) return;
    frameSlider.value = frameIndex;
    const head = "t = " + fraction(frame.i) + " = " + frame.t.toLocaleString("en-US");
    if (!frame.sim) {
      frameLabel.textContent = head + " — loading…";
      openButton.disabled = true;
      return;
    }
    const winner = frame.sim.deltas[bestIndex(frame.sim.deltas)];
    frameLabel.textContent = head + " — best δ = " + formatDelta(winner.delta) +
      ", λ = " + winner.lambda.toLocaleString("en-US") + ", qc = " + winner.qc.toFixed(3);
    openButton.disabled = false;
    // Keys across 1..n in every frame, so the prefix is seen filling in.
    const segs = segmentsShown ? frame.sim.segments : [];
    render(playerCanvas, frame.sim.keys, segs, winner.delta, [1, playerN], PLAYER_MARGIN, null);
  }

  function pause() {
    clearInterval(timer);
    timer = null;
    playButton.textContent = "Play";
  }

  function play() {
    if (timer) return;
    if (frameIndex === frames.length - 1) frameIndex = 0;  // replay from the start
    playButton.textContent = "Pause";
    drawFrame();
    timer = setInterval(() => {
      // Wait on a frame still loading rather than skip it.
      if (!frames[frameIndex] || !frames[frameIndex].sim) return;
      if (frameIndex === frames.length - 1) return pause();
      frameIndex += 1;
      drawFrame();
    }, FRAME_MS);
  }

  // Loads (from the cache where it can) and plays, from the start, the
  // prefixes of n for the run in the form.
  function loadPlayer(n) {
    const run = simRun.value;
    const key = run + ":" + n;
    playerKey = key;
    playerN = n;

    pause();
    playerHeading.textContent = "Optimal segments as the prefix grows — run " + run +
      ", n = " + n.toLocaleString("en-US");
    frames = prefixes(n).map(({ i, t }) => ({ i, t, sim: null }));
    frameIndex = 0;
    frameSlider.max = frames.length - 1;
    player.hidden = false;
    for (const frame of frames) {
      simulation(run, n, frame.t)
        .then((payload) => {
          if (playerKey !== key) return;  // replaced by another run or n
          frame.sim = payload;
          if (frame === frames[frameIndex]) drawFrame();
        })
        .catch((error) => {
          if (playerKey === key && frame === frames[frameIndex]) {
            frameLabel.textContent += " failed: " + error.message;
          }
        });
    }
    play();
  }

  playButton.addEventListener("click", () => (timer ? pause() : play()));
  frameSlider.addEventListener("input", () => {
    pause();
    frameIndex = Number(frameSlider.value);
    drawFrame();
  });
  openButton.addEventListener("click", () => {
    const frame = frames[frameIndex];
    if (!frame || !frame.sim) return;
    pause();
    tInput.value = frame.t;
    show(frame.sim);
    result.scrollIntoView({ behavior: "smooth", block: "start" });
  });

  new ResizeObserver(draw).observe(canvas);
  new ResizeObserver(drawFrame).observe(playerCanvas);
  window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => {
    draw();
    drawFrame();
  });

  // Until something has been simulated, opening the tab takes the run and n
  // the Plots tab shows; after that it keeps the inputs as they were.
  return {
    shown() {
      if (!data && !playerKey) prefill();
      draw();
      drawFrame();
    },
  };
}
