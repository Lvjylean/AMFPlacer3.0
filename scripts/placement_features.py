"""Shared placement/backend feature contract; absent new keys retain legacy defaults."""
import math

BOOL_KEYS = (
    'SllQuantityEnable', 'SllDynamicTauEnable', 'SllPlanningEnable',
    'SllCapacityOptimizationEnable', 'SllTimingCorrectionEnable', 'SllRouteConstraintEnable',
    'DieFirstSpreadingEnable', 'TimingDrivenPlacementEnable', 'DetailedDiagnosticsEnable',
    'IterativeMacroLegalizationEnable', 'SllTauCompletionRequired', 'SllDeferredUntilFirstSpread',
)
CHOICES = {key: ('true', 'false') for key in BOOL_KEYS}
CHOICES.update(SllTauPolicy=('adaptive', 'periodic', 'drop-or-timeout'),
               SllRegionPolicy=('preserve', 'release-single'))

def flag(config, key, default=False):
    value = config.get(key, 'true' if default else 'false')
    if value not in ('true', 'false'):
        raise ValueError(key + ' must be true/false')
    return value == 'true'

def route_enabled(config):
    return flag(config, 'SllPlanningEnable') and flag(config, 'SllRouteConstraintEnable', True)

def validate(config):
    for key, choices in CHOICES.items():
        if key in config and config[key] not in choices:
            raise ValueError(key + ': expected ' + '/'.join(choices))
    for key in ('SllQuantityTau','SllQuantityGamma','SllDensityTargetUtilization',
                'SllColumnTargetUtilization','SllColumnMaxUtilization','SllColumnUtilizationStep'):
        if key in config:
            value=float(config[key])
            if not math.isfinite(value) or value<=0 or ('Utilization' in key and value>1):
                raise ValueError(key + ' must be finite and positive (utilization <= 1)')
    if float(config.get('SllColumnTargetUtilization','.75'))>float(config.get('SllColumnMaxUtilization','.85')):
        raise ValueError('SLL column target cannot exceed maximum')
    if 'SllConstraintWorkers' in config and (not config['SllConstraintWorkers'].isdigit() or not 1<=int(config['SllConstraintWorkers'])<=8):
        raise ValueError('SllConstraintWorkers must be an integer in [1,8]')
    for key in ('SllTimingCorrectionEnable', 'SllRouteConstraintEnable', 'SllDeferredUntilFirstSpread'):
        if flag(config, key) and not flag(config, 'SllPlanningEnable'):
            raise ValueError(key + ' requires SllPlanningEnable=true')

def resolve(config):
    """Only the new explicit profile derives implementation switches."""
    validate(config)
    result = dict(config)
    planning = flag(result, 'SllPlanningEnable')
    result['SllJointPlacementEnable'] = 'true' if planning else 'false'
    result['SllTilePlanningEnable'] = 'true' if planning else 'false'
    inactive = {}
    for parent, children in (
        ('SllQuantityEnable', ('SllDynamicTauEnable', 'SllShortSpanEnable')),
        ('SllPlanningEnable', ('SllCapacityOptimizationEnable',)),
    ):
        if not flag(result, parent):
            for child in children:
                if child in result:
                    inactive[child] = parent + '=false'
                    result[child] = 'false'
    if not flag(result, 'SllDynamicTauEnable'):
        inactive['SllTauPolicy'] = 'SllDynamicTauEnable=false; use SllQuantityTau'
    return result, inactive
