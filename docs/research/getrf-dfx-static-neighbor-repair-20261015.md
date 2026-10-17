# GETRF 20ns DFX：固定接口邻居修复与恢复进展

记录截至2026-10-15 07:51 HKT；最终拼接和性能验收尚未完成。

## 当前有效运行

- A：099 `experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-incremental-099-05-staticanchor18-recovery/workers/a`。
- B：099 `experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-incremental-099-04-metadatafix16/workers/b`；恢复目录的workers/b仅为只读引用。
- 两worker均已completed并通过验收，完整/局部bits齐全，各自2956动态cell全部精确保留LOC/BEL，硬区域违规0、DRC Error/Critical Warning0、setup/hold通过。worker进程均退出，不能把旧PID当当前进程或因16轮顶层失败而重跑。
- 拼接控制器PID1749613于07:23:10开始assembly，实际native Vivado1768258。仅拼接两份routed RM，不重新place/route/AMF；模块位置/布线/接口保持、完整V1时序/DRC/路由、最终pr_verify及bits尚未验收完成。

## 故障、修复和验证

16轮A在完成输入准备后，AMF运行27.824796秒，以SIGABRT退出。最终LUT-FF relocation用CLB packing site取得关键路径邻居的位置，却未处理固定接口cell不参加动态RM打包、没有packing-site对象的情况。这是本DFX集成覆盖遗漏，不是Vivado报错。

新副本仅修改ParallelCLBPacker.cc的该细布局函数：已打包邻居仍用原site坐标；缺少packing site时，只允许经过验证的DFX固定、锁定且LOC保持的静态邻居使用实际site坐标。保留完整STA路径，不制造packing cluster，不移动静态cell，不放松参数或断言其他非法映射。

采用原编译器、原参数、原冻结对象，只重编译该文件并替换其archive成员后链接。所有原冻结源、库和二进制的哈希保持不变。首次编译检查01的const引用错误已保留，修正接口后02通过。新二进制SHA256：

`609ec41f8da157103efa845c81510ae14c00b95979b02ffb82e335570381ef54`

原全量/B二进制为：

`e097c317b7fff66e3ddd1a5291c86d1a49f7b1bfa589f96d181c9be73217df90`

b5322e1f…为基准commit；原dirty冻结源码、1文件覆盖、source.patch与对象/二进制哈希共同追溯实际代码，不能称为新旧同一二进制。

A实际触发固定邻居lookup并成功完成AMF，2689个固定锚点验证保持，2956个动态cell导入全部成功，native remap为0。新恢复driver逐SHA验证15份完整准备产物；周期错误、Pblock错误、仍运行父任务的3个反例被拒绝。恢复复制实测0.423577秒，未重复Vivado prepare或网表导出。

B用新二进制复算同一份完整输入：2956个cell的LOC/BEL精确相同，0移动/重映射，2686个固定锚点保持，固定邻居新分支lookup=0，HPWL仍为68194.400812。复算158.227355秒仅属验证开销，未重跑B后端。此证明限于本B输入，不扩大为所有设计或完全相同构建的证明。

## 已证实耗时

| 项目 | A | B |
|---|---:|---:|
| 原始prepare进程 | 2968.360 s | 2969.461 s |
| 原始接口cone选择 | 640.422 s | 638.733 s |
| 原始scoped网表写出 | 1261.667 s | 1276.946 s |
| 原始scoped输入函数（包含上述子阶段） | 1944.589 s | 1958.244 s |
| 失败AMF | 27.825 s | — |
| 成功AMF进程 | 279.427 s | 127.574 s |
| 输入恢复复制 | 0.424 s | 原结果保留 |
| Vivado兼容placement | 1845.298 s | 1622.951 s |
| 已完成route调用 | 938.839 s | 1094.581 s |
| routed审计 | 234.519 s | 237.692 s |
| pr_verify | 636.151 s | 699.058 s |
| 完整bitstream | 335.073 s | 348.283 s |
| 局部bitstream | 110.360 s | 134.233 s |

AMF进程含加载、初始化和导出，不能称为纯placement算法时间；最终实际placement须用相同profile口径统计。scoped输入函数包含子阶段，不能把表内重叠时间相加。恢复复制不代表冷启动输入准备已从50分钟加速到0.424秒。

Vivado placement实际执行，按用户约定单列为后端兼容处理时间。真实总墙钟仍须包含该步骤；排除后的值明确标注“扣除Vivado placement”。

## B当前布线结果

这是仅B替换、另一个RP仍为V0的完整上下文结果，不是最终两模块V1：

- WNS +1.494 ns，TNS 0，setup违例0。
- WHS +0.006 ns，THS 0，hold违例0。
- WPWS 9.300 ns，pulse违例0。
- route调用1094.581秒（18.24分钟），审计和pr_verify退出0；完整/局部bitstream均已生成。
- 922859个可布线net全部布通，其中919854个为fixed routing；路由错误0，DRC Error/Critical Warning为0。
- 最终2956个动态cell全部精确保留AMF的LOC/BEL，区域违例0；导出覆盖100%。
- 静态位置逐cell审计按既有明确配置跳过，不能称该扫描通过；独立的pr_verify -full_check已通过。最终拼接还要核对模块位置、布线及接口保持。
- 最终完整V1须等待A验收及两份routed RM拼接，不能提前宣称最终验收通过。

## 对照与剩余工作

匹配全量V1已完成：WNS+0.614/WHS+0.009ns，DRC Error/CriticalWarning0，923447个可布线net全部布通，实测SLL21085。AMF进程1418.428秒，profile placement扣export为1338.764103603秒，route3357.088秒。70%限额分别为937.134873秒和2349.9616秒。两组的原始/修复/replay/传输/排队开销及并行起跑偏移全部另列；不得通过换口径隐藏超限。修复二进制差异与参考兼容性须明确说明，旧参考记录不改写。

两worker验收已完成，A正式WNS+1.494/WHS+0.009ns、922862个可布线net全布通，B WNS+1.494/WHS+0.006ns、922859个可布线net全布通。正在拼接完整V1，继续核验完整布线、DRC、setup/hold、硬RP、接口、静态位置/布线保持、专用级联、pr_verify、完整及两份局部bitstream，再作完整结果结论。

## 故障恢复后的实测时间跨度

当前可计算计时，但完整拼接仍未完成；不能以此宣称完整增量流程验收通过。参考仍为原fullV1，A修复二进制与参考不同，按源代码条件分支及B等价复算作明确标注的兼容分析；严格“完全相同二进制”身份条件不满足，不改写比较哈希或绕过原检查工具。

| 同口径指标 | A | B | 全量V1 |
|---|---:|---:|---:|
| AMF run profile扣export_vivado self | 267.565 s | 116.042 s | 1338.764 s |
| 与全量之比 | 19.99% | 8.67% | 100% |
| route_design | 938.839 s | 1094.581 s | 3357.088 s |
| 与全量之比 | 27.97% | 32.61% | 100% |

上述AMF口径保守地保留诊断成本，不能称为纯算法时间。dumpFinalPacking的inclusive耗时A254.640秒、B92.253秒，含诊断性未打包PU搜索和嵌套导出，不能与其余子阶段重复相加，也不能悄悄扣除以改变主指标。代码对固定时序上下文中的LUT/FF仍可能生成“unpackable”诊断邻居搜索；这是后续性能诊断线索，尚未对本轮代码作修改或证明全部耗时来自该搜索。

成功的两个AMF进程没有时间重叠：原B已完成AMF，A故障修复后才恢复。成功AMF进程跨度2158.564秒；仅两成功进程之间的空档就1751.564秒，已超过placement70%限额937.135秒。因此，尽管单模块profile时间很低，本轮无法证明整组并行placement满足70%要求，不能忽略恢复/起跑偏移后宣称通过。

两次route_design实测开始至最后结束的跨度3267.066秒（54.45分钟），为全量3357.088秒的97.32%，超过70%限额2349.962秒。两次route调用也没有重叠，期间包括另一worker尚在兼容placement及报告等阶段的等待。本轮按原定义的整组routing跨度性能失败。route耗时之和2033.420秒可作工作量补充，却不替代实测跨度。原始墙钟和明确扣除Vivado placement的调整指标将在拼接完成后分别列出。

输入准备仍需优化。去除203个常量后，每组只有2956个动态fabric，固定接口2689/2686，总5645/5642。25-cell/611-pin只读查询诊断通过属性和顺序等价，但批量属性原型0.222秒比标量0.137秒更慢，不能采用为提速成果。连接小样本6.075秒、get_cells31850次/get_property93290次。下一候选应验证逻辑net别名复用和重复canonical查询；必须同完整golden输入逐文件等价，保持真实时序上下文、DSP端点、常量/时钟、SRL及区域掩码。缓存填充测试包含构建成本，不能冒称lookup耗时；GDB因ptrace权限没有取得栈。

DCP和bitstream仅在099保存，本地只同步文档、配置、日志和指标。所有修改都在独立副本，正式源码与用户GUI/其他进程保持不动。

## 完整输入缓存候选03

2026-10-15 06:55:02在099独立目录experiments/preflight/20261015-scoped-input-net-cache-03启动，runner PID1763381。只读原A的不可变linked.dcp，复用冻结脚本与完整golden输入；不执行opt/place/route、AMF或DCP写出，不修改当前worker和生产导出器。启动时可用内存约88.4GiB，预算900秒。07:10预算到期，runner对其自有Vivado组发SIGTERM，exit -15。此为诊断预算结束，不称为卡死或语义报错；完整网表未写完，不采用。

候选缓存逻辑net别名、复用同组segments，并记忆primitive owner资格，保留原driver/sink、常量、时钟、DSP及输出顺序规则。选择阶段242.367秒，cell/static/interface/SRL元数据及5份继承文件逐SHA保持；netlist约18.2/19.87MB未完成，DSP证书/clock/input_scope末尾文件未生成，不能称完整等价。原linked.dcp和所有golden输入均未改变。probe.tcl SHA256为8521aa8b7e1967288bb5960d0228386687028538e3816915978e9c54f5fb2fca，candidate.tcl为1499232ed272a3060b3ec252bbfb1a70ebbead3fad9f41267b7e85d8ca2b4aa9，runner.py为944bbb5614eb3af136cab796c98f3548745079a34bacbc799f139754cf003e84。

## 数组缓存候选05与静态保持

07:11:47在新独立目录experiments/preflight/20261015-scoped-input-array-cache-05启动，runner1766042、nativeVivado1766094，预算1800秒。保留03逻辑net/primitive-owner缓存，进一步用Tcl数组保存type、pin rows和clock driver缓存。所有原查询、筛选、名称和导出顺序规则保留；只读同一A linked.dcp，对9生成文件及5继承文件作完整golden比较。没有热改18的worker，没有重跑AMF或后端。B-net-cache-04只在本地准备且未启动，不含新数组策略，不应误用。

候选05于07:20:06成功完成exit0，9份生成文件及5份继承文件逐SHA全部exact，原DCP和golden哈希不变；完整netlist为19870766字节。manifest SHA256为4f651659ce9c88ccc5a248f4be544667a8aaf8c0151069c74e30bebc3889de60。cache_arrays.tcl SHA256：4331649764812ee2fc823bf7a9919c955be9c5147fd0140549a81569743e4830。

| 同一A linked.dcp输入导出阶段 | 原prepare | 只读候选05 |
|---|---:|---:|
| 接口cone选择 | 640.422 s | 17.150 s |
| cell元数据 | 33.818 s | 18.733 s |
| SRL元数据 | 8.572 s | 0.090 s |
| scoped网表写出 | 1261.667 s | 300.970 s |
| scoped输入总函数（含上述子阶段） | 1944.589 s | 337.050 s |

输入总函数从32.41分钟降到5.62分钟，为原耗时17.33%，实测5.77倍；候选进程499.137秒（8.32分钟）另包含DCP读取146.533秒和启动/关闭。候选未执行link/opt和资源表重建，不能称原49.5分钟完整prepare已实测变为8.32分钟。原始当前worker计时不改写。

实际统计net calls45525、cache misses7043、logical aliases41063，primitive owner misses6621/hits20743；type calls85771/misses5848，pin calls9628/misses5645，clock calls3405/misses1。原查询和筛选顺序规则保持，完整逐字节等价覆盖cell/pin顺序、连接/常量/clock、DSP证书、SRL属性、固定接口上下文及区域掩码。两轮缓存变化及运行负载并非全部单变量控制，不能把测得倍率推广为所有设计的保证。

07:22:30启动B独立只读验证experiments/preflight/20261015-scoped-input-array-cache-b-06，07:30:18成功exit0。9生成文件和5继承文件也全部exact，原DCP/golden未变。该验证将同一函数覆盖合并成一个scope文件，实际单文件包装SHA256 b0cc1e5ada363990042a22a7ea644f6ec9154f792efa18dc38e64f2198b040f5；B manifest SHA256 0a8c9ad3fa2c98b1bb07617580c6b98776e45ab34accd1f2151bbda616a95817。没有AMF/backend/opt/DCP写出。

| 同一B linked.dcp输入导出阶段 | 原prepare | 只读候选06 |
|---|---:|---:|
| 接口cone选择 | 638.733 s | 19.930 s |
| cell元数据 | 33.823 s | 16.743 s |
| SRL元数据 | 8.630 s | 0.080 s |
| scoped网表写出 | 1276.946 s | 277.435 s |
| scoped输入总函数（含子阶段） | 1958.244 s | 314.300 s |

B输入函数从32.64分钟到5.24分钟，实测6.23倍；进程468.275秒（7.80分钟），包含DCP读取140.126秒及启动/关闭。与A一样未重做link/opt/resource表，不能把该进程时间称为完整cold prepare。

已在099新独立部署experiments/deployments/full-amf-dfx-input-cache-19冻结验证通过的组合scope，manifest SHA256 60bdb2b11f914e7456c1cd38829dcfd931374072a6c345e9dfe334fc2bd8401e。相较部署18，实际代码变更只有scripts/dfx_input_scope.tcl；Python语法检查通过，现有worker脚本快照列表包含scope及SRL依赖。88份轻量源/配置/记录形成独立副本，排除硬编码旧运行目录的临时拼接启动器。没有排队新实验或更改当前18；二进制、算法、参数、区域及参考结果保持，配置保留099不可变依赖路径。

## 拼接快照查询诊断

07:35拼接native1768258 CPU约108%、RSS约11.1GiB，已完成A placement快照；仍在module_routing查询，未报错。该函数逐net取nodes/PIPs、逐资源取NAME并按RP footprint筛选，时长需结合输出和CPU判断，不能因无新日志称卡死。

07:40:38在独立experiments/preflight/20261015-route-audit-query-profile-07启动runner1777494，预算900秒。只读A最终routed.dcp，最多4种RP net类别，比较至多256个资源名称和footprint筛选的标量/批量结果，记录完整资源集合批量NAME的时间和数量；不重跑assembly/AMF/place/route，不写DCP。该小样本不证明完整模块路由等价，未修改运行中的审计代码或删去保持检查。probe.tcl SHA256 4d552c1f6b5a44c7686a96f6edcafd44eef2ef9667f35847aae217e374f1ad15。

诊断于07:43:52成功exit0，进程194.809秒，源DCP哈希未变；4类net的资源名称及footprint筛选样本均相同。一个RP内的层次alias仍暴露整个共享physical net：GROUND返回649200 nodes/609543 PIPs，POWER返回506360/446866，GLOBAL_CLOCK返回183011/183010；普通数据样本为57/56。这些不是动态RM新增路由需求或实际新增线数，不能当作SLL数量。

批量NAME对大集合约0.37–1.16秒，但样本footprint过滤仍约0.04–0.06秒/256项，标量/批量差距小。因此只批量NAME不足以解释或解决整个保持审计开销；下一候选应核对footprint查找数据结构，并保留所有层次net key及物理资源保持检查，完整验证后才采用。07:51原拼接仍在A module_routing，elapsed27分47秒、CPU103%，无报错；不能称为卡死，当前运行代码未修改。

A pr_verify-full_check实测636.151秒、exit0；日志比较静态site100775、静态cell868756、静态routed node16650509、静态routed PIP15622726并报告compatible。这些是Vivado pr_verify口径，不是AMF fabric cell数量；原额外逐cell扫描仍按明确配置跳过，不能称该扫描通过。该静态一致性证据独立于尚未完成的两RM拼接验收。


## 08:04 footprint 数组候选启动

原拼接在08:02仍处于A路由保持快照阶段，native1768258 elapsed39分38秒、CPU102%、RSS约11.2GiB，无新错误。两个worker均已完成，未重跑AMF、place或route。

08:04:12启动独立只读候选experiments/preflight/20261015-route-audit-array-candidate-08，runner1786516、wrapper/group1786517、native1786568，预算1800秒。footprint成员表每模块构造一次Tcl数组，资源NAME批量物化；不缓存net别名、不省略层次net或保持检查。先比较GROUND/POWER/GLOBAL_CLOCK/DATA四类完整node/PIP筛选，再导出全部25238层次net的候选快照，仍须与原完整快照逐net/逐资源比较后才考虑新部署。当前18拼接代码不变，尚不能宣称候选通过或提速。

候选probe.tcl SHA256 a88aa583be5541c3194b4a4ea27e0f309c61e7ba6ed13aff399e3706512ca647，runner.py SHA256 f2440810543a96dc4a7b32b053633e894b7df076af88455ba73472b1f3a4c6df。只读输入A routed.dcp SHA256 77de3a9ea9c98a0d5390a35e61334e7562df69c8b962830c83a4f9e06903c28b，最终再次核验。诊断开销单列；原组routing跨度97.32%的性能失败及原输入准备耗时不改写。


## 08:09 完整GROUND node筛选结果

候选08仍在运行。GROUND net的全部649200个node按RP footprint筛选保留2312个，原函数与数组候选完整排序结果exact。原函数132.542184秒，数组候选2.618363秒，约50.62倍；该比值只代表这一完整集合的筛选函数，不能称全模块拼接提速50倍。PIP及其余三类、完整模块快照与原快照对照仍待完成，未采用新审计代码。

原assembly native1768258 elapsed46分32秒，CPU102%、RSS约11.2GiB；仍在A module_routing阶段，无报错。候选native1786568 CPU117%、RSS约11GiB，两项任务未重启，原18未修改。候选完整通过前不停止当前拼接、不删路由保持检查。源DCP最终不变性仍须等待候选manifest完成后核验。


## 08:19 四类完整node/PIP筛选通过

候选08的GROUND、POWER、SIGNAL、GLOBAL_CLOCK四类完整资源集合均exact，共2578103项node/PIP（含不同共享net资源集合，不能视为新增布线量）。八项完整排序筛选原函数合计515.435503秒，候选11.633715秒，约44.31倍；只代表这些完整集合的筛选函数，不是整个拼接的加速比。未缓存物理net别名、未删层次net或保持检查。

08:19正在计算全部25238条层次net的候选快照，已记录约3466条的逐net计时；原完整A快照仍未写出，完整对照及源DCP最终不变性检查尚未完成，候选未采用。原assembly native1768258 elapsed56分32秒、CPU101%、RSS约11.2GiB；候选native1786568 CPU106%、RSS约11.5GiB，均无报错。候选预算仍为1800秒，不热改运行代码或预算，诊断开销单列。


## 08:32 剩余逐net开销诊断

08:29候选08完成约4850条/25238条，累计已记录逐net约772.51秒，完整快照尚未形成。后续08:30统计4861条累计840.93秒，其中852条空资源net占43.75秒；最慢多条DSP内部层次net各约6.48–6.61秒，筛选后都保留2312 nodes/2169 PIPs，与GROUND保留规模一致，但相同数量不能单独证明资源身份等价。这说明仅优化footprint筛选还未解决重复逐net工作，不能采用未完成候选。1800秒预算保持，若到期由候选自己的有限runner停止，原拼接继续，不改历史结果。

08:32:20在独立experiments/preflight/20261015-route-audit-phase-profile-09启动23条固定net样本只读诊断，runner1793260、wrapper/group1793261、native1793312，预算900秒。样本来自候选08已完成记录，含最慢别名、空资源、普通数据、四类根net；拆分TYPE、node/PIP查询、NAME/footprint筛选、字典插入及logical segments查询，并比较同segment组样本的完整筛选结果。诊断不会缓存生产路由或删层次net，不重跑AMF/place/route/assembly。

09 probe SHA256 dadee8b6e7b4bd0c402bd2959153eea18127eabc6c1c123c3a91cba97baae0c0，runner76b5f3b217d496f5d0deac1d2752e5337896618e0fef392b191d4e1c2f2aae08，固定23条sample SHA01d457f8811891db9e6104c0f9cb77db786638ffde0116048279f406f77c66b5。08:33尚在打开只读A最终DCP，不能提前宣称诊断结论或完整等价。原assembly native1768258 elapsed1小时9分50秒、CPU101%、无错误；未改当前18。


## 08:34 候选08按预算结束

08于08:34:13达到既定1800秒限额，由其自己的有限runner发送SIGTERM，exit -15；对应runner/native进程已退出，源A routed.dcp前后SHA均77de3a9ea9c98a0d5390a35e61334e7562df69c8b962830c83a4f9e06903c28b，checkpoint_unchanged=true。四类完整筛选exact的证据保留，但整个模块快照没有完成，不能宣称完整路由等价，不能采用该候选，也不重启08。这是诊断预算退出，不是布线失败或语义校验失败。

当前仅原18拼接与只读09分阶段样本诊断继续。08原始诊断1800.002秒及现场单列，不能从实验总墙钟中默默删除。09只查23条固定样本，不重跑成功worker/AMF/place/route；待实际结果定位剩余瓶颈后再在新独立候选处理。


## 08:44 精确资源序列缓存完整对照候选

只读09于08:36:36成功exit0，进程255.638566秒（含DCP打开183.151秒），DCP前后SHA不变。23样本合计node/PIP查询2.162363秒，NAME/数组footprint筛选56.950218秒，dict插入0.007189秒，segments查询与名称0.225478秒；过滤部分占这些分段合计约96%。8条慢DSP GROUND样本属于同一175层次net逻辑组，完整保留node/PIP列表比较一致。RP根GROUND只有1个逻辑segment，故不能把所有同TYPE的net直接视为同逻辑组，更不能凭TYPE或相同资源数量直接缓存。该诊断不是完整模块等价证明。

08:43:47新独立候选experiments/preflight/20261015-route-audit-exact-vector-cache-10已启动，runner1795887、wrapper/group1795888、native1795939，预算3600秒。首次复现09的23条完整样本；随后两遍都调用冻结原module_routing全局层次net循环，保留所有net key。参考遍使用原footprint过滤函数，只有完整资源序列与footprint均逐项身份一致的重复输入才能复用；每种新输入仍执行原函数并对比数组结果。候选遍另算数组过滤与同样严格的完整输入守卫，再逐key/排序node/PIP列表完整对照。缓存大输入时，native资源完整序列先与真实NAME完整序列验证一致才允许native向量命中，否则以完整NAME序列守卫；数量只作bucket，不作身份依据。缓存不跨DCP或RP footprint，不省略静态/接口/区域/pr_verify检查，不改变任何placement/routing约束。

参考遍使用透明的精确输入记忆，因此它的时间不等于未加速原18运行时间；不能拿它冒称原始基线重跑耗时。若旧18的a_routing.tcldata已经形成，候选还会额外逐key完整比较；该独立旧文件对照是否exact/pending会明确记在verification.tsv，不以样本通过替代完整验证。

10 probe SHA256 9962e79da238a89e40f1b77a1c0011df0330a460e4783e29820feee6b62d2569，runner bc4c1798cd34530e776a45998d4640177c417d92ba5e67b960b99126cde92389；冻结09 sample SHA e0bae36385f39ddc3d8814bd863113adb286b1fb54a60e7e34b606a5154d0574。09 manifest SHA0fedd4606ee768311849c1c5f6bad3eac41f4b375a6b5094fa97977828ba7774。08:44仍在只读打开DCP，尚未通过或采用。原18 assembly继续，08:39 elapsed1小时16分41秒、CPU101%、无报错；不重跑worker/full/AMF/place/route，09已完成不重跑、08预算退出不重启。


## 08:53 原生集合展开错误与独立11修复

10于08:46:49以exit2结束，进程181.872838秒（DCP打开177.110秒），源DCP前后SHA不变。实际错误Common17-736：list {*}$objects把649200项原生资源集合转换为文本时触发tcl.collectionResultDisplayLimit，提示截断到500项。诊断随即退出，未接受截断数据、未产生样本/完整对照或任何新布局布线结果。这是本诊断脚本的集合处理错误，AMF、两个已通过worker和原18拼接未被修改。10现场及manifest SHAde38ca62b8743706b80ce5ece95a57d0be8a60519bb0729e5d68f3842587db62保留，10不重跑。

08:52:43启动新独立experiments/preflight/20261015-route-audit-name-vector-cache-11，runner1798503、wrapper/group1798504、native1798555，预算3600秒。修复仅把原生整个集合的参数展开改为foreach逐资源构造普通Tcl列表，并分别核对完整原生资源数与完整NAME数；首次完整序列与真实NAME全向量匹配后才允许原生序列身份守卫，否则回退完整NAME向量比较。没有调整集合显示上限，不凭TYPE/count缓存，不删任何net或保持检查。两遍全模块参考/候选、23完整样本和额外旧18快照对照的设计保持。

11 probe SHAc529d0e444ef74c5cf707b2272b59d692717613d228681d15066b270dc3597a2，runner04a4d843298a76e017d4c3b0280294b7a661637187b65cbeb6351002135d20a6。原冻结loop/filter哈希、同A routedDCP SHA77de3a9ea9c98a0d5390a35e61334e7562df69c8b962830c83a4f9e06903c28b、09 golden sample SHAe0bae36385f39ddc3d8814bd863113adb286b1fb54a60e7e34b606a5154d0574不变。08:53真实进程打开DCP、CPU121%，仍未验证通过或采用。参考时间仍为精确输入记忆后的时间，不是未优化18耗时；额外原18文件对照pending也不能称通过。原18 native1768258于08:49 elapsed1小时26分29秒、CPU101%，无新产物或报错。


## 09:02 完整样本守卫通过及小样本12

11已输出EXACT_VECTOR_ALL23_SAMPLES_EQUAL：23条完整node/PIP路由列表均与09 golden一致，sample阶段49.318755秒，46次过滤调用、16次完整向量守卫命中、18次非空miss，其余空集合；六种大集合首次native序列与真实NAME全向量核对均为1。Common17-736转换故障已在实际样本中解除，未调整显示上限、未接受截断。但全module两遍和最终DCP不变性仍未完成，不采用11。当前reference缓存表已记录GROUND node/PIP两项原函数计算并与数组结果一致，其时间不是未加速18整遍时间。

09:01:47另启独立23样本只读experiments/preflight/20261015-route-audit-name-only-sample-12，runner1800251、wrapper/group1800252、native1800303，预算900秒。它每次直接批量读完整NAME属性，核对资源数量并以完整NAME序列作为缓存身份，跳过额外native序列构造/认证，只复现同09的23条完整golden路由以衡量守卫成本。它不做wholemodule/AMF/place/route/assembly，不能替代11的完整参考对照或直接生产采用。12 probe SHA33be3c178f078ff6136bad3a042dc2f9a114fb32ddf2dff441c69d70b1ccd0cb，runner7d4f10be9201d81f27033c51f150eda2c5a6d213947b0916cc9662c6501f89e3；同一golden23样本和A routedDCP不变。09:02在DCP读取、CPU118%，暂无结果。

原18仍运行，08:59 native1768258 elapsed1小时36分42秒、CPU101%、无报错，A路由快照文件尚未写出。11同刻CPU112%、RSS约12GiB，未重启或热改。各诊断开销和待验收范围继续单列，不把样本通过当作最终组合验收。


## 09:09 NAME-only小样本结果与完整参考进度

12于09:05:51成功exit0，进程244.091094秒（DCP打开178.767秒），DCP前后SHA不变。23条完整路由输出文件SHA e0bae36385f39ddc3d8814bd863113adb286b1fb54a60e7e34b606a5154d0574，与09 golden逐字节一致；verification明确whole_module_equivalence=NOT_TESTED、production_adopted=0。样本阶段51.801716秒、46calls/16NAME守卫hits/18非空miss；相较11样本49.318755秒未观察到加速（单次约慢5.0%，不作统计显著性结论）。不推进或重跑12，不将其样本正确性冒称完整模块验证。

11仍在全模块reference遍，进度到8000次过滤调用（约4000条net）、99次完整向量cache命中、7512次非空miss。新增八种large resource输入（含20924/20922规模的控制net）首次native/NAME完整身份均认证1，原过滤函数与数组结果均完整一致。计数是过滤调用，每条net最多node/PIP两次，不等于8000条net，也不意味着已经完成全部25238条。样本阶段仍49.318755秒，完整参考/候选快照和最终DCP检查均待完成。

09:09原18 native1768258 elapsed1小时46分31秒、CPU101%、RSS约11.2GiB，仍无a_routing.tcldata；11 native1798555 elapsed16分58秒、CPU105%、RSS约12.7GiB，无报错。参考遍已加精确输入记忆，其计时不是未优化原18基准；额外旧18文件对照仍待产物存在后才可检查。当前只有18和11继续，不重跑任何已完成AMF/backend或12样本。


## 09:19 完整参考遍与拼接现场

11参考遍进度到12000次footprint过滤调用，约6000条net，678次完整向量身份守卫命中、8394次非空miss；完整25238条net参考遍及独立候选遍尚未完成，verification.tsv未生成。候选实际native1798555 elapsed27分11秒、CPU104%、RSS约13.2GiB，原3600秒预算不变。23样本已通过的范围保持，全模块等价、最终源DCP不变性和额外旧18快照对照仍未完成，未采用候选。参考遍含精确相同输入的记忆，其时间不是未加速18基线。

原18 native1768258于09:19:54 elapsed1小时56分44秒、CPU100%、RSS约11.2GiB，无报错；a_placement.tcldata存在，a_routing.tcldata尚未生成，最终组合验收仍待完成。099可用内存约80GiB、/Orion可用745GiB，当前未见资源耗尽。该阶段判断基于脚本顺序、日志和文件，不冒称获取了运行栈。没有启动新诊断或重跑worker/全量/AMF/place/route，没有修改当前运行代码；继续原18和有限候选11。09:19监测证据包含真实命令、进程与资源快照，后续以新状态覆盖。


## 09:29 完整对照尚未完成

11参考遍最新记录18000次过滤调用，约9000条net，1498次完整向量守卫命中、10321次非空miss；仍未完成25238条net参考遍或候选遍，verification.tsv和完整快照未生成。native1798555 elapsed37分15秒、CPU103%、RSS约13.2GiB，无报错；原3600秒诊断预算及运行代码不变，仍不采用。全部输入资源身份、每个层次net key及保持检查的要求不变。

原18 native1768258 elapsed2小时6分48秒、CPU100%、RSS约11.2GiB，仍未形成a_routing.tcldata，无新增报错。099可用内存约65GiB、磁盘745GiB。原拼接和有限验证11继续，未新启任务或重复任何已完成AMF/place/route/全量。最终组合正确性待验收、原整组70%性能失败和二进制差异资格限制保持原记录。


## 09:44 原生筛选小样本13与完整参考进度

09:40读取的11参考遍已到22000次过滤调用，约11000条net，1958次完整向量守卫命中、11214次非空miss；native1798555 elapsed47分21秒、CPU102%，原3600秒预算不变。完整参考/候选双遍和最终DCP检查仍未完成，未采用。原18 native1768258 elapsed2小时16分54秒、CPU100%，无报错、A路由快照尚未写出；不修改或重启当前运行。

09:43:15启动新的只读23样本experiments/preflight/20261015-route-audit-native-filter-sample-13，runner1807463、wrapper/group1807464、native1807515，独立预算900秒。09/12已表明剩余开销集中在完整NAME资源向量和逐项筛选；13将精确footprint的literal tile名称组成共享前缀的正则表达式，对完整批量NAME属性使用Tcl原生lsearch筛选和lsort，不做逐资源Tcl列表重建或任何别名/TYPE/count缓存。它保留完整NAME数量检查，并对footprint每个tile及构造的非成员名称检验匹配边界；本地60条前缀、后缀和多层路径合成用例精确等价。实际验证仍需同09的23条完整路由golden逐key逐资源一致及最终DCP哈希检查，本地合成测试不代表实际通过，更不代表全模块等价。

13 probe SHA25d4b25e2fa3c6b1320096601af9d6263a7edefad25cf4b8c3472166c491d532，runner SHA3e3f1c7d4a8159cfc216a6472677ca04a799dbc156e012c04b63501eac391e6d；同A routedDCP SHA77de3a9ea9c98a0d5390a35e61334e7562df69c8b962830c83a4f9e06903c28b，23样本golden SHAe0bae36385f39ddc3d8814bd863113adb286b1fb54a60e7e34b606a5154d0574。09:43:47实际仍在DCP读取、CPU119%，无结果，production_adopted=false、whole_module_not_tested=true。这不是重跑AMF、place、route或assembly；当前18、11代码和预算均未改变。所有诊断计时单列，原整组70%性能失败及二进制资格限制不变。


## 09:51 小样本13完整结果与用户进度核对

13于09:50:51完整exit0，进程455.507809秒，源A DCP前后SHA相同。23条完整路由输出SHA e0bae36385f39ddc3d8814bd863113adb286b1fb54a60e7e34b606a5154d0574，与09 golden逐字节exact；10970个footprint tile和31969字节模式的成员/非成员边界检查通过。实测setup9.156705秒，样本247.239622秒，其中bulk NAME查询28.102032秒、原生正则筛选215.954110秒，明显慢于11样本49.318755秒。该方案正确但未提速，不采用、不重跑；wholemodule=NOT_TESTED，不能把样本精确等价当作完整模块证明。该结果排除了当前大literal trie正则作为加速方向，诊断455.508秒开销保留。

09:51:03原18 native1768258 elapsed2小时27分52秒、CPU100%，仍只有a_placement.tcldata，没有a_routing.tcldata或最终拼接DCP。11 native1798555 elapsed58分19秒、CPU102%，参考遍记录28000次过滤调用（约14000条net）、2698次完整向量hits、13272次非空miss，原3600秒预算即将到期，未修改或延长。完整参考/候选快照与最终DCP检查仍待，不采用；若预算退出按现场如实记录，不能重启旧11或把局部证明升格为全模块验证。

两个RM已完成的布局布线/pr_verify/完整和局部bits、全量V0/V1参考、输入优化A/B全14文件exact与19冻结状态均保持。整体最终组合验收仍未完成；原整组routing跨度97.32%超过70%限额、AMF修复带来的二进制资格差异及原prepare、失败/诊断/传输/排队开销继续保留，不以单模块时间替代整组实际跨度。
