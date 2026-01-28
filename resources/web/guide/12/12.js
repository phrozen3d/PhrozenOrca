var selectedPrinterType = 'filament'; // Default selected Filament

function OnInit() {
    TranslatePage();
    
    // Request saved printer type from backend
    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "request_printer_type";
    SendWXMessage(JSON.stringify(tSend));
    
    // Default will be set after receiving response
}

// Select printer type
function SelectPrinterType(type) {
    selectedPrinterType = type;
    
    // Remove all selected states
    $('.PrinterTypeOption').removeClass('selected');
    
    // Add selected state
    if (type === 'filament') {
        $('#OptionFilament').addClass('selected');
    } else if (type === 'resin') {
        $('#OptionResin').addClass('selected');
    }
}

// Confirm selection
function ConfirmSelection() {
    // Save selected data to C++
    var tSend = {};
    tSend['sequence_id'] = Math.round(new Date() / 1000);
    tSend['command'] = "save_printer_type";
    tSend['data'] = { type: selectedPrinterType };
    SendWXMessage(JSON.stringify(tSend));
    
    // Navigate to next page
    GotoNextPage();
}

// Navigate to previous page (used by close button)
function GotoPreviousPage() {
    window.location.href = "../11/index.html";
}

// Navigate to next page
function GotoNextPage() {
    window.location.href = "../21/index.html";
}

// Handle response from C++ (if needed)
function HandleStudio(pVal) {
    let strCmd = pVal['command'];
    
    if (strCmd == 'response_printer_type') {
        let savedType = pVal['type'] || 'filament';
        SelectPrinterType(savedType);
    }
}
