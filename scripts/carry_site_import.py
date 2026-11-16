"""Change only the import transaction boundary of complete CARRY8 sites."""
import collections
import hashlib
import json
import re
import shutil


def carry_groups(assignments, cell_types):
    sites = set()
    for name, target in assignments.items():
        if cell_types.get(name) == 'CARRY8':
            if not re.fullmatch(r'SLICE_X\d+Y\d+/CARRY8', target):
                raise ValueError('CARRY8 needs an explicit site/BEL: '+name)
            sites.add(target.split('/')[0])
    groups = collections.OrderedDict()
    for name, target in assignments.items():
        site = target.split('/')[0]
        if site not in sites:
            continue
        if any(c in name+target for c in '\t\r\n'):
            raise ValueError('Unsupported CARRY import identifier')
        if not re.fullmatch(re.escape(site)+r'/[A-Za-z0-9_]+', target):
            raise ValueError('Whole CARRY site member needs explicit BEL: '+name)
        groups.setdefault(site, []).append((name, target))
    return groups


def install(root, directory, script, assignments, cell_types):
    groups = carry_groups(assignments, cell_types)
    if not groups:
        return script, None
    initial = 'set result [catch {place_cell {'
    retry = 'set result [catch {place_cell  $placeBatch }'
    if script.count(initial) < 1 or script.count(retry) != 1:
        raise ValueError('Unsupported import boundaries for CARRY atomic adapter')
    data = directory/'placement/carry_site_groups.tsv'
    data.write_text('site\tcell\ttarget\n'+''.join(
        site+'\t'+name+'\t'+target+'\n'
        for site, members in groups.items() for name, target in members))
    helper = directory/'inputs/carry_site_import.tcl'
    shutil.copy2(root/'scripts/carry_site_import.tcl', helper)
    setup = '''# Import all requested cells in each CARRY8 site as one transaction.
source [file join $script_path .. inputs carry_site_import.tcl]
amf_carry_import::init [file join $script_path carry_site_groups.tsv] [file join $script_path .. reports carry_site_import.tsv]
'''
    script = script.replace(initial, setup+initial, 1)
    script = script.replace(initial, 'set result [catch {amf_carry_import::place {')
    script = script.replace(retry, 'set result [catch {amf_carry_import::place $placeBatch }')
    report = dict(policy='complete-carry8-site-on-first-reference',
                  sites=len(groups), cells=sum(map(len, groups.values())),
                  assignments_changed=False, shape_changed=False,
                  retry_policy='failed whole site remains a reported failure; never split or silently retry',
                  groups_sha256=hashlib.sha256(data.read_bytes()).hexdigest(),
                  helper_sha256=hashlib.sha256(helper.read_bytes()).hexdigest())
    (directory/'reports/carry_site_import_plan.json').write_text(json.dumps(report, indent=2)+'\n')
    return script, report
