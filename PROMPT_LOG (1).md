# AI Prompt Log - RemoteOps (IT24102290)

**Tool:** Claude (Anthropic), used through claude.ai in one continuous conversation, 3 to 7 Oct 2026.
**Policy:** CLEAR Level 3 (AI collaboration) for Part 1 only. No AI was used in the Lab Assessment or Viva.

> Rows are summarised from the real conversation. **✎ = add your own detail** (what you typed, what you changed, what you rejected). Do not leave a ✎ in the final version. Dates should agree with `git log`.

## How the code was produced (honest summary)

The AI wrote the first version of `agent_290.c`, `controller_290.c`, `Makefile_290`, the README and the report. I ran, tested and committed them on CentOS, and I am expected to explain every function. ✎ *State here, in your own words, which parts you read line by line, which you rewrote, and which you tested yourself.*

## Interaction log

| # | Day | What I asked | What the AI produced | How I used it / what I changed |
|---|-----|--------------|----------------------|--------------------------------|
| 1 | Day 1 (3 Oct) | Uploaded the assignment brief; asked for step-by-step guidance and code segments, with the purpose of each | Explanation of RemoteOps, 8-step roadmap, personalisation formulas, Step 1-2 code (constants, socket, threads, logging) | Used the roadmap as my plan; typed the Step 1-2 code into `agent_290.c`. ✎ |
| 2 | Day 1 | Gave my registration number IT24102290 | Calculated port 9410, SID 0922, token OPS-2290, file names, log and storage path; Step 3 code (framing, AUTH) | Checked the formulas myself against brief section 2.4. ✎ |
| 3 | Day 1 | Asked where to work (CentOS over SSH) and how to get the ZIP | Setup commands, `scp` and zip instructions, GitHub workflow | Followed the CentOS setup; used `scp`/GitHub for transfer. ✎ |
| 4 | Day 1 | Asked for a README to commit | `README.md` with personalised values, protocol table, checklist | Committed it and ticked the checklist as I finished each stage. ✎ |
| 5 | Day 1 | Asked whether to type the setup commands in the SSH prompt; gave my GitHub name and e-mail | Confirmed they run on CentOS; pointed out my `git ini` typo | Ran the corrected commands. |
| 6 | Day 1 | Asked whether `git init` would overwrite my existing GitHub repo | Explained it cannot; renaming the branch to `main`, adding the remote, `git fetch` check | Followed it; `git branch -r` printed nothing, so the GitHub repo was empty. |
| 7 | Day 1 | Asked SSH key vs personal access token | Recommended an SSH key; steps to create and test it | Used the SSH key; `ssh -T` succeeded. |
| 8 | Day 1 | Pasted my Step 1-2 code and asked whether it met all requirements | Said it was incomplete; produced the complete Agent (all commands) and a Makefile; tested it | Used the code but split it into daily stages (row 9). ✎ |
| 9 | Day 1 | Asked for the work split day by day so I can commit daily | Day 1 files, 5-day plan, commit commands | Committed Day 1 files. ✎ |
| 10 | Day 1 | Got `fatal: pathspec '.gitignore' did not match` | Explained the file did not exist; how to create it | Created `.gitignore` and retried the commit. ✎ |
| 11 | Day 2 (4 Oct) | Asked for Day 2 code, and which screenshots to take | Two staged files (framing + AUTH; then SYSINFO/LISTPROC/EXEC); screenshot list. It said it **cannot produce screenshots** for me | Pasted each stage, tested on CentOS, took my own screenshots. ✎ |
| 12 | Day 2 | Asked which Day 2 file to use first | Clarified part 1 first, then part 2 | Followed it. ✎ |
| 13 | Day 2 | Showed `AUTHOPS-2290` returning `ERR 003` | Explained the missing space | Retried correctly and kept both screenshots. |
| 14 | Day 2 | Showed a duplicate `git commit` that committed nothing | Explained that nothing was staged the second time | Committed the code on the next attempt. ✎ |
| 15 | Day 3 (5 Oct) | Asked for PUT/GET and MONITOR code, push commands and screenshot commands | Two staged files, test commands (`nc`, checksum round trip) | Pasted, tested, committed in two commits. ✎ |
| 16 | Day 3 | Asked whether the two files met the brief, and whether UDP and the Controller were done | Requirement-by-requirement checklist; explained the Agent sends UDP but the Controller must receive | Used it to plan Day 4. |
| 17 | Day 3 | Uploaded the report template; asked for a professional 18-20 page report with my Day 1-3 commands | Word report and PDF following the template, with placeholders for my screenshots | Inserting my own screenshots; filling the red fields. ✎ |
| 18 | Day 4 (6 Oct) | Asked for the Controller, evidence commands and commit commands | Two staged Controller files, evidence-command table, commits | Pasted, tested, committed. ✎ |
| 19 | Day 5 (7 Oct) | Asked whether everything was pushed | Said it could not see my machine; gave `git status`/`git log` checks | Ran the checks. ✎ |
| 20 | Day 5 | Asked for the final code, this prompt log, the design diary and a Controller summary | Final files, this log, the diary, the summary | Reviewing and editing. ✎ |

## Problems found in AI output, and how they were handled

* **Makefile:** the first version built `controller` in the default target before `controller_290.c` existed, so plain `make` would fail. I used `make -f Makefile_290 agent` until Day 4; the later Makefile builds the Controller only when its file exists.
* **Printing one function with `nl | sed`:** the first command did not stop at the end of the function. The AI corrected it to use `grep -n "^static"` and then a line range.
* **Test script bug:** one concurrency test reported six failures. The cause was shell quoting in the AI's test, not the code (`c$i.txt` was never expanded, and the Agent correctly rejected it with `ERR 010`). The AI re-ran it correctly.
* **Report numbering:** the first approach to chapter-based figure numbers printed the wrong text in the PDF. The AI changed it and re-checked the result.
* **Report styles:** the AI's own audit found duplicate Heading styles that Word would have shown as blue 16 pt. It removed the duplicates.
* **Dates and results:** the AI could not see my machine, so every "tests passed" statement from it came from its own runs. My CentOS results are the ones that count. ✎

## Verification I did myself ✎

*List what you actually ran, e.g. "built with `make -f Makefile_290`, ran 5 simultaneous Controllers, checked `md5sum` after PUT and GET, killed a client with Ctrl+C and confirmed the Agent kept running". Screenshots are in the Implementation Report.*
