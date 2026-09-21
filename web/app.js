// Experiment page: pick a run and an n, show the figures for that pair.
// The whole experiment arrives in one response, so switching either picker is
// local work - no further requests.

const name = decodeURIComponent(location.pathname.split("/").filter(Boolean)[1] || "");

const title = document.getElementById("title");
const pickers = document.getElementById("pickers");
const runPicker = document.getElementById("run");
const nPicker = document.getElementById("n");
const csvLink = document.getElementById("csv");
const panel = document.getElementById("figures");
const nHeading = document.getElementById("n-heading");
const runMetaLine = document.getElementById("run-meta");

let data = null;
let tab = "plots";        // the open tab: "plots" or "simulate"
let simulation = null;    // setupSimulate's handle, when the experiment has one

document.title = name + " - results";
title.textContent = name;

// -- url hash, so a selection can be linked and survives a reload -----------

function readHash() {
  const out = {};
  for (const pair of location.hash.replace(/^#/, "").split("&")) {
    if (!pair) continue;
    const [key, value] = pair.split("=");
    out[decodeURIComponent(key)] = decodeURIComponent(value || "");
  }
  return out;
}

function writeHash(run, n) {
  const hash = "#run=" + run + (n === null ? "" : "&n=" + n) + (tab === "plots" ? "" : "&tab=" + tab);
  if (hash !== location.hash) history.replaceState(null, "", hash);
}

// -- pickers ---------------------------------------------------------------

// Each choice is a value, or a [value, label] pair when the text shown differs.
function fill(select, choices, chosen) {
  select.textContent = "";
  for (const choice of choices) {
    const [value, label] = Array.isArray(choice) ? choice : [choice, choice];
    const option = document.createElement("option");
    option.value = value;
    option.textContent = label;
    select.appendChild(option);
  }
  select.value = String(chosen);
  select.disabled = choices.length === 0;
}

function sizes(run) {
  return Object.keys(data.figures[run] || {})
    .map(Number)
    .sort((a, b) => a - b);
}

// The n picker's entries: the figures across n first, when the run has them,
// then each n. Values are strings, as the select and the URL hash hold them.
const OVERALL = "overall";
const OVERALL_LABEL = "Across n";

function overallImages(run) {
  return (data.overall || {})[run] || [];
}

function choices(run) {
  const list = sizes(run).map((n) => [String(n), n.toLocaleString("en-US")]);
  if (overallImages(run).length) list.unshift([OVERALL, OVERALL_LABEL]);
  return list;
}

// -- run metadata ----------------------------------------------------------

function runMeta(run) {
  return (data.meta || {})[run] || null;
}

// "2 · zipf:16,1": the permutation is what tells runs apart. Just the number
// for runs whose meta.json does not name one.
function runChoices() {
  return data.runs.map((run) => {
    const meta = runMeta(run);
    const order = meta && meta.permutation ? meta.permutation : null;
    return [String(run), order ? run + " · " + order : String(run)];
  });
}

function pretty(formula) {
  return formula.replace(/delta/g, "δ").replace(/lambda/g, "λ");
}

// What a --permutation spec means, in words.
function describePermutation(spec) {
  const [kind, args = ""] = spec.split(":");
  const values = args.split(",");
  if (kind === "uniform") return "a uniformly random order of the keys 1..n";
  if (kind === "probing") {
    return "linear probing: pick a random key; if it is already in, take the next free one above it, wrapping from n to 1";
  }
  if (kind === "blocks") {
    return "blocks of " + values[0] + " consecutive keys; the blocks in random order, and the keys within each block in random order";
  }
  if (kind === "zipf") {
    return "the keys split into " + values[0] + " equal regions; each insert picks a region with weight 1/rank^" +
      values[1] + " (hot regions placed at random), then a random key in it - hot regions fill early, cold ones late";
  }
  return "";
}

// "2026-09-21 12:08:48 UTC", and how long the run took when it finished.
function runTime(meta) {
  if (!meta.started) return null;
  const start = meta.started.replace("T", " ").replace("Z", " UTC");
  if (!meta.finished) return start;
  const seconds = (Date.parse(meta.finished) - Date.parse(meta.started)) / 1000;
  const took = seconds < 60 ? Math.round(seconds) + " s"
    : Math.floor(seconds / 60) + " min " + String(Math.round(seconds % 60)).padStart(2, "0") + " s";
  return start + "  (took " + took + ")";
}

// The run's metadata as a label / value list under the pickers, with a warning
// first when the run did not finish - its CSVs may then be missing or cut short.
function renderRunMeta(run) {
  const meta = runMeta(run);
  runMetaLine.textContent = "";
  runMetaLine.classList.remove("incomplete");
  if (!meta) {
    runMetaLine.textContent = "No meta.json for this run.";
    return;
  }

  function row(label, ...content) {
    const term = document.createElement("dt");
    term.textContent = label;
    const value = document.createElement("dd");
    for (const piece of content) {
      if (piece === null || piece === undefined || piece === "") continue;
      value.append(piece);
    }
    runMetaLine.append(term, value);
    return value;
  }

  function code(text) {
    const element = document.createElement("code");
    element.textContent = text;
    return element;
  }

  function aside(text) {
    const element = document.createElement("span");
    element.className = "aside";
    element.textContent = text;
    return element;
  }

  if (meta.status && meta.status !== "complete") {
    runMetaLine.classList.add("incomplete");
    row("status", "run " + meta.status + ", not complete - its results may be partial");
  }
  if (meta.seed !== undefined && meta.seed !== null) {
    row("seed", code(String(meta.seed)), aside("each n uses seed + n"));
  }
  if (meta.permutation) {
    row("permutation", code(meta.permutation), aside(describePermutation(meta.permutation)));
  }
  if (meta.cost) row("cost", pretty(meta.cost));
  const time = runTime(meta);
  if (time) row("time", time);
  if (meta.commit) {
    row("commit", code(meta.commit),
        meta.dirty === true ? aside("plus uncommitted changes") : null,
        meta.dirty === null && meta.commit !== "unknown" ? aside("uncommitted changes unknown") : null);
  }

  const link = document.createElement("a");
  link.href = "/results/" + encodeURIComponent(name) + "/" + run + "/meta.json";
  link.textContent = "meta.json";
  row("file", link);
}

function message(text, command) {
  panel.textContent = "";
  const box = document.createElement("p");
  box.className = "empty";
  box.textContent = text;
  panel.appendChild(box);
  if (command) {
    const pre = document.createElement("pre");
    pre.textContent = command;
    panel.appendChild(pre);
  }
}

// -- rendering -------------------------------------------------------------

function figureElement(image) {
  const figure = document.createElement("figure");

  const caption = document.createElement("figcaption");
  caption.textContent = image.kind.replace(/_/g, " ");
  figure.appendChild(caption);

  const img = document.createElement("img");
  img.src = image.url;
  img.alt = image.file;
  img.loading = "lazy";
  figure.appendChild(img);

  return figure;
}

function render() {
  const run = runPicker.value;
  const available = choices(run);
  renderRunMeta(run);

  if (!available.length) {
    nPicker.textContent = "";
    nPicker.disabled = true;
    csvLink.hidden = true;
    nHeading.hidden = true;
    writeHash(run, null);
    message(
      "No figures for run " + run + ". Generate them with:",
      "python3 experiments/" + name + "/plot_" + name + ".py --run " + run
    );
    return;
  }

  // Keep the current choice when moving between runs that both have it, so
  // stepping through runs at a fixed n does not reset the selection.
  const values = available.map(([value]) => value);
  const n = values.includes(nPicker.value) ? nPicker.value : values[0];
  fill(nPicker, available, n);
  writeHash(run, n);

  const across = n === OVERALL;
  nHeading.textContent = across ? OVERALL_LABEL : "n = " + Number(n).toLocaleString("en-US");
  nHeading.hidden = false;

  const images = across ? overallImages(run) : data.figures[run][n] || [];
  panel.textContent = "";
  for (const image of images) panel.appendChild(figureElement(image));

  const table = across ? null : (data.tables[run] || {})[n];
  if (table) {
    csvLink.href = table.url;
    csvLink.textContent = "download " + table.file;
    csvLink.hidden = false;
  } else {
    csvLink.hidden = true;
  }
}

// -- tabs ------------------------------------------------------------------

const tabs = document.getElementById("tabs");

function showTab(which) {
  tab = which;
  for (const button of tabs.querySelectorAll("[data-tab]")) {
    const active = button.dataset.tab === which;
    button.classList.toggle("active", active);
    button.setAttribute("aria-selected", String(active));
    document.getElementById("tab-" + button.dataset.tab).hidden = !active;
  }
  writeHash(runPicker.value, nPicker.value || null);
  if (which === "simulate") simulation.shown();
}

for (const button of tabs.querySelectorAll("[data-tab]")) {
  button.addEventListener("click", () => showTab(button.dataset.tab));
}

runPicker.addEventListener("change", render);
nPicker.addEventListener("change", render);

// -- load ------------------------------------------------------------------

fetch("/api/experiments/" + encodeURIComponent(name))
  .then((response) => {
    if (!response.ok) throw new Error("no such experiment");
    return response.json();
  })
  .then((payload) => {
    data = payload;

    // The server knows the display name (exp1 -> "Experiment 1"); the URL
    // only carries the directory name.
    const label = data.label || name;
    document.title = label + " - results";
    title.textContent = label;

    if (!data.runs.length) {
      message("No runs for " + label + " yet.");
      return;
    }

    const hash = readHash();
    const run = data.runs.includes(Number(hash.run)) ? Number(hash.run) : data.runs[0];

    pickers.hidden = false;
    fill(runPicker, runChoices(), run);
    // The tab bar only when there is a second tab to switch to.
    if (data.simulate) {
      // Each run's n values, from its figures and CSVs, for the Simulate n box.
      const runSizes = {};
      for (const r of data.runs) {
        const found = new Set([...Object.keys(data.figures[r] || {}), ...Object.keys(data.tables[r] || {})]);
        runSizes[r] = [...found].map(Number).sort((a, b) => a - b);
      }
      simulation = setupSimulate(name, runChoices(), runSizes);
      tabs.querySelector('[data-tab="simulate"]').hidden = false;
      tabs.hidden = false;
    }

    const available = choices(run);
    if (available.some(([value]) => value === hash.n)) {
      fill(nPicker, available, hash.n);
    }
    render();
    if (hash.tab === "simulate" && simulation) showTab("simulate");
  })
  .catch((error) => message("Could not load " + name + ": " + error.message));
