var selectedPrinterType = 'filament'; // Default selected Filament
var IMG_BASE = '../../image/';

var PRINTER_IMGS = [
    IMG_BASE + 'FDM_default.png',
    IMG_BASE + 'FDM_unselected.png',
    IMG_BASE + 'LCD_default.png',
    IMG_BASE + 'LCD_unselected.png'
];

function preloadPrinterImages() {
    for (var i = 0; i < PRINTER_IMGS.length; i++) {
        var img = new Image();
        img.src = PRINTER_IMGS[i];
    }
}

function OnInit() {
    preloadPrinterImages();
    TranslatePage();
    
    // Apply initial selection (class + images)
    SelectPrinterType(selectedPrinterType);
    
    // Request saved printer type from backend
    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "request_printer_type";
    SendWXMessage(JSON.stringify(tSend));
    
    // Default will be set after receiving response
}

// Select printer type and update images (selected = default, unselected = unselected)
function SelectPrinterType(type) {
    selectedPrinterType = type;
    
    // Update selected class first so border and text color change immediately
    $('.PrinterTypeOption').removeClass('selected');
    if (type === 'filament') {
        $('#OptionFilament').addClass('selected');
    } else if (type === 'resin') {
        $('#OptionResin').addClass('selected');
    }
    
    // Then swap images (preloaded so no visible delay)
    if (type === 'filament') {
        $('#OptionFilament .OptionIcon img').attr('src', PRINTER_IMGS[0]);
        $('#OptionResin .OptionIcon img').attr('src', PRINTER_IMGS[3]);
    } else if (type === 'resin') {
        $('#OptionFilament .OptionIcon img').attr('src', PRINTER_IMGS[1]);
        $('#OptionResin .OptionIcon img').attr('src', PRINTER_IMGS[2]);
    }
}

// Confirm selection
function ConfirmSelection() {
    // Save selected data to C++; wait for C++ response before navigating so LCD_printer gets correct printer_type
    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "save_printer_type";
    tSend['data'] = { type: selectedPrinterType };
    SendWXMessage(JSON.stringify(tSend));
    // GotoNextPage() is called in HandleStudio when response_save_printer_type is received
}

// Navigate to previous page (used by close button)
function GotoPreviousPage() {
    window.location.href = "../11/index.html";
}

// Navigate to next page: FFF (Filament) -> 21, SLA (Resin) -> LCD_printer
function GotoNextPage() {
    if (selectedPrinterType === 'filament') {
        window.location.href = "../21/index.html";
    } else {
        window.location.href = "../LCD_printer/index.html?wizard=1";
    }
}

// Handle response from C++ (if needed)
function HandleStudio(pVal) {
    let strCmd = pVal['command'];

    if (strCmd == 'response_save_printer_type') {
        // C++ has applied printer_type; safe to navigate so next page (LCD_printer or 21) gets correct profile filter
        GotoNextPage();
        return;
    }
    if (strCmd == 'response_printer_type') {
        let savedType = pVal['type'] || 'filament';
        SelectPrinterType(savedType);
    }
}
