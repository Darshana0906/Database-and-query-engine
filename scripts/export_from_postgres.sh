#!/usr/bin/env bash
# Export the hostel_entry PostgreSQL tables to CSV files GateDB can LOAD.
# Usage: scripts/export_from_postgres.sh [outdir]      (defaults: db hostel_entry, user postgres, host localhost)
set -euo pipefail
OUT="${1:-data/export}"
DB="${PGDATABASE:-hostel_entry}"; USER_="${PGUSER:-postgres}"; HOST="${PGHOST:-localhost}"
mkdir -p "$OUT"
for t in hostel in_out_time warden student entry_exit_log alert; do
  # -h localhost forces password auth; set PGPASSWORD=postgres to avoid the prompt.
  psql -h "$HOST" -U "$USER_" -d "$DB" -c "SET TIME ZONE 'UTC'" \
       -c "\copy $t TO '$OUT/$t.csv' CSV HEADER"
  echo "exported $t -> $OUT/$t.csv"
done
echo "Now in GateDB:  LOAD student FROM '$OUT/student.csv';  (see scripts/demo.sql for the full list)"
