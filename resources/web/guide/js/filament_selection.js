// Keep the full preset identity visible and submit exactly the displayed preset.
// Dropping @model merges different printers and nozzle variants with the same material name.
function GetFilamentRows(profile)
{
    const models = profile.model.filter(model => model.nozzle_selected !== "");
    const selectedModels = models.flatMap(model => model.nozzle_selected.split(';')
        .map(nozzle => '[' + model.model + '++' + nozzle + ']'));
    return Object.keys(profile.filament).filter(key => {
        const models = profile.filament[key].models;
        return models === '' || selectedModels.some(model => models.indexOf(model) >= 0);
    }).map(key => Object.assign({}, profile.filament[key], {key: key}));
}

function IsDefaultFilament(name, defaults)
{
    // Older printer models list a material family; newer models list full preset names.
    return defaults.indexOf(name) >= 0 || defaults.indexOf(name.split('@')[0].trim()) >= 0;
}

function RenderFilamentRows(profile)
{
    // Build the list before applying selections; pending HTML is not in the DOM yet.
    let SelectNumber = 0;
    const TypeHtmlArray = {};
    const VendorHtmlArray = {};
    const rows = GetFilamentRows(profile);
    rows.sort((a, b) => Number(b.vendor.toLowerCase() === 'generic') - Number(a.vendor.toLowerCase() === 'generic'));
    $("#ItemBlockArea").empty();
    rows.forEach(filament => {
        const fVendor = filament.vendor;
        const fType = filament.type;
        const typeKey = fType.toLowerCase();
        const vendorKey = fVendor.toLowerCase();
        if (!Object.prototype.hasOwnProperty.call(TypeHtmlArray, typeKey)) {
            TypeHtmlArray[typeKey] = $('<label>').append($('<input>', {type: 'checkbox', filatype: fType})
                .on('change', FilaClick)).append($('<span>').text(fType));
        }
        if (!Object.prototype.hasOwnProperty.call(VendorHtmlArray, vendorKey)) {
            VendorHtmlArray[vendorKey] = $('<label>').append($('<input>', {type: 'checkbox', vendor: fVendor})
                .on('change', VendorClick)).append($('<span>').text(fVendor));
        }
        const selected = Number(filament.selected) === 1;
        const checkbox = $('<input>', {type: 'checkbox', vendor: fVendor, filatype: fType,
            model: filament.models, name: filament.key}).prop('checked', selected);
        $('<label>', {class: 'MItem', title: filament.name}).append(checkbox)
            .append($('<span>').text(filament.name)).appendTo('#ItemBlockArea');
        if (selected)
            SelectNumber++;
    });
    return {SelectNumber, TypeHtmlArray, VendorHtmlArray};
}
