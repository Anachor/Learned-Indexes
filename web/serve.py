#!/usr/bin/env python3
"""A small webserver for browsing experiment results.

Serves the figures that the per-experiment plot scripts wrote:
  homepage         the experiments found under figures/ and results/
  /exp/<name>      one page per experiment, with a run and n picker

Generates nothing itself - it only shows what is already on disk.

    python3 web/serve.py [--port N] [--root DIR]
"""

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

WEB = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(WEB)

# Two figure layouts are supported, because the plot scripts have used both:
#   nested  figures/<exp>/<run>/n<N>/<kind>.png   (current)
#   flat    figures/<exp>/<run>/<kind>_n<N>.png   (earlier)
# Discovering rather than hardcoding is what lets the server survive the change.
BUCKET = re.compile(r"^n(?P<n>\d+)$")
OVERALL = "overall"  # figures/<exp>/<run>/overall/: the figures across n
FLAT = re.compile(r"^(?P<kind>.+)_n(?P<n>\d+)\.png$")
RUN = re.compile(r"^\d+$")
NUMBERED = re.compile(r"^exp(?P<number>\d+)$")

# Simulation limits: the permutation of n keys is built in full, and the t keys
# of the prefix are sent to the browser to draw.
SIMULATE_MAX_N = 1 << 24
SIMULATE_MAX_T = 1 << 20
SIMULATE_TIMEOUT = 120  # seconds
# A simulation is 1..SIMULATE_MAX_STEPS prefixes, and at most SIMULATE_WORKERS
# simulate processes run at once across all requests - more wait their turn -
# so neither one request nor many at once can swamp the machine.
SIMULATE_MAX_STEPS = 100
SIMULATE_WORKERS = 4
SLOTS = threading.BoundedSemaphore(SIMULATE_WORKERS)

PAGES = {"index": "index.html", "experiment": "experiment.html"}
TYPES = {".html": "text/html; charset=utf-8",
         ".css": "text/css; charset=utf-8",
         ".js": "text/javascript; charset=utf-8",
         ".png": "image/png",
         ".csv": "text/csv; charset=utf-8"}


def subdirectories(path):
    try:
        names = os.listdir(path)
    except OSError:
        return []
    return [name for name in names if os.path.isdir(os.path.join(path, name))]


def experiments(root):
    """The experiment names, from figures/ and results/ together.

    An experiment that has been run but not yet plotted has a results/ directory
    and no figures/ one, and should still be listed.
    """
    names = set(subdirectories(os.path.join(root, "figures")))
    names |= set(subdirectories(os.path.join(root, "results")))
    return sorted(names)


def label(experiment):
    """The display name: exp1 is shown as "Experiment 1", anything else as is.

    Only the display changes - URLs and paths keep the directory name.
    """
    match = NUMBERED.match(experiment)
    return f"Experiment {int(match.group('number'))}" if match else experiment


def runs(root, experiment):
    """The run numbers for an experiment, newest first."""
    numbers = set()
    for top in ("figures", "results"):
        for name in subdirectories(os.path.join(root, top, experiment)):
            if RUN.match(name):
                numbers.add(int(name))
    return sorted(numbers, reverse=True)


def written(path):
    """Sort key: the order the plot script wrote the files in, then the name."""
    try:
        return (os.stat(path).st_mtime, os.path.basename(path))
    except OSError:
        return (0, os.path.basename(path))


def overall(root, experiment, run):
    """The run's figures across n, from figures/<exp>/<run>/overall/, in the
    order they were written."""
    directory = os.path.join(root, "figures", experiment, str(run), OVERALL)
    return [{"kind": os.path.splitext(os.path.basename(path))[0],
             "file": os.path.basename(path),
             "url": f"/figures/{experiment}/{run}/{OVERALL}/{os.path.basename(path)}"}
            for path in sorted(glob.glob(os.path.join(directory, "*.png")), key=written)]


def figures(root, experiment, run):
    """The figures of one run, as {n: [{"kind", "file", "url"}, ...]}.

    Reads both layouts. Within an n the figures keep the order the plot script
    wrote them in, which is the order it means them to be read - the headline
    figure first. Ordering by name instead would lead on whichever kind happens
    to sort first. Falls back to the name when the times are equal, so a fresh
    checkout still gets a stable order.
    """
    directory = os.path.join(root, "figures", experiment, str(run))
    found = {}

    # nested: a directory per n, one figure per kind inside it
    for bucket in sorted(subdirectories(directory)):
        match = BUCKET.match(bucket)
        if not match:
            continue
        n = int(match.group("n"))
        for path in sorted(glob.glob(os.path.join(directory, bucket, "*.png")), key=written):
            name = os.path.basename(path)
            found.setdefault(n, []).append({
                "kind": os.path.splitext(name)[0],
                "file": name,
                "url": f"/figures/{experiment}/{run}/{bucket}/{name}",
            })

    # flat: the n is part of the filename
    for path in sorted(glob.glob(os.path.join(directory, "*.png"))):
        name = os.path.basename(path)
        match = FLAT.match(name)
        if not match:
            continue
        n = int(match.group("n"))
        found.setdefault(n, []).append({
            "kind": match.group("kind"),
            "file": name,
            "url": f"/figures/{experiment}/{run}/{name}",
        })

    return found


def tables(root, experiment, run):
    """The CSVs of one run, as {n: {"file", "url"}}, for the download links."""
    directory = os.path.join(root, "results", experiment, str(run))
    found = {}
    for path in sorted(glob.glob(os.path.join(directory, "*.csv"))):
        name = os.path.basename(path)
        match = re.match(r"^.+_n(\d+)\.csv$", name)
        if not match:
            continue
        found[int(match.group(1))] = {
            "file": name,
            "url": f"/results/{experiment}/{run}/{name}",
        }
    return found


# The experiments whose program has --simulate; the Simulate tab is only shown
# for these.
SIMULATES = {"exp1"}


def binary(root, experiment):
    """The experiment's compiled program, experiments/<exp>/<exp>, or None when
    it is not built or has no --simulate."""
    if experiment not in SIMULATES:
        return None
    path = os.path.join(root, "experiments", experiment, experiment)
    return path if os.path.isfile(path) and os.access(path, os.X_OK) else None


def metadata(root, experiment, run):
    """The run's results/<exp>/<run>/meta.json, or None if it has none or it
    does not parse. Runs from before exp1 wrote one have none."""
    path = os.path.join(root, "results", experiment, str(run), "meta.json")
    try:
        with open(path) as handle:
            meta = json.load(handle)
    except (OSError, ValueError):
        return None
    return meta if isinstance(meta, dict) else None


def base_seed(root, experiment, run):
    """The seed a run was started with, or None.

    From the run's meta.json when it has one. Otherwise from its CSVs: each row
    holds the per-n seed, seed + n, so any one row of any of them gives it back.
    """
    meta = metadata(root, experiment, run)
    if meta and isinstance(meta.get("seed"), int):
        return meta["seed"]
    for path in sorted(glob.glob(os.path.join(root, "results", experiment, str(run), "*.csv"))):
        try:
            with open(path) as handle:
                header = handle.readline().strip().split(",")
                row = handle.readline().strip().split(",")
            return int(row[header.index("seed")]) - int(row[header.index("n")])
        except (OSError, ValueError, IndexError):
            continue
    return None


PERMUTATION = re.compile(r"^(uniform|probing|bitrev(:[0-9.eE+-]+)?|blocks:\d+|zipf:\d+,[0-9.eE+-]+(,\d+)?)$")


def permutation(root, experiment, run):
    """The run's --permutation spec from its meta.json: "uniform" when it names
    none (runs from before the option), None when it names one exp1 no longer
    accepts - simulating that as uniform would show the wrong keys."""
    meta = metadata(root, experiment, run) or {}
    spec = meta.get("permutation")
    if spec is None:
        return "uniform"
    return spec if isinstance(spec, str) and PERMUTATION.match(spec) else None


# The Simulate tab's tiebreaker when the page does not pick one: the fewest
# segments, the experiments' own default. The runs so far used mindelta, so on a
# tie its best delta can differ from their figures - qc never does.
TIEBREAKERS = ("minlambda", "mindelta")
DEFAULT_TIEBREAKER = "minlambda"


def simulate(root, experiment, run, n, t, k=None, tiebreaker=DEFAULT_TIEBREAKER):
    """Runs <exp> --simulate on one prefix: (payload, None) or (None, error).

    Without k: the keys, the table of every delta tried, and the best delta's
    segments. With k: only the segments for delta = k/2, fetched when another
    row of the table is picked.
    """
    program = binary(root, experiment)
    if program is None:
        return None, f"experiments/{experiment}/{experiment} is not built"
    seed = base_seed(root, experiment, run)
    if seed is None:
        return None, f"no meta.json or CSV in results/{experiment}/{run}/ to take the seed from"
    mode = ["--json"] if k is None else ["--segments", str(k)]
    # The run's insertion order, so the simulation rebuilds its keys. Runs
    # without one in their meta.json predate the option and are uniform.
    order = permutation(root, experiment, run)
    if order is None:
        spec = (metadata(root, experiment, run) or {}).get("permutation")
        return None, f"run {run}'s permutation {spec!r} is not one {experiment} can rebuild any more"
    order_flag = ["--permutation", order] if order != "uniform" else []
    order_flag += ["--tiebreaker", tiebreaker]
    tail = order_flag + ["-n", str(n), str(seed)]
    command = [program, "--simulate", str(t)] + mode + tail
    if not SLOTS.acquire(timeout=SIMULATE_TIMEOUT):
        return None, "the server is busy with other simulations - try again shortly"
    try:
        done = subprocess.run(command, capture_output=True, text=True, timeout=SIMULATE_TIMEOUT)
    except subprocess.TimeoutExpired:
        return None, f"simulation took longer than {SIMULATE_TIMEOUT} s"
    finally:
        SLOTS.release()
    if done.returncode != 0:
        return None, (done.stderr.strip().splitlines() or ["simulation failed"])[0]
    payload = json.loads(done.stdout)
    if k is None:
        payload["run"] = run
        # The table-printing form: the same command without --json.
        payload["command"] = " ".join([f"experiments/{experiment}/{experiment}",
                                       "--simulate", str(t)] + tail)
    return payload, None


def prefix_lengths(start, end, steps):
    """The prefixes a simulation plays: t = start + j (end - start) / steps for
    j = 0..steps, rounded - start and end included, so steps + 1 of them, or just
    one when start = end. t = 0, the empty prefix, is left out, and any that
    coincide after rounding are shown once."""
    out = []
    for j in range(steps + 1):
        t = round(start + j * (end - start) / steps)
        if t >= 1 and t not in out:
            out.append(t)
    return out


def summary(root):
    """The homepage payload: every experiment with its runs, the n values it has
    data for, and its figure count.

    The n values are taken from the CSVs as well as the figures, across every
    run, so a run that has been computed but not yet plotted still counts.
    """
    listing = []
    for name in experiments(root):
        numbers = runs(root, name)
        count = 0
        sizes = set()
        for run in numbers:
            found = figures(root, name, run)
            count += sum(len(images) for images in found.values())
            count += len(overall(root, name, run))
            sizes |= set(found) | set(tables(root, name, run))
        listing.append({"name": name, "label": label(name), "runs": numbers,
                        "sizes": sorted(sizes), "figure_count": count})
    return listing


def detail(root, experiment):
    """The experiment payload: every run's figures and CSVs, in one response.

    The whole tree is a few dozen files, so sending it at once keeps switching
    run or n instant with no further requests.
    """
    numbers = runs(root, experiment)
    return {
        "name": experiment,
        "label": label(experiment),
        "runs": numbers,
        "simulate": binary(root, experiment) is not None,
        "meta": {str(run): metadata(root, experiment, run) for run in numbers},
        "figures": {str(run): {str(n): images
                               for n, images in figures(root, experiment, run).items()}
                    for run in numbers},
        "overall": {str(run): overall(root, experiment, run) for run in numbers},
        "tables": {str(run): {str(n): table
                              for n, table in tables(root, experiment, run).items()}
                   for run in numbers},
    }


def safe(root, top, experiment, run, name, bucket=None):
    """Resolve a figure or result file, or None if anything does not check out.

    The path pieces from the URL are never joined blindly: the experiment must be
    one we discovered, the run must be digits, the optional bucket must look like
    n<N> or be overall/, and the file must actually sit in the resulting
    directory. The realpath check at the end is the backstop.
    """
    if experiment not in experiments(root):
        return None
    if not RUN.match(run):
        return None
    if bucket is not None and not (BUCKET.match(bucket) or bucket == OVERALL):
        return None
    if name != os.path.basename(name) or name.startswith("."):
        return None

    pieces = [root, top, experiment, run] + ([bucket] if bucket else [])
    directory = os.path.realpath(os.path.join(*pieces))
    path = os.path.realpath(os.path.join(directory, name))
    if os.path.dirname(path) != directory or not os.path.isfile(path):
        return None
    if os.path.commonpath([path, os.path.realpath(root)]) != os.path.realpath(root):
        return None
    return path


class Handler(BaseHTTPRequestHandler):
    server_version = "results"
    root = ROOT

    def log_message(self, format, *args):
        print(f"{self.address_string()} - {format % args}")

    # -- replies ----------------------------------------------------------

    def send_json(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def send_missing(self, message="not found"):
        self.send_json({"error": message}, status=404)

    def send_file(self, path, attachment=False):
        """Serve a file, streamed, with a no-cache validator.

        Figures are regenerated in place while the server runs, so the ETag is
        derived from mtime and size and revalidated on every request - a reload
        after re-plotting must show the new image, not a cached one.
        """
        try:
            info = os.stat(path)
        except OSError:
            return self.send_missing()

        tag = f'"{int(info.st_mtime)}-{info.st_size}"'
        if self.headers.get("If-None-Match") == tag:
            self.send_response(304)
            self.send_header("ETag", tag)
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            return

        extension = os.path.splitext(path)[1].lower()
        self.send_response(200)
        self.send_header("Content-Type", TYPES.get(extension, "application/octet-stream"))
        self.send_header("Content-Length", str(info.st_size))
        self.send_header("ETag", tag)
        self.send_header("Cache-Control", "no-cache")
        if attachment:
            name = os.path.basename(path)
            self.send_header("Content-Disposition", f'attachment; filename="{name}"')
        self.end_headers()
        if self.command == "HEAD":
            return
        with open(path, "rb") as handle:
            shutil.copyfileobj(handle, self.wfile)

    def send_page(self, page):
        self.send_file(os.path.join(WEB, PAGES[page]))

    # -- routing ----------------------------------------------------------

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        path = self.path.split("?", 1)[0].split("#", 1)[0]
        parts = [part for part in path.split("/") if part]

        if not parts:
            return self.send_page("index")

        if parts[0] == "api":
            query = parse_qs(self.path.split("?", 1)[1]) if "?" in self.path else {}
            return self.api(parts[1:], query)

        if parts[0] == "exp" and len(parts) == 2:
            if parts[1] not in experiments(self.root):
                return self.send_missing(f"no experiment {parts[1]!r}")
            return self.send_page("experiment")

        # /figures/<exp>/<run>/<file>        flat layout
        # /figures/<exp>/<run>/n<N>/<file>   nested layout
        if parts[0] in ("figures", "results") and len(parts) in (4, 5):
            bucket = parts[3] if len(parts) == 5 else None
            target = safe(self.root, parts[0], parts[1], parts[2], parts[-1], bucket)
            if target is None:
                return self.send_missing()
            return self.send_file(target, attachment=parts[0] == "results")

        # Anything else is one of the static frontend files, by name only.
        if len(parts) == 1 and parts[0] in os.listdir(WEB):
            return self.send_file(os.path.join(WEB, parts[0]))

        return self.send_missing()

    def api(self, parts, query):
        if parts == ["experiments"]:
            return self.send_json(summary(self.root))
        if len(parts) == 2 and parts[0] == "experiments":
            if parts[1] not in experiments(self.root):
                return self.send_missing(f"no experiment {parts[1]!r}")
            return self.send_json(detail(self.root, parts[1]))
        # /api/simulate/<exp>?run=R&n=N&start=S&end=E&steps=K: the prefixes played
        # /api/simulate/<exp>?run=R&n=N&t=T&k=K: one prefix's segments for one k
        if len(parts) == 2 and parts[0] == "simulate":
            return self.simulate(parts[1], query)
        return self.send_missing()

    def simulate(self, experiment, query):
        if experiment not in experiments(self.root):
            return self.send_missing(f"no experiment {experiment!r}")
        segments = "k" in query
        keys = ("run", "n", "t", "k") if segments else ("run", "n", "start", "end", "steps")
        tiebreaker = query.get("tiebreaker", [DEFAULT_TIEBREAKER])[0]
        if tiebreaker not in TIEBREAKERS:
            return self.send_json({"error": "tiebreaker must be " + " or ".join(TIEBREAKERS)}, status=400)
        try:
            values = {key: int(query[key][0]) for key in keys}
        except (KeyError, ValueError):
            return self.send_json({"error": ", ".join(keys) + " must be integers"}, status=400)
        run, n = values["run"], values["n"]
        if run not in runs(self.root, experiment):
            return self.send_missing(f"no run {run}")
        if not 1 <= n <= SIMULATE_MAX_N:
            return self.send_json({"error": f"n must be 1 to {SIMULATE_MAX_N}"}, status=400)
        top = min(n, SIMULATE_MAX_T)

        if segments:
            t, k = values["t"], values["k"]
            if not 1 <= t <= top:
                return self.send_json({"error": f"t must be 1 to {top}"}, status=400)
            if not 1 <= k <= 2 * n:
                return self.send_json({"error": f"k must be 1 to {2 * n}"}, status=400)
            payload, error = simulate(self.root, experiment, run, n, t, k, tiebreaker)
            if error:
                return self.send_json({"error": error}, status=500)
            return self.send_json(payload)

        start, end, steps = values["start"], values["end"], values["steps"]
        if not (0 <= start <= end <= top and end >= 1):
            return self.send_json({"error": f"need 0 <= start <= end <= {top}, end at least 1"}, status=400)
        if not 1 <= steps <= SIMULATE_MAX_STEPS:
            return self.send_json({"error": f"steps must be 1 to {SIMULATE_MAX_STEPS}"}, status=400)
        self.stream_simulation(experiment, run, n, prefix_lengths(start, end, steps), tiebreaker)

    def stream_simulation(self, experiment, run, n, ts, tiebreaker):
        """One JSON object per line: first {"ts": [...]}, the prefixes to play,
        then {"t", "payload"} or {"t", "error"} for each as it finishes, so the
        page can show the early ones while the rest compute."""
        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()

        def line(obj):
            self.wfile.write((json.dumps(obj) + "\n").encode())
            self.wfile.flush()

        try:
            line({"ts": ts})
            with ThreadPoolExecutor(max_workers=SIMULATE_WORKERS) as pool:
                futures = {pool.submit(simulate, self.root, experiment, run, n, t, None, tiebreaker): t
                           for t in ts}
                for future in as_completed(futures):
                    payload, error = future.result()
                    line({"t": futures[future], "error": error} if error else
                         {"t": futures[future], "payload": payload})
        except (BrokenPipeError, ConnectionResetError):
            pass  # the page moved on; the running simulations finish and are dropped


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8000, help="port to listen on")
    parser.add_argument("--root", default=ROOT, help="repository root holding figures/ and results/")
    arguments = parser.parse_args()

    root = os.path.realpath(arguments.root)
    Handler.root = root

    # Flushed, so the summary still appears when stdout is a redirected log
    # rather than a terminal - a wrong --root should be obvious either way.
    found = summary(root)
    lines = [f"serving {root}"]
    if found:
        for entry in found:
            numbers = ", ".join(str(run) for run in entry["runs"]) or "none"
            lines.append(f"  {entry['label']}: runs {numbers} ({entry['figure_count']} figures)")
    else:
        lines.append("  no experiments found - is --root correct?")
    lines.append(f"http://localhost:{arguments.port}/")
    print("\n".join(lines), flush=True)

    server = ThreadingHTTPServer(("", arguments.port), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")
        server.server_close()


if __name__ == "__main__":
    main()
