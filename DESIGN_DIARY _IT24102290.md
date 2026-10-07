# Design Diary - RemoteOps (IT24102290)

**Day 1 - Sat 3 Oct: foundations.**
Worked out my personalised values from IT24102290: port 9410 (7000 + 2410), SID 0922, token OPS-2290, log `remoteops_IT24102290.log`, storage `./agentfiles/IT24102290/`. I keep the SID as a **string** so the leading zero is never lost. Chose **one pthread per client**: the simplest model that meets the 5+ clients requirement, at the cost of a mutex around the log file. Ignored `SIGPIPE` so writing to a dead client cannot kill the Agent, and used `SO_REUSEADDR` so I can restart quickly. Set up CentOS over SSH and GitHub over an SSH key.
*Obstacles:* typo `git ini`; `git add` failed because `.gitignore` did not exist yet; git created `master` but GitHub used `main` . 

**Day 2 - Sun 4 Oct: framing, AUTH and system commands.**
TCP is a byte stream, so each client has a receive buffer and `read_line()` extracts one `\n`-terminated line (handles partial and merged lines). One `reply()` function appends ` SID:0922`, so no response can leave the tag out. AUTH must come first; three failures drop the connection; the token is never logged. SYSINFO reads `/proc` (load average, MemTotal - MemAvailable, uptime). LISTPROC uses `popen("ps ...")`. EXEC uses a **constant table**, so client text never reaches the shell.
*Obstacle:* I typed `AUTHOPS-2290` without the space and got `ERR 003`; the protocol needs `AUTH <09410>`. 

**Day 3 - Mon 5 Oct: files and monitoring.**
PUT/GET count bytes exactly: `recv_exact()` uses bytes already in the buffer, then loops on `recv()`. Uploads go to a temporary file and are `rename()`d when complete; filenames are restricted, which blocks `../`. An oversize PUT gets `ERR 004` and the connection is closed, because the stream cannot be cheaply resynchronised. MONITOR uses one extra thread per session sending a UDP datagram every 2 s; it waits on a condition variable so STOP takes effect at once, and `stop_monitor()` also runs on QUIT and on disconnect. Killing a client mid-stream leaves the Agent running and logging. 

**Day 4 - Tue 6 Oct: Controller.**
Sends AUTH automatically, then offers an interactive prompt. Reuses the Agent's framing idea (64 KB buffer because LISTPROC replies are long). Binds the UDP socket **before** sending MONITOR START so no datagram is missed. Checks the 10 MB limit locally and checks every reply for the SID tag. Prints throughput (bytes/second) for PUT and GET, which is my optional extension. 

**Day 5 - Wed 7 Oct:** README test results, screenshots, Implementation Report, reflection, ZIP, CourseWeb report upload. 
