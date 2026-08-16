# FireflyOS Current Handoff Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the stale multi-worktree handoff with a concise, fact-checked current-main development handoff.

**Architecture:** The target remains one self-contained Markdown file. It describes current facts first, routes historical implementation ideas to `knowledge/`, and ends with a copyable prompt for the next development conversation.

**Tech Stack:** UTF-8 Markdown, Git read-only inspection, PowerShell path validation.

---

## File map

- Modify: `docs/项目介绍-对话衔接版.md` — the only product-facing handoff document changed by this plan.
- Reference: `knowledge/README.md`, `knowledge/00-源码清单与取回方法.md`, `knowledge/14-主程序集成与后续复用顺序.md`.
- Reference: `docs/FireflyOS系统架构总纲.md`, `docs/Arduino-IDE配置指南.md`, `docs/模块说明/01-UI-Shell.md`.

The existing knowledge rewrite, deleted files and archive deletion are outside this plan. No staging, commit, merge, push, branch deletion or worktree creation is authorized.

### Task 1: Freeze current repository facts

**Files:**
- Read: `docs/项目介绍-对话衔接版.md`
- Read: `Firefly/partitions.csv`
- Read: `libraries/FireflyOS/src/firefly/`
- Read: `knowledge/README.md`

- [ ] **Step 1: Confirm Git and remote state**

Run:

```powershell
git status --short --branch --untracked-files=all
git log -1 --oneline --decorate
git ls-remote --heads origin
git worktree list --porcelain
```

Expected facts: current branch and remote main are `fab5509`; the remote exposes only `main`; only the root worktree is registered; the previous commit/push request did not create a new commit.

- [ ] **Step 2: Confirm current source scope**

Run:

```powershell
rg --files Firefly libraries/FireflyOS/src/firefly tests tools
Test-Path -LiteralPath AndroidCompanion
```

Expected facts: core, HAL and UI Shell sources are present; no `AndroidCompanion` directory exists; plan 3～6 service source is not current-main code.

- [ ] **Step 3: Confirm partition and build facts**

Read `Firefly/partitions.csv`, `Firefly/build_opt.h` and `docs/Arduino-IDE配置指南.md`. Record dual `0x480000` OTA slots, FFat, coredump, Arduino core 0/event core 0, and the repository-root sketchbook requirement. State explicitly that partition support does not equal an OTA service.

### Task 2: Rewrite the handoff document

**Files:**
- Modify: `docs/项目介绍-对话衔接版.md`

- [ ] **Step 1: Replace obsolete history with nine current sections**

Use these headings in order:

```markdown
# FireflyOS 项目介绍与当前开发衔接
## 1. 项目定位
## 2. 硬件与构建环境
## 3. 当前 Git 基线
## 4. 当前仓库结构
## 5. 当前 main 已有能力
## 6. 历史模块与知识库
## 7. 不可破坏的约束
## 8. 当前工作区与已知问题
## 9. 推荐下一步与新对话提示
```

- [ ] **Step 2: Describe only current capabilities as implemented**

List current core runtime, I²C/HAL foundation, UI Shell, lock/home/glance, control/notification centers, alarm/settings UI, previews, tests and build tools. Describe BLE, Android, storage/media services, Wi-Fi/weather, transfer and OTA coordination as historical design references, not current features.

- [ ] **Step 3: Preserve the non-negotiable constraints**

Keep LVGL 8.3.11, UI-main-loop ownership, 410×502 rounded safe area, 48px touch targets, preview-first UI, fixed capacities, shared I²C ownership, local-first operation, automatic-versus-hardware evidence separation, and protection of `image/图片生成提示词`.

- [ ] **Step 4: Record the current dirty worktree without absorbing it**

Summarize the knowledge rewrite, its two process documents, the handoff rewrite, and the pre-existing deletions of `docs/真机验证与分支同步流程.md`, `docs/项目介绍.md` and `libraries/TouchLib-main.zip`. State that these changes are uncommitted and must not be restored or committed selectively without reviewing the final diff.

- [ ] **Step 5: End with a minimal copyable startup prompt**

The prompt must direct the next conversation to read the handoff, architecture summary, Arduino guide, UI Shell module note and three knowledge entry files. It must say the first engineering priority is stabilizing UI gestures and the hardware baseline before restoring later modules.

### Task 3: Validate the rewritten handoff

**Files:**
- Verify: `docs/项目介绍-对话衔接版.md`

- [ ] **Step 1: Check length and required headings**

Run:

```powershell
$path = 'docs\项目介绍-对话衔接版.md'
(Get-Content -LiteralPath $path -Encoding UTF8).Count
Select-String -LiteralPath $path -Pattern '^## ' -Encoding UTF8
```

Expected: 120～180 lines and exactly nine level-two headings.

- [ ] **Step 2: Reject obsolete paths and branch claims**

Run:

```powershell
rg -n '\.worktrees[/\\]|origin/codex/|main 51a6136|ahead 15|AndroidCompanion/' docs/项目介绍-对话衔接版.md
```

Expected: no matches. The word `AndroidCompanion` may appear only in a sentence stating that the directory is absent, without a path suffix.

- [ ] **Step 3: Verify every referenced repository path**

Check the startup reading list with `Test-Path -LiteralPath`. Expected: every listed file exists in the current working tree.

- [ ] **Step 4: Run knowledge validation**

Run:

```powershell
python knowledge/validate_knowledge.py
```

Expected: exit code 0, 18 Markdown files and 14 module/integration cards.

### Task 4: Verify scope and preserve user changes

**Files:**
- Verify: `docs/项目介绍-对话衔接版.md`
- Verify: current working tree

- [ ] **Step 1: Check Markdown whitespace**

Run:

```powershell
git diff --check -- docs/项目介绍-对话衔接版.md
```

Expected: exit code 0; Windows line-ending notices are informational.

- [ ] **Step 2: Review the final diff and status**

Run:

```powershell
git diff -- docs/项目介绍-对话衔接版.md
git status --short --untracked-files=all
```

Expected: the handoff is the only additional product document modified by this plan; all pre-existing knowledge changes, process documents and deletion entries remain present and unaltered.
