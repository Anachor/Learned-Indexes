// Simulate tab, with the run's seed and permutation. The server runs the
// experiment's program on each prefix length of the range in the form - start
// to end in steps equal steps, both included (empty: n/2, n/2 and 1) - and streams
// them back as they finish. The player steps through them: at each prefix its
// table of every delta tried (delta = 1/2, 1, 3/2, ... until no larger delta
// can lower qc = log2(delta) + log2(lambda)), with the best marked, and the
// plot of the keys and the segments.
//
// Clicking a row pauses and draws that delta, fetching its segments the first
// time. Nothing runs until Simulate is pressed. app.js calls setupSimulate once
// the experiment has loaded, and the returned shown() each time the tab opens.

// sizes: the n values each run has data for, offered in the n box.
function setupSimulate(experiment, runs, sizes) {
  const form = document.getElementById("simulate-form");
  const nInput = document.getElementById("sim-n");
  const startInput = document.getElementById("sim-start");
  const endInput = document.getElementById("sim-end");
  const stepsInput = document.getElementById("sim-steps");
  const tiebreakerInput = document.getElementById("sim-tiebreaker");
  const status = document.getElementById("sim-status");
  const simRun = document.getElementById("sim-run");
  const runPicker = document.getElementById("run");  // on the Plots tab
  const nPicker = document.getElementById("n");
  const nValues = document.getElementById("sim-n-values");
  const nOpen = document.getElementById("sim-n-open");

  const player = document.getElementById("sim-player");
  const playerHeading = document.getElementById("player-heading");
  const playButton = document.getElementById("player-play");
  const frameSlider = document.getElementById("player-frame");
  const frameLabel = document.getElementById("player-label");
  const body = document.querySelector("#sim-table tbody");
  const stop = document.getElementById("sim-stop");
  const command = document.getElementById("sim-command");
  const plotHeading = document.getElementById("plot-heading");
  const canvas = document.getElementById("sim-plot");

  const MAX_STEPS = 100;  // the server refuses more
  const FRAME_MS = 1400;
  const MARGIN = { left: 92, right: 16, top: 12, bottom: 40 };  // left: room for 1,048,576

  // runs: [value, label] pairs, labelled as on the Plots tab ("2 · zipf:16,1").
  for (const [run, label] of runs) simRun.appendChild(new Option(label, run));

  // -- form -------------------------------------------------------------------

  // n: one field, typed into, or picked from the run's n values with the button
  // beside it - all of them, whatever is typed.
  function offerSizes() {
    nValues.textContent = "";
    const values = (sizes || {})[simRun.value] || [];
    for (const n of values) {
      const item = document.createElement("li");
      item.setAttribute("role", "option");
      item.dataset.n = n;
      item.textContent = n.toLocaleString("en-US");
      nValues.appendChild(item);
    }
    if (!values.length) {
      const item = document.createElement("li");
      item.className = "none";
      item.textContent = "no data for this run - type n";
      nValues.appendChild(item);
    }
  }
  simRun.addEventListener("change", offerSizes);

  function openSizes(open) {
    nValues.hidden = !open;
    nOpen.setAttribute("aria-expanded", String(open));
    if (!open) return;
    const typed = nInput.value.replace(/[,\s_]/g, "");
    for (const item of nValues.children) item.classList.toggle("current", item.dataset.n === typed);
  }
  nOpen.addEventListener("click", () => openSizes(nValues.hidden));
  nValues.addEventListener("click", (event) => {
    const item = event.target.closest("li[data-n]");
    if (!item) return;
    nInput.value = item.dataset.n;
    openSizes(false);
    nInput.focus();
  });
  nInput.addEventListener("keydown", (event) => {
    if (event.key === "ArrowDown") {
      event.preventDefault();
      openSizes(true);
    } else if (event.key === "Escape") {
      openSizes(false);
    }
  });
  document.addEventListener("click", (event) => {
    if (!nValues.hidden && !event.target.closest(".combo")) openSizes(false);
  });

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

  // A start or end: a number, or an arithmetic expression in n - numbers, n,
  // + - * /, parentheses, and a number right before n or a parenthesis
  // multiplying it (3n/4, 2(n-1)). Its value for this n, or null when it does
  // not parse. Parsed here, never evaluated as code.
  function evaluate(text, n) {
    const tokens = text.replace(/\s+/g, "").toLowerCase().match(/\d+(\.\d+)?|[n()+\-*/]|./g) || [];
    let at = 0;
    const peek = () => tokens[at];
    function fail() { throw new Error("parse"); }

    function sum() {
      let value = product();
      while (peek() === "+" || peek() === "-") value = tokens[at++] === "+" ? value + product() : value - product();
      return value;
    }
    function product() {
      let value = unary();
      for (;;) {
        if (peek() === "*") { at++; value *= unary(); }
        else if (peek() === "/") { at++; value /= unary(); }
        else if (peek() === "n" || peek() === "(") value *= unary();  // 3n, 2(n-1)
        else return value;
      }
    }
    function unary() {
      if (peek() === "-") { at++; return -unary(); }
      if (peek() === "+") { at++; return unary(); }
      return atom();
    }
    function atom() {
      const token = tokens[at++];
      if (token === undefined) fail();
      if (token === "n") return n;
      if (token === "(") {
        const value = sum();
        if (tokens[at++] !== ")") fail();
        return value;
      }
      if (/^\d/.test(token)) return Number(token);
      return fail();
    }

    try {
      const value = sum();
      return at === tokens.length && Number.isFinite(value) ? value : null;
    } catch (error) {
      return null;
    }
  }

  // The form's n, start, end and steps, or null (with a message) when they do
  // not make a range. Empty fields take the defaults: start = end = n/2 (one
  // prefix) and 1 step.
  function readForm() {
    const text = nInput.value.replace(/[,\s_]/g, "");
    if (!/^\d+$/.test(text) || Number(text) < 1) {
      status.textContent = "n must be a whole number, at least 1.";
      return null;
    }
    const n = Number(text);
    function field(input, fallback, name, low, high) {
      if (input.value === "") return fallback;
      const value = Number(input.value);
      if (!(Number.isInteger(value) && value >= low && value <= high)) {
        status.textContent = name + " must be a whole number from " + low + " to " + high + ".";
        return null;
      }
      return value;
    }
    // start and end: expressions in n, rounded.
    function position(input, fallback, name, low) {
      if (input.value.trim() === "") return fallback;
      const value = evaluate(input.value, n);
      if (value === null) {
        status.textContent = name + ": \"" + input.value + "\" is not a number or an expression in n, like n/3 or 3n/4.";
        return null;
      }
      const rounded = Math.round(value);
      if (rounded < low || rounded > n) {
        status.textContent = name + " = " + input.value + " = " + rounded.toLocaleString("en-US") +
          " for n = " + n.toLocaleString("en-US") + "; it must be from " + low + " to n.";
        return null;
      }
      return rounded;
    }
    const half = Math.max(1, Math.round(n / 2));
    const start = position(startInput, half, "start", 0);
    if (start === null) return null;
    const end = position(endInput, half, "end", 1);
    if (end === null) return null;
    const steps = field(stepsInput, 1, "steps", 1, MAX_STEPS);
    if (steps === null) return null;
    if (end < start) {
      status.textContent = "end must be at least start.";
      return null;
    }
    return { n, start, end, steps, tiebreaker: tiebreakerInput.value };
  }

  form.addEventListener("submit", (event) => {
    event.preventDefault();
    const range = readForm();
    if (range === null) return;
    status.textContent = "";
    load(simRun.value, range);
    player.scrollIntoView({ behavior: "smooth", block: "nearest" });
  });

  // -- loading ----------------------------------------------------------------

  let frames = [];      // {t, sim, segments: {k: segments}} per prefix, sim null until it arrives
  let frameIndex = 0;
  let selected = 0;     // index into the frame's deltas of the delta drawn
  let view = null;      // [lowest key, highest key] shown, or null for 1..n
  let current = null;   // {run, n, tiebreaker}: the simulation shown
  let reader = null;    // the stream still arriving, cancelled when replaced
  let timer = null;

  // Reads the server's stream: a line with the prefix lengths, then a line per
  // prefix as it finishes, in any order.
  async function load(run, { n, start, end, steps, tiebreaker }) {
    pause();
    if (reader) reader.cancel().catch(() => {});
    const mine = current = { run, n, tiebreaker };
    reader = null;
    frames = [];
    frameIndex = 0;
    view = null;
    player.hidden = false;
    playerHeading.textContent = "Optimal segments as the prefix grows - run " + run +
      ", n = " + n.toLocaleString("en-US") + ", " + steps + " steps from " +
      start.toLocaleString("en-US") + " to " + end.toLocaleString("en-US");
    frameLabel.textContent = "Starting…";
    body.textContent = "";
    stop.textContent = "";
    command.textContent = "";
    plotHeading.textContent = "";

    const query = "?run=" + run + "&n=" + n + "&start=" + start + "&end=" + end + "&steps=" + steps +
      "&tiebreaker=" + tiebreaker;
    try {
      const response = await fetch("/api/simulate/" + encodeURIComponent(experiment) + query);
      if (!response.ok) {
        const payload = await response.json().catch(() => ({}));
        throw new Error(payload.error || response.statusText);
      }
      const stream = reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffer = "";
      for (;;) {
        const { value, done } = await stream.read();
        if (current !== mine) {  // replaced by another simulation
          stream.cancel().catch(() => {});
          return;
        }
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        let cut;
        while ((cut = buffer.indexOf("\n")) >= 0) {
          receive(JSON.parse(buffer.slice(0, cut)));
          buffer = buffer.slice(cut + 1);
        }
      }
      if (frames.some((f) => !f.sim && !f.error)) throw new Error("the connection closed early");
    } catch (error) {
      if (current === mine) frameLabel.textContent = "Simulation failed: " + error.message;
    } finally {
      if (current === mine) reader = null;
    }
  }

  function receive(line) {
    if (line.ts) {
      frames = line.ts.map((t) => ({ t, sim: null, error: null, segments: {} }));
      frameSlider.max = frames.length - 1;
      play();
      return;
    }
    const frame = frames.find((f) => f.t === line.t);
    if (!frame) return;
    if (line.error) frame.error = line.error;
    else {
      frame.sim = line.payload;
      frame.segments[frame.sim.best_k] = frame.sim.segments;
    }
    if (frame === frames[frameIndex]) showFrame();
  }

  // -- player -----------------------------------------------------------------

  function pause() {
    clearInterval(timer);
    timer = null;
    playButton.textContent = "Play";
  }

  function play() {
    if (timer || !frames.length) return;
    if (frameIndex === frames.length - 1) frameIndex = 0;  // replay from the start
    playButton.textContent = "Pause";
    showFrame();
    timer = setInterval(() => {
      // Wait on a frame still loading rather than skip it.
      const frame = frames[frameIndex];
      if (!frame || (!frame.sim && !frame.error)) return;
      if (frameIndex === frames.length - 1) return pause();
      frameIndex += 1;
      showFrame();
    }, FRAME_MS);
  }

  playButton.addEventListener("click", () => (timer ? pause() : play()));

  // Stepping, by the buttons or the keys: left and right move through the
  // prefixes (each opens on its best delta), up and down through the current
  // prefix's deltas, a row at a time as the table shows them.
  function stepFrame(by) {
    if (!frames.length) return;
    pause();
    frameIndex = Math.min(frames.length - 1, Math.max(0, frameIndex + by));
    showFrame();
  }

  function stepDelta(by) {
    const frame = shownFrame();
    if (!frame) return;
    const i = Math.min(frame.sim.deltas.length - 1, Math.max(0, selected + by));
    if (i === selected) return;
    pick(i);
    const row = body.rows[i];
    const wrap = body.closest(".table-wrap");
    if (row.offsetTop < wrap.scrollTop + row.offsetHeight ||
        row.offsetTop + row.offsetHeight > wrap.scrollTop + wrap.clientHeight) {
      wrap.scrollTop = row.offsetTop - wrap.clientHeight / 2;
    }
  }

  document.getElementById("player-prev").addEventListener("click", () => stepFrame(-1));
  document.getElementById("player-next").addEventListener("click", () => stepFrame(1));

  const KEYS = { ArrowLeft: () => stepFrame(-1), ArrowRight: () => stepFrame(1),
                 ArrowUp: () => stepDelta(-1), ArrowDown: () => stepDelta(1) };
  document.addEventListener("keydown", (event) => {
    const action = KEYS[event.key];
    if (!action || event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return;
    // Only on the Simulate tab with something loaded, and not while typing in
    // a field or moving a slider or picker, which have their own arrow keys.
    if (player.hidden || player.closest("[hidden]")) return;
    if (event.target.closest("input, select, textarea")) return;
    event.preventDefault();
    action();
  });
  frameSlider.addEventListener("input", () => {
    pause();
    frameIndex = Number(frameSlider.value);
    showFrame();
  });

  // -- one frame: its table and plot -----------------------------------------

  // The best row: the program's own pick, so ties go the run's --tiebreaker way.
  function bestIndex(sim) {
    const i = sim.deltas.findIndex((row) => row.k === sim.best_k);
    return i < 0 ? 0 : i;
  }

  function formatDelta(delta) {
    return delta.toLocaleString("en-US");
  }

  // Draws the current frame from the start: its table, the best delta selected.
  function showFrame() {
    const frame = frames[frameIndex];
    if (!frame) return;
    frameSlider.value = frameIndex;
    const head = "t = " + frame.t.toLocaleString("en-US") + "  (" + (frameIndex + 1) + " of " + frames.length + ")";
    if (!frame.sim) {
      frameLabel.textContent = head + (frame.error ? " - failed: " + frame.error : " - loading…");
      // Not the previous frame's table and plot under this frame's label.
      body.textContent = "";
      stop.textContent = "";
      command.textContent = "";
      plotHeading.textContent = "";
      canvas.getContext("2d").clearRect(0, 0, canvas.width, canvas.height);
      return;
    }
    const sim = frame.sim;
    const best = bestIndex(sim);
    const winner = sim.deltas[best];
    selected = best;
    frameLabel.textContent = head + " - best δ = " + formatDelta(winner.delta) +
      ", λ = " + winner.lambda.toLocaleString("en-US") + ", qc = " + winner.qc.toFixed(3);

    body.textContent = "";
    sim.deltas.forEach((row, i) => {
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
    const next = sim.deltas[sim.deltas.length - 1].delta + 0.5;
    stop.textContent = sim.deltas.length + " values of δ tried; every δ ≥ " + formatDelta(next) +
      " has qc ≥ log₂δ ≥ " + Math.log2(next).toFixed(3) + ", no lower than the best.";
    command.textContent = sim.command;

    markSelected();
    draw();

    // Bring the best row into view in the scrolling table, not the page.
    const wrap = body.closest(".table-wrap");
    wrap.scrollTop = body.rows[best].offsetTop - wrap.clientHeight / 2;
  }

  // Draws another delta of the current frame, fetching its segments the first
  // time. Pauses, so the frame stays while it is looked at.
  function pick(i) {
    pause();
    const frame = frames[frameIndex];
    selected = i;
    markSelected();
    const k = frame.sim.deltas[i].k;
    if (frame.segments[k]) return draw();

    draw();  // the points, until the segments arrive
    const query = "?run=" + current.run + "&n=" + current.n + "&t=" + frame.t + "&k=" + k +
      "&tiebreaker=" + current.tiebreaker;
    fetch("/api/simulate/" + encodeURIComponent(experiment) + query)
      .then((response) => response.json().then((payload) => {
        if (!response.ok) throw new Error(payload.error || response.statusText);
        return payload;
      }))
      .then((payload) => {
        frame.segments[k] = payload.segments;
        if (frame === frames[frameIndex]) {
          markSelected();
          draw();
        }
      })
      .catch((error) => {
        if (frame === frames[frameIndex]) plotHeading.textContent += " - could not load: " + error.message;
      });
  }

  function markSelected() {
    const frame = frames[frameIndex];
    [...body.rows].forEach((tr, i) => tr.classList.toggle("selected", i === selected));
    const row = frame.sim.deltas[selected];
    plotHeading.textContent = "δ = " + formatDelta(row.delta) + ", λ = " +
      row.lambda.toLocaleString("en-US") + " segments" + (frame.segments[row.k] ? "" : " - loading…");
  }

  // -- plot -------------------------------------------------------------------

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


  function shownFrame() {
    const frame = frames[frameIndex];
    return frame && frame.sim ? frame : null;
  }

  // Keys over 1..n unless zoomed, so the prefix is seen filling in.
  function range() {
    return view || [1, current.n];
  }

  function geometry() {
    const frame = shownFrame();
    return geometryFor(canvas, frame.sim.keys, frame.sim.deltas[selected].delta, range(), MARGIN);
  }

  // Hide segments, so the keys alone can be seen.
  let segmentsShown = true;
  const segmentToggle = document.querySelector("#sim-player .segments-toggle");
  segmentToggle.addEventListener("click", () => {
    segmentsShown = !segmentsShown;
    segmentToggle.textContent = segmentsShown ? "Hide segments" : "Show segments";
    segmentToggle.setAttribute("aria-pressed", String(!segmentsShown));
    segmentToggle.classList.toggle("active", !segmentsShown);
    draw();
  });

  function draw() {
    const frame = shownFrame();
    if (!frame || player.hidden) return;
    const row = frame.sim.deltas[selected];
    const segs = segmentsShown ? frame.segments[row.k] || [] : [];
    render(canvas, frame.sim.keys, segs, row.delta, range(), MARGIN, drag);
  }

  // -- zoom: drag a range of keys, double-click for all of 1..n ---------------

  function offsetX(event) {
    const box = canvas.getBoundingClientRect();
    const g = geometry();
    return Math.min(Math.max(event.clientX - box.left, MARGIN.left), MARGIN.left + g.plotWidth);
  }

  canvas.addEventListener("mousedown", (event) => {
    if (!shownFrame()) return;
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

  // Until something has been simulated, opening the tab takes the run and n
  // the Plots tab shows; after that it keeps the inputs as they were.
  return {
    shown() {
      if (!current) prefill();
      draw();
    },
  };
}
