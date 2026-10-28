#!/usr/bin/env python3
"""Run isolated Vivado SLL generation/audit shards and record every result."""
import concurrent.futures, hashlib, json, os, pathlib, subprocess, sys, time
def digest(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open("rb") as f:
        for b in iter(lambda:f.read(8*1024*1024),b""):h.update(b)
    return h.hexdigest()
def main():
    mode,checkpoint,directory,workers=sys.argv[1:];workers=int(workers)
    if mode not in ("apply","audit") or not 1<=workers<=8:raise ValueError("Invalid worker arguments")
    root=pathlib.Path(__file__).resolve().parent;out=pathlib.Path(directory)
    vivado=os.environ.get("AMF_SLL_VIVADO","/Projects/Xilinx26/2026.1/Vivado/bin/vivado")
    shards=sorted(out.glob("shard-*"))
    if not shards:raise ValueError("No SLL shards")
    manifest={"mode":mode,"input_dcp":checkpoint,"input_sha256":digest(checkpoint),"workers":workers,
      "helper_sha256":digest(root/"sll_routes.tcl"),"worker_sha256":digest(root/"sll_parallel_worker.tcl"),
      "runner_sha256":digest(__file__),"started_unix":time.time(),"shards":[]}
    started=time.monotonic()
    def run(shard):
        start=time.monotonic();dest=shard/mode;dest.mkdir(exist_ok=False)
        data=shard/"plan.tcl" if mode=="apply" else shard/"apply/records.tcl"
        cmd=[vivado,"-mode","batch","-notrace","-nojournal","-log",str(dest/"vivado_internal.log"),
             "-source",str(root/"sll_parallel_worker.tcl"),"-tclargs",mode,checkpoint,str(shard),str(root/"sll_routes.tcl")]
        with (dest/"vivado.log").open("w") as log:
            p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,cwd=dest)
        result={"shard":shard.name,"command":cmd,"exit_code":p.returncode,"seconds":time.monotonic()-start,"input_sha256":digest(data)}
        (dest/"status.json").write_text(json.dumps(result,indent=2)+"\n")
        return result
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for future in concurrent.futures.as_completed([pool.submit(run,s) for s in shards]):
            row=future.result();manifest["shards"].append(row);print(json.dumps(row),flush=True)
    manifest["wall_seconds"]=time.monotonic()-started;manifest["shards"].sort(key=lambda x:x["shard"])
    manifest["state"]="completed" if all(x["exit_code"]==0 for x in manifest["shards"]) else "failed"
    (out/(mode+"-manifest.json")).write_text(json.dumps(manifest,indent=2)+"\n")
    if mode=="audit" and manifest["state"]=="completed":
        rows=0
        for shard in shards:
            path=shard/"audit/sll-path-audit.tsv"
            with path.open() as f:
                next(f)
                for line in f:
                    if line.rstrip("\n").split("\t")[-1]!="1":raise RuntimeError("SLL audit mismatch")
                    rows+=1
        manifest["verified_receiver_rows"]=rows
        (out/(mode+"-manifest.json")).write_text(json.dumps(manifest,indent=2)+"\n")
    return 0 if manifest["state"]=="completed" else 1
if __name__=="__main__":sys.exit(main())
