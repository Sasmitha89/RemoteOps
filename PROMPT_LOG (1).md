# AI Prompt Log - RemoteOps (IT24102290)

**Tool:** Claude (Anthropic), used through claude.ai in one continuous conversation, 3 to 7 Oct 2026.
**Policy:** CLEAR Level 3 (AI collaboration) for Part 1 only. No AI was used in the Lab Assessment or Viva.

## How the code was produced (honest summary)

The AI wrote the first version of `agent_290.c`, `controller_290.c`, `Makefile_290`, the README and the report. I ran, tested and committed them on CentOS, and I am expected to explain every function.

## Interaction log

| # | Day | What I asked | What the AI produced | How I used it / what I changed |
|---|-----|--------------|----------------------|--------------------------------|
| 1 | Day 1(oct3) | Asked where to work (CentOS over SSH) and how to get the ZIP | Setup commands, `scp` and zip instructions, GitHub workflow | Followed the CentOS setup; used `scp`/GitHub for transfer.  |
| 2 | Day 1 | Asked SSH key vs personal access token | Recommended an SSH key; steps to create and test it | Used the SSH key; `ssh -T` succeeded. |
| 3 | Day 1 | Asked for the work split day by day so I can commit daily | Day 1 files, 5-day plan, commit commands | Committed Day 1 files.|
| 4| Day 1 | Got `fatal: pathspec '.gitignore' did not match` | Explained the file did not exist; how to create it | Created `.gitignore` and retried the commit.|
| 5| Day 2 (4 Oct) | Asked for Day 2, which screenshots to take from the code | Two staged files (framing + AUTH; then SYSINFO/LISTPROC/EXEC); screenshot list. It said it **cannot produce screenshots** for me | Pasted each stage, tested on CentOS, took my own screenshots.|
| 6| Day 2 | Showed `AUTHOPS-2290` returning `ERR 003` | Explained the missing space | Retried correctly and kept both screenshots. |
| 7 | Day 2 | Showed a duplicate `git commit` that committed nothing | Explained that nothing was staged the second time | Committed the code on the next attempt. |
| 8 | Day 3 (5 Oct) | Asked for PUT/GET and MONITOR code, push commands and screenshot commands | Two staged files, test commands (`nc`, checksum round trip) |committed in two commits.  |
| 9 | Day 3 | Asked whether the two files met the brief, and whether UDP and the Controller were done | Requirement-by-requirement checklist; explained the Agent sends UDP but the Controller must receive | Used it to plan Day 4. |
| 10 | Day 4 (6 Oct) | Asked for the Controller, evidence commands and commit commands | Two staged Controller files, evidence-command table, commits | Pasted, tested, committed. |
| 11 | Day 5 (7 Oct) | Asked whether everything was pushed | Said it could not see my machine; gave `git status`/`git log` checks | Ran the checks.|
| 12| Day 5 | Asked for this prompt log, the design diary and a Controller summary | Final files, this log, the diary, the summary | Reviewing and editing.|

## Problems found in AI output, and how they were handled

* **Makefile:** the first version built `controller` in the default target before `controller_290.c` existed, so plain `make` would fail. I used `make -f Makefile_290 agent` until Day 4; the later Makefile builds the Controller only when its file exists.
* **Printing one function with `nl | sed`:** the first command did not stop at the end of the function. The AI corrected it to use `grep -n "^static"` and then a line range.
* **Test script bug:** one concurrency test reported six failures. The cause was shell quoting in the AI's test, not the code (`c$i.txt` was never expanded, and the Agent correctly rejected it with `ERR 010`). The AI re-ran it correctly.
* **Report numbering:** the first approach to chapter-based figure numbers printed the wrong text in the PDF. The AI changed it and re-checked the result.
* **Report styles:** the AI's own audit found duplicate Heading styles that Word would have shown as blue 16 pt. It removed the duplicates.
* **Dates and results:** the AI could not see my machine, so every "tests passed" statement from it came from its own runs. My CentOS results are the ones that count. 

## Verification I did myself 
Built both programs with make -f Makefile_290 and ran the Agent and Controller on CentOS. Tested every command in the brief, the error cases, and a file upload and download compared with md5sum. Checked concurrent clients and an abrupt client disconnect, and confirmed the Agent kept running. Evidence is in the Implementation Report

