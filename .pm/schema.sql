-- PM Agent Schema
-- Tasks database for sprint management

CREATE TABLE IF NOT EXISTS tasks (
    sprint TEXT NOT NULL,
    spec TEXT,
    task_num INTEGER NOT NULL,
    title TEXT NOT NULL,
    description TEXT,
    done_when TEXT,
    status TEXT DEFAULT 'pending' CHECK (status IN ('pending', 'red', 'green', 'blocked', 'in_progress', 'done')),
    blocked_reason TEXT,
    type TEXT CHECK (type IN ('database', 'actions', 'frontend', 'infra', 'agent', 'e2e', 'docs', 'core', 'network', 'strategy', 'testing')),
    owner TEXT,
    skills TEXT,
    pattern_audited BOOLEAN DEFAULT 0,
    pattern_audit_notes TEXT,
    skills_updated BOOLEAN DEFAULT 0,
    skills_update_notes TEXT,
    tests_pass BOOLEAN DEFAULT 0,
    testing_posture TEXT CHECK (testing_posture IN ('A', 'B', 'C', 'D', 'F')),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (sprint, task_num)
);

CREATE TABLE IF NOT EXISTS task_dependencies (
    sprint TEXT NOT NULL,
    task_num INTEGER NOT NULL,
    depends_on_sprint TEXT NOT NULL,
    depends_on_task INTEGER NOT NULL,
    PRIMARY KEY (sprint, task_num, depends_on_sprint, depends_on_task),
    FOREIGN KEY (sprint, task_num) REFERENCES tasks(sprint, task_num),
    FOREIGN KEY (depends_on_sprint, depends_on_task) REFERENCES tasks(sprint, task_num)
);

-- View: tasks that are available to work on (pending with all deps satisfied)
CREATE VIEW IF NOT EXISTS available_tasks AS
SELECT t.*
FROM tasks t
WHERE t.status = 'pending'
AND NOT EXISTS (
    SELECT 1 FROM task_dependencies d
    JOIN tasks dep ON dep.sprint = d.depends_on_sprint AND dep.task_num = d.depends_on_task
    WHERE d.sprint = t.sprint AND d.task_num = t.task_num
    AND dep.status NOT IN ('green', 'done')
);

-- View: sprint progress summary
CREATE VIEW IF NOT EXISTS sprint_progress AS
SELECT
    sprint,
    COUNT(*) as total_tasks,
    SUM(CASE WHEN status IN ('green', 'done') THEN 1 ELSE 0 END) as completed,
    SUM(CASE WHEN status = 'blocked' THEN 1 ELSE 0 END) as blocked,
    SUM(CASE WHEN status = 'pending' THEN 1 ELSE 0 END) as pending,
    SUM(CASE WHEN status IN ('red', 'in_progress') THEN 1 ELSE 0 END) as in_progress,
    ROUND(100.0 * SUM(CASE WHEN status IN ('green', 'done') THEN 1 ELSE 0 END) / COUNT(*), 1) as pct_complete
FROM tasks
GROUP BY sprint;

-- View: blocked tasks with reasons
CREATE VIEW IF NOT EXISTS blocked_tasks AS
SELECT sprint, task_num, title, blocked_reason
FROM tasks
WHERE status = 'blocked';
