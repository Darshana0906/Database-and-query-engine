-- GateDB demo: loads the 5-student demo data, builds one index of each kind, and shows plans.
LOAD hostel         FROM 'data/demo/hostel.csv';
LOAD in_out_time    FROM 'data/demo/in_out_time.csv';
LOAD warden         FROM 'data/demo/warden.csv';
LOAD student        FROM 'data/demo/student.csv';
LOAD entry_exit_log FROM 'data/demo/entry_exit_log.csv';
LOAD alert          FROM 'data/demo/alert.csv';
SHOW TABLES;

-- 1. before indexes: everything is a full scan
EXPLAIN SELECT name, room FROM student WHERE mis = '612415096';

CREATE INDEX idx_student_mis  ON student (mis)  USING HASH;
CREATE INDEX idx_student_name ON student (name) USING TRIE;
CREATE INDEX idx_log_exit     ON entry_exit_log (exit_time) USING BTREE;
CREATE INDEX idx_log_span     ON entry_exit_log (exit_time, entry_time) USING INTERVAL;
CREATE INDEX idx_log_student  ON entry_exit_log (student_mis) USING HASH;
CREATE INDEX idx_log_status   ON entry_exit_log (status) USING HASH;
SHOW INDEXES;

-- 2. same query, now an index lookup
EXPLAIN SELECT name, room FROM student WHERE mis = '612415096';
SELECT name, room FROM student WHERE mis = '612415096';

-- 3. name search -> trie prefix walk
SELECT mis, name FROM student WHERE name LIKE 'sh%';

-- 4. who is outside right now (open log) and who was outside at 15:00 on 27 Oct
SELECT s.name, s.room, l.exit_time FROM entry_exit_log l JOIN student s ON s.mis = l.student_mis WHERE l.status = 'OUT';
SELECT s.name FROM entry_exit_log l JOIN student s ON s.mis = l.student_mis
  WHERE OVERLAP(l.exit_time, l.entry_time, '2026-10-27 15:00:00', '2026-10-27 15:00:00');

-- 5. time-range with B+ tree, aggregate, top-K
SELECT COUNT(*) FROM entry_exit_log WHERE exit_time >= '2026-10-27 00:00:00';
SELECT student_mis, COUNT(*) FROM entry_exit_log GROUP BY student_mis ORDER BY COUNT(*) DESC LIMIT 3;
SELECT student_mis, exit_time FROM entry_exit_log ORDER BY exit_time DESC LIMIT 3;
EXPLAIN SELECT student_mis, exit_time FROM entry_exit_log ORDER BY exit_time DESC LIMIT 3;

-- 6. a new admission: all indexes are maintained on INSERT
INSERT INTO student VALUES ('612415200', 'Shreya Naik', 'FY CSE', 'D-401', 'Goa', '2026-08-05', '{9000000009}', '{9100000009}', 1);
SELECT mis, name FROM student WHERE name LIKE 'sh%';
