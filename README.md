# Query engine in C for the hostel entry system

**Course:** Advanced Data Structures (ADS)  **Language:** C11, standard library only
**Baseline:** the *Digital Hostel Entry System* (PostgreSQL schema + Node/React app).

GateDB is a small in-memory database engine. It reads the hostel system's tables (exported from PostgreSQL as CSV),
parses a SQL subset, picks an access path with a rule-based optimizer, and executes queries using data structures
implemented from scratch: **B+ tree, hash index, trie, interval tree (augmented treap) and binary heap**.
PostgreSQL stays the reference database; GateDB re-implements the part that answers the hostel queries and lets you
*measure* what each data structure buys you.

```
 PostgreSQL (hostel_entry)                         HostelDB (this project, pure C)
 ┌──────────────────────┐   \copy ... CSV    ┌──────────────────────────────────────────────┐
 │ hostel, student,     │ ─────────────────► │ CSV loader ─► row store ─► indexes           │
 │ entry_exit_log, ...  │                    │ SQL text ─► lexer ─► parser ─► planner ─►    │
 └──────────────────────┘                    │ executor (scan / index / join / agg / sort)  │
                                             └──────────────────────────────────────────────┘
```

## Quick start

```bash
make            # builds build/gatedb (shell) and build/gen (data generator)
make demo       # loads the 5-student demo data and walks through every index type
make test       # unit tests for each structure + index-vs-scan equivalence tests
make asan       # same tests under AddressSanitizer + UBSan
make bench      # 50k students / ~350k log rows; prints the speedup table
./build/gatedb  # interactive shell:  HELP;
```

Needs only `gcc` (or any C11 compiler) and `make`. No libraries.

### Loading your real PostgreSQL data

```bash
PGPASSWORD=postgres scripts/export_from_postgres.sh data/export   # \copy each table to CSV (UTC)
./build/gatedb
gatedb> LOAD student FROM 'data/export/student.csv';
```

The loader handles quoted fields, embedded commas/newlines, `""` escapes, CRLF, empty = NULL, PostgreSQL
`timestamptz` text (`2026-10-27 18:30:00+05:30` is converted to UTC) and array text (`{9000000001}` is kept as text).
Columns that GateDB's schema does not model (passwords, coordinates...) are ignored. Timestamps print in UTC.

## What each feature of the hostel system maps to

| Hostel-system question | GateDB query | Data structure doing the work |
|---|---|---|
| Look up a student by MIS (login, profile) | `WHERE mis = '612415096'` | **Hash index** |
| Warden's search box: names starting with "Sh" | `WHERE name LIKE 'sh%'` | **Trie** (case-insensitive prefix walk) |
| Logs between two times (report / CSV export) | `WHERE exit_time >= .. AND exit_time < ..` | **B+ tree** range scan along the leaf chain |
| Who was outside at 3 PM? (audit) | `WHERE OVERLAP(exit_time, entry_time, '..', '..')` | **Interval tree** (open trips have a NULL end = +inf) |
| Who is outside right now? (dashboard) | `WHERE status = 'OUT'` + join to `student` | **Hash index**, **index nested-loop join** |
| Most recent N exits | `ORDER BY exit_time DESC LIMIT N` | **Binary heap** (top-K in O(n log k)) |
| Exits per student / per hostel | `GROUP BY ... COUNT(*)` | **Hash table** aggregation |

## SQL supported

```
LOAD <table> FROM 'file.csv';
CREATE INDEX [name] ON t (col) [USING BTREE|HASH|TRIE];
CREATE INDEX [name] ON t (start_col, end_col) USING INTERVAL;
DROP INDEX name;   SHOW TABLES;   SHOW INDEXES;   DESCRIBE t;
INSERT INTO t VALUES (...);                      -- every index is updated incrementally
[EXPLAIN] SELECT * | col, ... | COUNT(*) FROM t [alias]
    [JOIN t2 [alias] ON a.x = b.y]               -- inner equi-join
    [WHERE cond AND cond ...]                    -- = != < <= > >=, LIKE 'abc%', IS [NOT] NULL, OVERLAP(...)
    [GROUP BY col] [ORDER BY col|COUNT(*) [ASC|DESC]] [LIMIT n];
```

## The data structures

| Structure | File | Used for | Insert | Lookup | Notes |
|---|---|---|---|---|---|
| **B+ tree** | `src/bptree.c` | `INT`/`TIMESTAMP` equality and range | O(log n) | O(log n + k) | Order 32; entries are the composite `(key, rid)` so duplicate keys are totally ordered; leaves are chained for range scans; `bpt_check` verifies all invariants |
| **Hash index** | `src/hashidx.c` | equality on any column, hash join, GROUP BY | O(1) avg | O(1) avg | Separate chaining, one node per distinct key holding a row-id list; doubles and rehashes at load factor 0.75; FNV-1a |
| **Trie** | `src/trie.c` | `LIKE 'prefix%'` | O(L) | O(L + k) | First-child / next-sibling nodes (compact for sparse alphabets); case-insensitive |
| **Interval tree** | `src/itree.c` | `OVERLAP(start, end, a, b)` | O(log n) exp. | O(log n + k) | Treap keyed on interval start, each node storing the max end of its subtree; prunes whole subtrees |
| **Binary heap** | `src/heap.c` | `ORDER BY .. LIMIT k` | O(log k) | n/a | Bounded max-heap keeps the k best of n rows: O(n log k) time, O(k) memory vs O(n log n) full sort |

*L = key length, k = number of results.*

## How a query runs

1. **Lexer + recursive-descent parser** (`src/sql.c`) produce a `Stmt` (an AST-like struct).
2. **Binder** resolves column names (with aliases), checks types, converts literals (timestamps become Unix seconds).
3. **Rule-based optimizer** (`choose_access` in `src/engine.c`), in priority order:
   1. `=` on a column with a **hash** index (or B+ tree for numeric columns) -> point lookup
   2. `LIKE 'abc%'` on a **trie** column -> prefix walk
   3. `OVERLAP(a, b, ..)` with an **interval** index -> stabbing query
   4. comparisons on a **B+ tree** column -> range scan
   5. otherwise **full scan**.
   Joins use an **index nested-loop** when the right table has a usable index, else a run-time **hash join**
   (build a hash table on the filtered right side, probe with the left).
4. **Executor** fetches candidate row ids, *re-checks every predicate on the real row* (so an index bound only has
   to be a superset, which keeps the optimizer simple and safe), then aggregates, sorts (top-K heap or `qsort`) and limits.
   Queries with `LIMIT` and no `ORDER BY` stop scanning early.
5. `EXPLAIN` prints the chosen plan instead of running it.

```
gatedb> EXPLAIN SELECT s.name FROM entry_exit_log l JOIN student s ON s.mis = l.student_mis WHERE l.status = 'OUT';
QUERY PLAN
  Scan  HASH LOOKUP entry_exit_log.status = 'OUT'  using idx_log_status
        filter: 1 condition(s) re-checked on fetched rows
  Probe student via index per left row
  Join  INDEX NESTED LOOP  entry_exit_log.student_mis = student.mis  probing hash index idx_student_mis
```

## Project layout

```
include/ src/      library: common, bptree, hashidx, trie, itree, heap, table, sql, engine; src/main.c = shell
tests/             test_structs.c, test_engine.c
tools/gen.c        synthetic data generator (same CSV layout as PostgreSQL \copy)
bench/bench.c      index vs no-index benchmark
scripts/           demo.sql, export_from_postgres.sh
data/demo/         6 CSVs modelled on the app's seed data (5 students; phone numbers are placeholders)
```

