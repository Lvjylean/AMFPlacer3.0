"""Opt-in full-run contract for independent RP partitioning and first-full margins."""
POLICY = 'clock-region-slice-dsp-v1'


def configure(job, cfg, capability):
    enabled = job.get('dfx_partition_floorplan', False)
    if not isinstance(enabled, bool):
        raise ValueError('dfx_partition_floorplan must be boolean')
    if not enabled:
        if cfg.get('DFX partition floorplan', 'false') != 'false':
            raise ValueError('Enable DFX partition floorplan through the full-run job')
        return False
    if capability.get('dfx_partition_floorplan_schema') != 1:
        raise ValueError('Binary lacks independent DFX partition/floorplan support')
    if cfg.get('RandomInitialPlacement') == 'true' or (cfg.get('external floorplan membership file') or cfg.get('external floorplan regions file')):
        raise ValueError('DFX partition/floorplan requires its constrained initialization')
    # Existing exact-recovery contracts predate raw/effective mask separation.
    # A future recovery adapter must explicitly verify all new input hashes.
    for key in ('recover_exact_preparation', 'reuse_prepared_input_run'):
        if job.get(key):
            raise ValueError('DFX partition/floorplan requires fresh region/resource export: '+key)
    if job.get('native_dfx_edge_policy', POLICY) != POLICY:
        raise ValueError('DFX partition/floorplan requires first-full SLICE/DSP margin policy')
    if job.get('native_dfx_edge_side_policy', 'interior-prohibit') != 'interior-prohibit':
        raise ValueError('Corner PROHIBIT must not exclude a whole side column')
    job['native_dfx_edge_policy'] = POLICY
    job['native_dfx_edge_side_policy'] = 'interior-prohibit'
    job['full_edge_policy'] = True  # export actual inventory in the existing prepare process
    cfg['DFX partition floorplan'] = 'true'
    return True


def bind_outputs(cfg, out):
    cfg.update({
        'DFX resource inventory file': str(out / 'resources.tsv'),
        'DFX resource geometry file': str(out / 'resource_regions.tsv'),
        'DFX boundary audit file': str(out / 'full_edge_policy_audit.tsv'),
        'DFX effective sites file': str(out / 'hard_region_sites.effective.tsv'),
        'DFX partition audit file': str(out / 'dfx_partition_floorplan.tsv'),
    })
