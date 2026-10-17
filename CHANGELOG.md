# 2026-10-17 SLL quantity modeling and QP optimization

- Publish the validated SLL quantity implementation on the existing ordinary Pro / SRL-compatible two-round baseline, preserving defaults and phase budgets.
- Include discrete crossing counts, SoftFloor/WASLL analytic gradients, per-net OpenMP evaluation, local QP terms, transactional candidate checks and stage diagnostics.
- Preserve optional region-policy controls and frozen A/C/D configurations; document a fresh-directory run workflow.
- Include GP validation, later completed Vivado backend results and source provenance. Spatial hotspots and controllable physical SLL locations remain future work.
- Organize existing implementation without changing the validated algorithm or rerunning full GETRF routing for this publication.

# 2026-10-12 AMF3Pro SRL compatibility branch

- Publish frozen independent max-two-round SLR implementation as primary code.
- Preserve the exact merged-stage implementation as a source overlay and configuration variant.
- Permit compatible independent SRL PUs to share SLICEM sites, up to four SRL primitives.
- Restore coupled MCLB/CARRY legalization; default weak attraction on and primitive timing exceptions off.
- Include Pro changes, fine-grained timing, input/source provenance and completed four-arm experiment summaries.
- Resolve/hash SRL control metadata in full-run and provide portable host/input configuration.
- No new placement algorithm changes or multicore optimization in this publication.

# Changelog

## 3.0.0-dev — 2026-09-25

- 建立 AMFplacer3.0 项目管理结构，保留 AMF 2.0 上游历史及现有源码修改。
- 登记已完成的 faceDetect 全量基线和两次失败尝试。
- 提供独立构建、运行、状态查询和轻量报告同步入口。
- 明确服务器保存最终 DCP、Git 管理代码和轻量元数据的边界。
- 增量编译与多芯粒功能尚未实现。
