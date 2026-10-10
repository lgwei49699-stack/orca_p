// Run: node --test tests/web/guide_filaments.test.cjs
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const vm = require('node:vm');
const plain = value => JSON.parse(JSON.stringify(value));
const material = (name, models, selected = 0, vendor = 'Generic') => ({ name, models, selected, vendor, type: 'PLA' });
const profile = {
    model: [{ model: 'Advancer3', nozzle_selected: '0.4;0.25' }, { model: 'EP3', nozzle_selected: '' }],
    filament: {
        'Generic PLA @EP3': material('Generic PLA @EP3', '[EP3++0.4]'),
        'Generic PLA @Advancer3': material('Generic PLA @Advancer3', '[Advancer3++0.4][Advancer3++0.6]', 1),
        'Generic PLA @Advancer3 0.25 nozzle': material('Generic PLA @Advancer3 0.25 nozzle', '[Advancer3++0.25]'),
        'Generic PLA': material('Generic PLA', ''),
        'PLA @Advancer3': material('PLA @Advancer3', '[Advancer3++0.4]', 0, 'Vendor'),
    },
};
for (const page of ['22', '23']) {
    const context = vm.createContext({});
    vm.runInContext(readFileSync(join(__dirname, '../../resources/web/guide/js/filament_selection.js'), 'utf8'), context);
    vm.runInContext(readFileSync(join(__dirname, `../../resources/web/guide/${page}/${page}.js`), 'utf8'), context);
    test(page + ': full model and nozzle identities survive equal Generic family names', () => {
        const rows = plain(context.GetFilamentRows(profile));
        assert.deepEqual(rows.map(row => row.key), Object.keys(profile.filament).slice(1));
        assert.equal(rows[0].name, 'Generic PLA @Advancer3');
        assert.equal(rows[0].selected, 1);
        assert.equal(rows[1].name, 'Generic PLA @Advancer3 0.25 nozzle');
    });
    test(page + ': machine filtering requires exact model/nozzle tokens', () => {
        const input = structuredClone(profile);
        input.model[0].nozzle_selected = '0.2';
        assert.deepEqual(plain(context.GetFilamentRows(input)).map(row => row.key), ['Generic PLA']);
    });
    test(page + ': different selected machines do not overwrite same material family', () => {
        const input = structuredClone(profile);
        input.model[1].nozzle_selected = '0.4';
        assert.equal(context.GetFilamentRows(input).length, 5);
    });
    test(page + ': full defaults select only the matching model and nozzle; legacy family defaults still work', () => {
        assert.equal(context.IsDefaultFilament('Generic PLA @Advancer3', ['Generic PLA @Advancer3']), true);
        assert.equal(context.IsDefaultFilament('Generic PLA @EP3', ['Generic PLA @Advancer3']), false);
        assert.equal(context.IsDefaultFilament('Generic PLA @Advancer3 0.25 nozzle', ['Generic PLA @Advancer3']), false);
        assert.equal(context.IsDefaultFilament('Generic PLA @EP3', ['Generic PLA']), true);
    });
    test(page + ': bridge sends only checked exact preset keys, without broadening to other printers', () => {
        const chosen = ['Generic PLA @Advancer3', 'Generic PLA @Advancer3 0.25 nozzle'];
        context.m_ProfileItem = profile;
        context.$ = selector => {
            assert.equal(selector, '#ItemBlockArea input:checked');
            return chosen.map(name => ({ getAttribute: attr => { assert.equal(attr, 'name'); return name; } }));
        };
        let message;
        context.SendWXMessage = text => { message = JSON.parse(text); };
        assert.equal(context.ResponseFilamentResult(), true);
        assert.equal(message.command, 'save_userguide_filaments');
        assert.deepEqual(message.data.filament, chosen);
    });
    test(page + ': empty selection remains rejected without writing a profile', () => {
        context.$ = () => [];
        let notice;
        context.ShowNotice = value => { notice = value; };
        context.SendWXMessage = () => assert.fail('must not save an empty selection');
        assert.equal(context.ResponseFilamentResult(), false);
        assert.equal(notice, 1);
    });
}
