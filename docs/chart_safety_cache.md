# Chart-safety cache

Weather Routing owns the authoritative cache of chart-classified safety
tiles. OpenCPN supplies exact chart classifications through an optional,
dynamically detected host capability; the plugin owns the RAM policy, durable
format, invalidation and recovery.

## Host compatibility

- On an enhanced OpenCPN host, Weather Routing registers its cache callbacks
  and uses the highest-detail applicable chart selected by the host safety
  service.
- On stock OpenCPN, the same plugin binary loads without unresolved enhanced
  API symbols and retains standard GSHHS land checking.
- If enhanced chart safety is explicitly enforced but the capability is
  absent, routing fails closed. It never silently substitutes GSHHS for an
  enforced chart-safety request.
- On a new enhanced-host installation, chart safety and enforcement default
  on. Existing explicit user choices remain unchanged.

## RAM and disk policy

The hot cache is an LRU with a user-selectable budget from 256 to 8192 MiB.
`Auto` uses one thirty-second of physical memory, rounded to 256 MiB and
bounded to 256–2048 MiB. Increasing this budget reduces repeated SSD reads
without changing any safety result.

Persistence defaults on. The disk cache is append-only during routing:
changed tiles are written in batches, not by rewriting the complete cache.
Records are checksummed and a partial final record is discarded after an
unclean shutdown. Compaction is deferred until shutdown and only considered
after the file exceeds 1 GiB.

The cache identity includes the host chart-set identity. Any chart database,
chart file metadata, chart group or safety-grid format change invalidates
stored tiles rather than risking a stale safety answer. Cached payloads retain
the exact hazard flags, depth availability and minimum depths produced by the
host.

## Long-passage prewarm

Prewarm geometry is a performance hint only. It never bounds the weather
solver, certifies unexamined water, or changes the fail-closed authoritative
segment checks made as a route expands.

For an ocean passage of at least 600 NM, fallback prewarm uses five narrow
route-shaped corridors: the great-circle centreline plus symmetric inner and
outer bowed alternatives. The outer cross-track displacement is 15% of
passage length, bounded to 90–900 NM, and the inner alternatives are halfway
between it and the centreline. Each line has only a 6–12 NM raster margin.
Consequently a passage thousands of miles long can prefetch plausible
synoptic-scale diversions without filling a huge rectangular raster or
turning the corridor into a route-quality assumption. Scout-derived prewarm
is preferred when the preliminary isochrones expose a more relevant search
shape; any other solver excursion is populated safely on demand.

The Chart Awareness settings page shows capability state, effective RAM,
cache counts and I/O totals, and provides an explicit clear action. Headless
test runs log the same counters as `WR_PLUGIN_CHART_CACHE`.

The same RAM budget is also available in the Weather Routing configuration
dialog's **Advanced** tab, beside the departure-time worker control. Changes
take effect immediately and are saved for subsequent OpenCPN launches. The RAM
budget affects only the plugin's hot tile cache; certified tile persistence on
SSD remains enabled independently.
