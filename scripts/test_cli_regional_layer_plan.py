#!/usr/bin/env python3
"""Offline fixed-pose transport regression. No tuning/search, deployment or printing."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import zipfile

from test_cli_slice_analysis import digest, save_json
from test_cli_slice_features import write_stl
from evaluate_layer_height_candidate import load_artifacts
from check_regional_layer_scope import check_scope


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--before-binary', type=Path)
    parser.add_argument('--machine', type=Path, required=True)
    parser.add_argument('--process', type=Path, required=True)
    parser.add_argument('--filament', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    root = args.output_dir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    # Closed wedge: upward shallow face Z8..10, asymmetric for pose checks.
    vertices = [(0,0,0),(12,0,0),(12,6,0),(0,6,0),(0,0,8),(12,0,10),(12,6,10),(0,6,8)]
    faces = [(0,2,1),(0,3,2),(4,5,6),(4,6,7),(0,1,5),(0,5,4),(1,2,6),(1,6,5),
             (2,3,7),(2,7,6),(3,0,4),(3,4,7)]
    model = root / 'wedge.stl'
    write_stl(model, [[vertices[i] for i in face] for face in faces])
    process = json.loads(args.process.read_text())
    process.update(layer_height='0.2', initial_layer_print_height='0.24', precise_z_height='0',
                   spiral_mode='0', raft_layers='0', enable_support='0', brim_type='no_brim', skirt_loops='0')
    process_path = root/'process.json'
    save_json(process_path, process)
    plan = dict(version='regional-slope-v1', modelSha256=digest(model), modelScale=1., rotateX=0., rotateY=0., rotateZ=0.,
                baseMm=.2, firstMm=.24, minimumMm=.16, maximumMm=.2, modelHeightMm=10.,
                ranges=[dict(minimumZMm=8.04, maximumZMm=9.84, layerHeightMm=1.8/11)])
    identities = {str(path.resolve()): digest(path) for path in [args.binary, args.machine, args.filament, process_path, model, Path(__file__)]}
    if args.before_binary:
        identities[str(args.before_binary.resolve())] = digest(args.before_binary)
    save_json(root/'identities.json', identities)
    summary = {}

    def run(name, value=None, pose=(1.,0.,0.,0.), extra=(), error=False, report=True, binary=None, raw=None,
            process_override=None, machine_override=None, filament_override=None, progress=False,
            export_transforms=True):
        case = root/name
        case.mkdir()
        paths = [args.machine, process_path, args.filament]
        for i, override in enumerate([machine_override, process_override, filament_override]):
            if override:
                config = json.loads(paths[i].read_text())
                config.update(override)
                paths[i] = case/('config-' + str(i) + '.json')
                save_json(paths[i], config)
                identities[str(paths[i])] = digest(paths[i])
        command = [str(binary or args.binary), '--datadir', str(case/'data'), '--debug', '1', '--slice', '0',
            '--load-settings', str(paths[0])+';'+str(paths[1]), '--load-filaments', str(paths[2]),
            '--outputdir', str(case/'output'), '--arrange', '0', '--auto-plate', '0', '--ensure-on-bed',
            '--auto-repair-model', '0', '--model', model.name,
            '--model-position', '80,90,0', '--model-scale', str(pose[0]), '--model-rotate', ','.join(map(str,pose[1:]))]
        if export_transforms:
            command.append('--export-model-transforms')
        if report:
            command.append('--slice-analysis-report')
        if progress:
            command += ['--pipe', str(case/'progress.pipe')]
        if value is not None or raw is not None:
            path = case/'plan.json'
            path.write_text(raw if raw is not None else json.dumps(value))
            command += ['--model-layer-height-plan', str(path)]
        command += list(extra) + [str(model)]
        save_json(case/'command.json', command)
        with (case/'stdout.log').open('wb') as log, (case/'progress.jsonl').open('wb') as progress_log:
            reader = None
            try:
                if progress:
                    os.mkfifo(case/'progress.pipe')
                    reader = subprocess.Popen(['cat', str(case/'progress.pipe')], stdout=progress_log)
                result = subprocess.run(command, cwd=case, stdout=log, stderr=subprocess.STDOUT, timeout=120)
                if reader is not None and result.returncode == 0:
                    assert reader.wait(timeout=5) == 0, 'Progress reader did not finish'
            finally:
                if reader is not None:
                    if reader.poll() is None:
                        reader.terminate()
                    reader.wait(timeout=5)
                    (case/'progress.pipe').unlink()
        result_path = case/'output/result.json'
        native = json.loads(result_path.read_text()) if result_path.exists() else {}
        row = {'exit_code': result.returncode, 'return_code': native.get('return_code'), 'passed': False}
        summary[name] = row
        save_json(root/'summary.json', summary)
        if error:
            assert result.returncode != 0, name
            assert not list((case/'output').glob('*.gcode')), name
        else:
            assert result.returncode == 0 and native.get('return_code') == 0, (name, row)
            if export_transforms:
                transforms = native['model_transforms']
                assert len(transforms) == 1, transforms
                assert all(abs(a-b) < 1e-4 for a,b in zip(transforms[0]['position'], [80,90,0])), transforms
            if report:
                load_artifacts(case)  # Actual G-code and native report must both parse.
            if progress:
                messages = [json.loads(line) for line in (case/'progress.jsonl').read_text().splitlines()]
                percentages = [message['total_percent'] for message in messages]
                assert percentages and percentages[-1] == 100, (name, messages)
                assert all(0 <= percent <= 100 for percent in percentages), (name, percentages)
                assert percentages == sorted(percentages), (name, percentages)
                row['progress_messages'] = len(messages)
        row['passed'] = True
        save_json(root/'summary.json', summary)
        print(name, 'PASS', flush=True)
        return case

    fixed = run('fixed')
    regional = run('regional', plan)
    baseline, baseline_fact, _ = load_artifacts(fixed)
    candidate, candidate_fact, _ = load_artifacts(regional)
    scope = check_scope(baseline, candidate, plan)
    save_json(root/'scope.json', scope)
    assert scope['scope_verified'], scope
    no_report = run('regional-no-report', plan, report=False)
    def commands(folder):
        return [line.split(';',1)[0].strip() for line in (folder/'output/plate_1.gcode').read_text().splitlines()
                if line.split(';',1)[0].strip()]
    assert commands(regional) == commands(no_report), 'Report changed executable commands'
    if args.before_binary:
        before = run('before-fixed', binary=args.before_binary)
        assert commands(before) == commands(fixed), 'Ordinary slicing changed'
        _, before_fact, _ = load_artifacts(before)
        assert before_fact['configuration'] == baseline_fact['configuration']
    if os.name == 'posix':
        fixed_pipe = run('fixed-pipe', progress=True)
        regional_pipe = run('regional-pipe', plan, progress=True)
        regional_pipe_no_report = run('regional-pipe-no-report', plan, progress=True, report=False)
        assert commands(fixed_pipe) == commands(fixed), 'Pipe changed ordinary G-code'
        assert commands(regional_pipe) == commands(regional), 'Pipe changed regional G-code'
        assert commands(regional_pipe_no_report) == commands(regional), 'Pipe/report changed regional G-code'
        if args.before_binary:
            before_pipe = run('before-fixed-pipe', progress=True, binary=args.before_binary)
            assert commands(before_pipe) == commands(fixed_pipe), 'Ordinary service slicing changed'
        service_flags = ['--allow-multicolor-oneplate', '--optimize-model-detail-paths', '1']
        service = run('service-detail', plan, extra=service_flags, export_transforms=False)
        service_pipe = run('service-detail-pipe', plan, extra=service_flags, progress=True, export_transforms=False)
        assert commands(service_pipe) == commands(service), 'Service pipe changed detail G-code'
        package_flags = service_flags + ['--export-3mf', 'slice.gcode.3mf', '--smooth-m73-progress', '1']
        package_full = run('service-3mf-full', plan, extra=package_flags, progress=True, export_transforms=False)
        package_min = run('service-3mf-min', plan, extra=package_flags + ['--min-save'], progress=True, export_transforms=False)
        assert commands(package_full) == commands(package_min), 'Minimum save changed G-code'
        for package in [package_full, package_min]:
            with zipfile.ZipFile(package/'output/slice.gcode.3mf') as archive:
                embedded = [name for name in archive.namelist() if name.endswith('.gcode')]
                assert len(embedded) == 1, archive.namelist()
                assert archive.read(embedded[0]) == (package/'output/plate_1.gcode').read_bytes()
        for name, extra in [('repair', ['--repair-model','cgal']),
                            ('cache', ['--export-slicedata',str(root/'pipe-cache')])]:
            run('pipe-conflict-'+name, plan, extra=extra, progress=True, error=True)
    # The 1.25 budget belongs to PAM, not the native execution contract.
    lower_budget = copy.deepcopy(plan)
    lower_budget['minimumMm'] = .12
    lower_budget['ranges'][0]['layerHeightMm'] = 1.8 / 15
    run('upstream-selected-bound', lower_budget)
    posed = copy.deepcopy(plan)
    posed.update(modelScale=1.25, rotateY=90., modelHeightMm=15.)
    posed['ranges'] = [dict(minimumZMm=.24, maximumZMm=3.44, layerHeightMm=3.2/19)]
    fixed_pose = run('posed-fixed', pose=(1.25,0.,90.,0.))
    regional_pose = run('posed-regional', posed, pose=(1.25,0.,90.,0.))
    scope_pose = check_scope(load_artifacts(fixed_pose)[0], load_artifacts(regional_pose)[0], posed)
    save_json(root/'scope-pose.json', scope_pose)
    assert scope_pose['scope_verified'], scope_pose
    for key, value in [('modelSha256','0'*64), ('rotateY',90.), ('modelScale',2.), ('modelHeightMm',11.),
                       ('baseMm',.24), ('firstMm',.2), ('minimumMm',0.), ('version','unknown'), ('ranges',[])]:
        invalid = copy.deepcopy(plan)
        invalid[key] = value
        run('invalid-'+key, invalid, error=True)
    for name, ranges in [('overlap', plan['ranges']*2),
                         ('phase',[dict(minimumZMm=8.05,maximumZMm=9.84,layerHeightMm=1.79/11)]),
                         ('count',[dict(minimumZMm=8.04,maximumZMm=9.84,layerHeightMm=.17)])]:
        invalid = copy.deepcopy(plan)
        invalid['ranges'] = ranges
        run('invalid-'+name, invalid, error=True)
    for name, extra in [('adaptive',['--variable-layer-height-mode','adaptive']), ('repair',['--repair-model','cgal']),
                        ('rotate',['--rotate-x','10']), ('arrange',['--arrange','1']),
                        ('cache',['--export-slicedata',str(root/'cache')])]:
        run('conflict-'+name, plan, extra=extra, error=True)
    for name, override in [('base', {'layer_height':'.16'}), ('first', {'initial_layer_print_height':'.2'}),
                           ('raft', {'raft_layers':'3'}), ('spiral', {'spiral_mode':'1'}), ('precise', {'precise_z_height':'1'})]:
        run('effective-'+name, plan, process_override=override, error=True)
    run('machine-min', plan, machine_override={'min_layer_height':['0.18']}, error=True)
    run('filament-shrink', plan, filament_override={'filament_shrink':['99%']}, error=True)
    run('malformed', raw='{', error=True)
    assert all(digest(Path(path)) == expected for path,expected in identities.items()), 'Inputs changed during regression'
    save_json(root/'identities.json', identities)
    save_json(root/'completed.json', {'cases':len(summary), 'passed':all(row['passed'] for row in summary.values()),
                                    'physical_quality_verified':False})


if __name__ == '__main__':
    main()
