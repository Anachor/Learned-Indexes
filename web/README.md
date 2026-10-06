# Results server

Browses the figures already in `figures/`: the homepage lists the experiments, and
each experiment has a page with a run picker and an n picker showing that run's
figures, plus a link to the matching CSV. Standard library only, no dependencies.

```
python3 web/serve.py [--port N] [--root DIR]
```

Then open `http://localhost:8000/`. The selection is kept in the URL
(`/exp/exp1#run=1&n=1024`, plus `&tab=simulate` on the Simulate tab), so a particular
view can be linked.

It generates nothing - run the [plot script](../experiments/README.md#plot-modes) first, and if a run has no
figures the page says which command to run. The one exception is the **Simulate** tab, shown for
experiments whose program has `--simulate` ([exp1](../experiments/exp1/README.md#simulate)) once it is built.
Pick a run, n, start, end and a number of steps - start to end in that many equal steps, both
included (empty: start = end = n/2 and 1 step, the single prefix t = n/2; start =
end is always one prefix, and t = 0 is skipped): the
server runs `--simulate` on each of those prefix lengths with the run's seed and
permutation (from its `meta.json`, or the seed from its CSVs for runs without one)
and streams them back as they finish. The player steps through them, showing each
prefix's table of deltas tried, best marked, beside the plot of its keys and
segments; click a row to draw that delta, drag to zoom. Nothing runs until
**Simulate** is pressed. To keep the machine safe the server takes at most 100
steps per simulation and runs at most 4 simulations at once across all requests.
The tiebreaker defaults to `minlambda`, as the experiments now do, whatever the run
used (the runs so far used `mindelta`), so on a tied prefix the best delta can differ
from those runs' figures; qc does not. Pick min δ to match them.
The figures are on the **Plots** tab. Figures are re-read on every request, so
re-plotting and reloading the page is enough to see new output.

## As a service

To keep it running as a systemd user service (restarts on failure, keeps running
after logout, starts at boot):

```
web/serve.sh --install [--port N]   # write the unit, enable and start it (default port 8289)
web/serve.sh --up | --down | --restart | --status
```

Re-run `--install` to change the port, or after moving the repo - it writes the
unit with this checkout's path. `--restart` after editing `serve.py`; new figures
and frontend edits only need a browser reload. `journalctl --user -u results-server -f`
follows its log.
