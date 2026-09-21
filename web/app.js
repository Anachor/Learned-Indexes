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

let data = null;

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
  const hash = "#run=" + run + (n === null ? "" : "&n=" + n);
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
    fill(runPicker, data.runs, run);

    const available = choices(run);
    if (available.some(([value]) => value === hash.n)) {
      fill(nPicker, available, hash.n);
    }
    render();
  })
  .catch((error) => message("Could not load " + name + ": " + error.message));
