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

function fill(select, values, chosen) {
  select.textContent = "";
  for (const value of values) {
    const option = document.createElement("option");
    option.value = value;
    option.textContent = value;
    select.appendChild(option);
  }
  select.value = chosen;
  select.disabled = values.length === 0;
}

function sizes(run) {
  return Object.keys(data.figures[run] || {})
    .map(Number)
    .sort((a, b) => a - b);
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

function render() {
  const run = runPicker.value;
  const available = sizes(run);

  if (!available.length) {
    nPicker.textContent = "";
    nPicker.disabled = true;
    csvLink.hidden = true;
    writeHash(run, null);
    message(
      "No figures for run " + run + ". Generate them with:",
      "python3 experiments/" + name + "/plot_" + name + ".py --run " + run
    );
    return;
  }

  // Keep the current n when moving between runs that both have it, so stepping
  // through runs at a fixed n does not reset the selection.
  const wanted = Number(nPicker.value);
  const n = available.includes(wanted) ? wanted : available[0];
  fill(nPicker, available, n);
  writeHash(run, n);

  const images = data.figures[run][n] || [];
  panel.textContent = "";
  for (const image of images) {
    const figure = document.createElement("figure");

    const caption = document.createElement("figcaption");
    caption.textContent = image.kind.replace(/_/g, " ");
    figure.appendChild(caption);

    const img = document.createElement("img");
    img.src = image.url;
    img.alt = image.file;
    img.loading = "lazy";
    figure.appendChild(img);

    panel.appendChild(figure);
  }

  const table = (data.tables[run] || {})[n];
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

    if (!data.runs.length) {
      message("No runs for " + name + " yet.");
      return;
    }

    const hash = readHash();
    const run = data.runs.includes(Number(hash.run)) ? Number(hash.run) : data.runs[0];

    pickers.hidden = false;
    fill(runPicker, data.runs, run);

    const available = sizes(run);
    if (available.includes(Number(hash.n))) {
      fill(nPicker, available, Number(hash.n));
    }
    render();
  })
  .catch((error) => message("Could not load " + name + ": " + error.message));
