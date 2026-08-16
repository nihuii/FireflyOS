# FireflyOS Lightweight Knowledge Base Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite `knowledge/` as compact, self-contained design-reference cards for development from `main@fab5509`, without depending on deleted worktrees or remote feature branches.

**Architecture:** Keep one short routing document, one source-navigation document, thirteen independent module cards, and one staged integration guide. Preserve the local legacy commit only as an optional evidence anchor; daily use must work from current repository-relative paths and the text in each card.

**Tech Stack:** UTF-8 Markdown, Python 3 standard library, Git object inspection.

---

## File map

- `knowledge/README.md`: baseline, reading rules, module index, global constraints.
- `knowledge/00-源码清单与取回方法.md`: current source navigation and optional local-object inspection.
- `knowledge/01-*.md` through `knowledge/13-*.md`: compact module design cards.
- `knowledge/14-主程序集成与后续复用顺序.md`: incremental development order from current main.
- `knowledge/知识库设计.md`: compact maintenance contract.
- `knowledge/知识库实施计划.md`: compact update checklist.
- `knowledge/validate_knowledge.py`: structure, links, baseline, forbidden-path and optional-object validation.

Existing user deletions outside these files must remain untouched. No stage, commit, merge, push, branch deletion, or worktree creation is part of this plan.

### Task 1: Rewrite entry and navigation documents

**Files:**
- Modify: `knowledge/README.md`
- Modify: `knowledge/00-源码清单与取回方法.md`
- Modify: `knowledge/知识库设计.md`
- Modify: `knowledge/知识库实施计划.md`

- [ ] **Step 1: Replace README with the current-baseline entry point**

Keep only `main@fab5509`, a five-step reading method, links to modules 00～14, and these global boundaries: LVGL main-loop ownership, 410×502 safe area, 48px targets, fixed capacity, local-first operation, preview-before-UI, and separation of automatic versus hardware evidence.

- [ ] **Step 2: Replace source navigation with two source levels**

Document current paths (`Firefly/`, `libraries/FireflyOS/`, `tests/`, `tools/`, `docs/`) as the default source. Keep `git cat-file -e "e4fb0d1^{commit}"` and `git show "e4fb0d1:<path>"` only as optional local-history inspection commands, explicitly stating that no remote recovery is guaranteed.

- [ ] **Step 3: Compress maintenance documents**

Make `知识库设计.md` define the five-heading card schema and forbidden content. Make `知识库实施计划.md` a short checklist for updating a card when current code adopts an idea.

- [ ] **Step 4: Scan entry documents for obsolete location claims**

Run:

```powershell
rg -n "origin/codex/|\.worktrees[/\\]|D:\\Study\\Projects|已推送.*codex" knowledge/README.md knowledge/00-源码清单与取回方法.md knowledge/知识库设计.md knowledge/知识库实施计划.md
```

Expected: no matches.

### Task 2: Rewrite core and local-device module cards

**Files:**
- Modify: `knowledge/01-核心运行时与事件状态.md`
- Modify: `knowledge/02-UI-Shell与导航覆盖层.md`
- Modify: `knowledge/03-时间闹钟与调度.md`
- Modify: `knowledge/04-电源按键触摸与IMU.md`
- Modify: `knowledge/05-存储文件与SD卡.md`
- Modify: `knowledge/06-音频音乐与录音.md`
- Modify: `knowledge/07-主题包与资源管理.md`

- [ ] **Step 1: Apply the compact card schema**

Each file must contain exactly these reusable sections after its title:

```markdown
## 目标
## 推荐边界
## 最小实现
## 主要风险
## 参考路径
```

- [ ] **Step 2: Preserve only actionable design ideas**

Keep bounded event/state flow, UI main-loop ownership, overlay gesture ownership, RTC/alarm separation, shared-I²C ownership, SD leasing, audio arbitration/WAV finalization, and atomic theme activation. Remove long code excerpts, old test counts, old branch state, and repeated Git history.

- [ ] **Step 3: Make each card independently readable**

Every card must state whether an idea exists on current main or is legacy-only, and list no more than six repository-relative reference paths. A missing current path must be labelled `本地历史对象参考` rather than presented as current code.

### Task 3: Rewrite connectivity, update and recovery module cards

**Files:**
- Modify: `knowledge/08-BLE协议配对与通知.md`
- Modify: `knowledge/09-Android伴侣应用.md`
- Modify: `knowledge/10-WiFi-NTP与天气.md`
- Modify: `knowledge/11-大文件传输.md`
- Modify: `knowledge/12-OTA更新与首启回滚.md`
- Modify: `knowledge/13-诊断恢复出厂与发布闭锁.md`

- [ ] **Step 1: Apply the same five-heading schema**

Use the exact headings from Task 2 so a reader can load any single card without the README.

- [ ] **Step 2: Preserve protocol and safety boundaries**

Keep authenticated framing and ACK ordering, Android explicit permissions, on-demand Wi-Fi, stale-weather policy, temporary-file transfer with checksum and atomic rename, signed dual-source OTA with boot validation, factory-reset allowlists, and Release fail-closed behavior.

- [ ] **Step 3: Mark all non-main implementations correctly**

Android, BLE, Wi-Fi, transfer, OTA, diagnostic and factory-reset implementations absent from current main must be described as design references from the optional local history object, never as available remote branches or current capabilities.

### Task 4: Rewrite the incremental integration guide

**Files:**
- Modify: `knowledge/14-主程序集成与后续复用顺序.md`

- [ ] **Step 1: Establish `main@fab5509` as the only starting point**

Describe the existing UI-shell baseline and its known status-panel return defect without claiming hardware acceptance.

- [ ] **Step 2: Define small development stages**

Use this order: stabilize UI gestures → unify I²C and power → time/core apps → SD/audio → BLE → Wi-Fi/weather → transfer/OTA. Each stage must require its own automatic evidence and hardware record before the next stage.

- [ ] **Step 3: Remove branch-chain recovery instructions**

Do not instruct readers to merge old feature branches. Historical code may only inform a fresh implementation or a manually selected fragment.

### Task 5: Drive validator changes from a failing run

**Files:**
- Modify: `knowledge/validate_knowledge.py`

- [ ] **Step 1: Run the old validator against the new card schema**

Run:

```powershell
python knowledge/validate_knowledge.py
```

Expected: FAIL because the old validator still requires eleven verbose headings and baseline `351dff7`.

- [ ] **Step 2: Replace legacy validation constants**

Set the current baseline to `fab5509`; require the five compact headings; forbid `.worktrees/`, `origin/codex/`, machine-specific absolute paths, private keys, and placeholders in published knowledge files.

- [ ] **Step 3: Make legacy-object validation optional**

If `e4fb0d1^{commit}` exists, print an informational success line. If it does not exist, print a warning while keeping documentation validation successful. Do not require one `git show` command per module.

- [ ] **Step 4: Run the updated validator**

Run:

```powershell
python knowledge/validate_knowledge.py
```

Expected: exit code 0 and a summary naming 18 Markdown files and 14 module/integration cards.

### Task 6: Verify token reduction and scope safety

**Files:**
- Verify: `knowledge/*.md`
- Verify: `knowledge/validate_knowledge.py`

- [ ] **Step 1: Measure Markdown reduction**

Run a read-only size comparison using the pre-edit total recorded in this plan: 111,820 bytes for the 18 knowledge Markdown files. Expected new total: at most 67,092 bytes, a reduction of at least 40%.

- [ ] **Step 2: Scan all knowledge files for stale paths and claims**

Run:

```powershell
rg -n "origin/codex/|\.worktrees[/\\]|D:\\Study\\Projects|当前本地及.*origin/main|已推送.*codex" knowledge
```

Expected: no matches in published Markdown; validator source may contain the forbidden strings only as rules.

- [ ] **Step 3: Review the exact diff scope**

Run:

```powershell
git diff -- knowledge docs/superpowers/specs/2026-08-16-knowledge-base-refresh-design.md docs/superpowers/plans/2026-08-16-knowledge-base-refresh.md
git status --short
```

Expected: only the approved knowledge rewrite and its two process documents are new/modified, alongside the user's pre-existing deletions of `docs/真机验证与分支同步流程.md`, `docs/项目介绍.md`, and `libraries/TouchLib-main.zip`.
